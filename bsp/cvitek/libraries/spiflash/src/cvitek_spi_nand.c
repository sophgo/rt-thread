/*
 * cvitek_spi_nand.c - cvitek SPI NAND 控制器层（RT-Thread 系统层，只读）。
 *
 * 移植来源：
 *   - 控制器读/feature/DMA 序列 : RTT boot loader
 *       cvi_rtt/solutions/cv18xx_boot/application/cvi_spinand.c（mmio_* 风格，
 *       同一颗控制器、已 RTT shim 跑通）。
 *   - READID(0x9F) / RESET(0xFF) 寄存器序列 : u-boot-2021.10
 *       drivers/mtd/nand/raw/cvitek/cvsnfc.c 的 cvsnfc_send_cmd_readid /
 *       cvsnfc_send_cmd_reset（boot loader 版靠 FIP 参数拿几何、没有 read_id）。
 *
 * 寄存器/命令码定义内联在本文件（boot 侧 cv_spi_nand.h 不在系统层 include 路径）。
 */
#if defined(BOOT_SPINAND)

#include <rtthread.h>
#include <rthw.h>
#include <string.h>

#include "cvitek_spi_nand.h"
#include "mmio.h"
/* dcache 失效走 RTT 标准 API rt_hw_cpu_dcache_ops（rthw.h，RT_USING_CACHE=y
 * 时实体在 libcpu c906fdv cache.c；未开时 rthw.h 提供 no-op 桩）。 */

/* ---- 寄存器偏移（内联自 boot loader cv_spi_nand.h） ---- */
#define REG_SPI_NAND_TRX_CTRL0 0x000
#define BIT_REG_TRX_START (0x01 << 0)
#define REG_SPI_NAND_TRX_CTRL2 0x008
#define TRX_CMD_CONT_SIZE_SHIFT (0)
#define TRX_DATA_SIZE_SHIFT (16)
#define REG_SPI_NAND_TRX_CTRL3 0x00C
#define SPI_NAND_CTRL3_IO_TYPE_X2_MODE 2
#define SPI_NAND_CTRL3_IO_TYPE_X4_MODE 3
#define BIT_REG_TRX_RW (0x01 << 16)
#define BIT_REG_TRX_DUMMY_HIZ (0x01 << 17)
#define BIT_REG_TRX_DMA_EN (0x01 << 18)
#define REG_SPI_NAND_INT_EN 0x010
#define REG_SPI_NAND_INT_CLR 0x014
/* 所有 INT_CLR 位：TRX_DONE/EXCEPTION(bit0,1) + TX/RX push/pop err(bit4-7)
 * + DMA_DONE/E_TERM(bit8,9) = 0x3F3（同 boot loader BITS_SPI_NAND_INT_CLR_ALL）。*/
#define BITS_SPI_NAND_INT_CLR_ALL 0x000003f3
#define REG_SPI_NAND_INT_MASK 0x018
#define REG_SPI_NAND_INT 0x01C
#define BIT_REG_TRX_DONE_INT (0x01 << 0)
#define BIT_REG_DMA_DONE_INT (0x01 << 8)
#define BITS_REG_TRX_DMA_DONE_INT (BIT_REG_TRX_DONE_INT | BIT_REG_DMA_DONE_INT)
#define REG_SPI_NAND_TRX_CMD0 0x30
#define TRX_CMD_CONT0_SHIFT (8)
#define REG_SPI_NAND_RX_DATA 0x64

/* ---- 命令码 ---- */
#define SPI_NAND_CMD_GET_FEATURE 0x0F
#define SPI_NAND_CMD_SET_FEATURE 0x1F
#define SPI_NAND_CMD_PAGE_READ_TO_CACHE 0x13
#define SPI_NAND_CMD_READ_FROM_CACHE 0x03
#define SPI_NAND_CMD_READ_FROM_CACHEX2 0x3B
#define SPI_NAND_CMD_READ_FROM_CACHEX4 0x6B
#define SPI_NAND_CMD_READ_ID 0x9F
#define SPI_NAND_CMD_RESET 0xFF
#define SPI_NAND_CMD_WRITE_ENABLE 0x06
#define SPI_NAND_CMD_PROGRAM_LOAD 0x02
#define SPI_NAND_CMD_PROGRAM_LOADX4 0x32
#define SPI_NAND_CMD_PROGRAM_EXECUTE 0x10
#define SPI_NAND_CMD_BLOCK_ERASE 0xD8

/* ---- feature 位（Configuration/Status 寄存器） ---- */
#define SPI_NAND_STATUS0_OIP (0x01 << 0)

/* status(0xC0) 位；解保护寄存器 0xA0 */
#define SPI_NAND_STATUS_P_FAIL (0x01 << 3)
#define SPI_NAND_STATUS_E_FAIL (0x01 << 2)
#define SPI_NAND_STATUS_WEL (0x01 << 1)
#define SPI_NAND_FEATURE_PROTECT_ADDR 0xA0
#define SPI_NAND_FEATURE_PROTECT_NONE 0x00

#define SPI_NAND_PLANE_BIT_OFFSET (1UL << 12)

#define BASE_SYSDMAC 0x04330000

#define SPI_NAND_INTR_UDELAY_TIME 10
#define SPI_NAND_INTR_UDELAY_COUNT (100000000 / SPI_NAND_INTR_UDELAY_TIME)

static unsigned long ctrl_base = CVI_SPI_NAND_BASE;
static struct cvi_spi_nand_info nand_info;
static uint8_t ecc_en_status = 0xff;

/*
 * DMA bounce 缓冲：c906fdv 的 csi_dcache_invalid_range 对非 cache-line(64B)
 * 对齐的地址会把首/尾 line 整条失效（纯 invalidate 不回写），若缓冲来自
 * rt_malloc（仅 8B 对齐）则会丢弃相邻堆块的脏元数据 → rt_smem_free 断言崩。
 * 故所有 DMA 读/写一律经此 64B 对齐、长度 64B 对齐的静态缓冲中转，cache
 * 操作只作用于本缓冲、绝不跨相邻分配。NAND 操作串行、单控制器，无重入。
 * 尺寸覆盖最大几何 page(4096)+oob(256)，向上取整到 64B。 */
#define SPI_NAND_CACHE_LINE 64
#define SPI_NAND_DMA_BOUF_SIZE 4352 /* (4096+256)，已是 64 的整数倍 */
static uint8_t spi_nand_dma_buf[SPI_NAND_DMA_BOUF_SIZE]
    __attribute__((aligned(SPI_NAND_CACHE_LINE)));

/* 把长度按 cache line 向上取整，供 dcache 失效/回写用（不越过 bounce 边界）。 */
static inline uint32_t spi_nand_cacheline_round(uint32_t len)
{
    return (len + SPI_NAND_CACHE_LINE - 1) & ~(SPI_NAND_CACHE_LINE - 1);
}

/* ---------- feature ops（照抄 boot loader cvi_spinand.c） ---------- */

static int spi_nand_set_feature(uint8_t fe, uint8_t val)
{
    uint32_t fe_set = fe | (val << 8);
    uint32_t retry = 0;

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2, 2 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, BIT_REG_TRX_RW);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                  (fe_set << TRX_CMD_CONT0_SHIFT) | SPI_NAND_CMD_SET_FEATURE);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    if (retry > SPI_NAND_INTR_UDELAY_COUNT)
        rt_kprintf("[E] SPINAND set_feature no interrupt\n");

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);
    return 0;
}

static void spi_nand_setup_intr(void)
{
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_EN, 3);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BITS_SPI_NAND_INT_CLR_ALL);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_MASK, 0x00000f00);
}

static uint8_t spi_nand_get_feature(uint32_t fe)
{
    uint32_t val = 0;
    uint32_t retry = 0;

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2,
                  1 << TRX_DATA_SIZE_SHIFT | 1 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0, fe << 8 | SPI_NAND_CMD_GET_FEATURE);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    if (retry > SPI_NAND_INTR_UDELAY_COUNT)
        rt_kprintf("[E] SPINAND get_feature no interrupt\n");

    val = mmio_read_32(ctrl_base + REG_SPI_NAND_RX_DATA);

    uint32_t intr = mmio_read_32(ctrl_base + REG_SPI_NAND_INT);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, intr);

    return (val & 0xff);
}

/* ---------- reset / read_id（序列取自 u-boot cvsnfc.c） ---------- */

int cvi_spi_nand_reset(void)
{
    uint32_t retry = 0;

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0, SPI_NAND_CMD_RESET);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);

    /* 复位后 OIP 拉高，等其清零。 */
    retry = 0;
    while ((spi_nand_get_feature(0xc0) & SPI_NAND_STATUS0_OIP) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    return 0;
}

int cvi_spi_nand_read_id(uint8_t *id_out)
{
    uint32_t retry = 0;
    uint32_t data;

    /* CTRL2 = DATA_SIZE(3)<<16 | CMD_CONT_SIZE(1)：0x9F + 1 字节地址，读回 3 字节。
     * 与 u-boot cvsnfc_send_cmd_readid 的 0x00030001 一致。 */
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2,
                  3 << TRX_DATA_SIZE_SHIFT | 1 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0, SPI_NAND_CMD_READ_ID);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    if (retry > SPI_NAND_INTR_UDELAY_COUNT) {
        rt_kprintf("[E] SPINAND read_id no interrupt\n");
        return -1;
    }

    /* 3 字节 ID 落在一个 32-bit RX_DATA 里（低 3 字节，MID 在 bit0..7）。 */
    data = mmio_read_32(ctrl_base + REG_SPI_NAND_RX_DATA);

    uint32_t intr = mmio_read_32(ctrl_base + REG_SPI_NAND_INT);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, intr);

    id_out[0] = data & 0xff;
    id_out[1] = (data >> 8) & 0xff;
    id_out[2] = (data >> 16) & 0xff;
    return 0;
}

/* ---------- setup / info ---------- */

void cvi_spi_nand_setup(const struct spi_nand_chip_info *chip)
{
    uint32_t ppb;

    memset(&nand_info, 0, sizeof(nand_info));
    nand_info.name = chip->name;
    nand_info.id_len = chip->id_len;
    memcpy(nand_info.id, chip->id, SPI_NAND_MAX_ID_LEN);

    nand_info.page_size = chip->pagesize;
    nand_info.oob_size = chip->oobsize;
    nand_info.block_size = chip->erasesize;
    ppb = chip->erasesize / chip->pagesize;
    nand_info.pages_per_block = ppb;
    nand_info.block_total = (uint32_t)(chip->chipsize / chip->erasesize);
    nand_info.pages_per_block_shift = (uint8_t)__builtin_ctz(ppb);

    nand_info.flags = chip->flags;
    nand_info.badblock_pos = chip->badblock_pos;
    nand_info.ecc_en_feature_offset = chip->ecc_en_feature_offset;
    nand_info.ecc_en_mask = chip->ecc_en_mask;
    nand_info.ecc_status_offset = chip->ecc_status_offset;
    nand_info.ecc_status_mask = chip->ecc_status_mask;
    nand_info.ecc_status_shift = chip->ecc_status_shift;
    nand_info.ecc_status_uncorr_val = chip->ecc_status_uncorr_val;

    ecc_en_status = 0xff; /* 强制下次 ctrl_ecc 真正写 feature */
}

const struct cvi_spi_nand_info *cvi_spi_nand_get_info(void)
{
    return &nand_info;
}

/* ---------- 读路径（照抄 boot loader cvi_spinand.c 的寄存器序列） ---------- */

static void spi_nand_ctrl_ecc(uint8_t enable)
{
    uint8_t status;

    if (ecc_en_status == enable)
        return;

    status = spi_nand_get_feature(nand_info.ecc_en_feature_offset);
    if (enable)
        spi_nand_set_feature(nand_info.ecc_en_feature_offset,
                             status | nand_info.ecc_en_mask);
    else
        spi_nand_set_feature(nand_info.ecc_en_feature_offset,
                             status & (~nand_info.ecc_en_mask));
    ecc_en_status = enable;
}

/* 解写保护 + WREN(0x06) + 读回校验 WEL。返回 0=WEL 置位成功，<0 失败。
 * NAND 上电默认 block-protect，program/erase 前必做。 */
static int spi_nand_write_enable(void)
{
    uint8_t st;
    uint32_t retry = 0;

    /* 1. 清 block-protect：解写保护（上电默认写保护，必做）。 */
    spi_nand_set_feature(SPI_NAND_FEATURE_PROTECT_ADDR, SPI_NAND_FEATURE_PROTECT_NONE);

    /* 2. 发 WREN(0x06)（无数据、无地址；同 reset 的最简 CMD 序列）。 */
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0, SPI_NAND_CMD_WRITE_ENABLE);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);

    /* 3. 读回 status 校验 WEL。 */
    st = spi_nand_get_feature(0xc0);
    return (st & SPI_NAND_STATUS_WEL) ? 0 : -1;
}

static void spi_nand_rw_dma_setup(void *buf, uint32_t len, uint32_t rw)
{
    uint32_t ch = 0;

    mmio_write_32(BASE_SYSDMAC + 0x010, 0x00000003);
    mmio_write_32(BASE_SYSDMAC + 0x018, 0x00000f00);
    mmio_write_32(BASE_SYSDMAC + 0x110 + ch * 0x100, (len / 4) - 1);
    mmio_write_32(BASE_SYSDMAC + 0x11C + ch * 0x100, 0x000f0792);

    if (rw) { /* MEM_TO_PER：DDR -> write cache(ctrl_base+0x800) */
        mmio_write_64(BASE_SYSDMAC + 0x100 + ch * 0x100, (uint64_t)(uintptr_t)buf);
        mmio_write_64(BASE_SYSDMAC + 0x108 + ch * 0x100, ctrl_base + 0x800);
        mmio_write_32(BASE_SYSDMAC + 0x118 + ch * 0x100, 0x00045441);
        mmio_write_32(BASE_SYSDMAC + 0x124 + ch * 0x100, 0x00000001);
    } else {  /* PER_TO_MEM：read cache(ctrl_base+0xC00) -> DDR（现有读路径） */
        mmio_write_64(BASE_SYSDMAC + 0x100 + ch * 0x100, ctrl_base + 0xC00);
        mmio_write_64(BASE_SYSDMAC + 0x108 + ch * 0x100, (uint64_t)(uintptr_t)buf);
        mmio_write_32(BASE_SYSDMAC + 0x118 + ch * 0x100, 0x00046214);
        mmio_write_32(BASE_SYSDMAC + 0x124 + ch * 0x100, 0x00000002);
    }
    mmio_write_32(0x03000154, 0x26);
    mmio_setbits_32(0x03000154, (1UL << 31));

    mmio_write_32(BASE_SYSDMAC + 0x120 + ch * 0x100, 0x0);
    mmio_write_32(BASE_SYSDMAC + 0x018, 0x00000f0f);
}

static void spi_nand_polling_oip(void)
{
    uint32_t status = 0xff;
    uint32_t retry = 0;

    while ((status & SPI_NAND_STATUS0_OIP) &&
           (retry++ < (SPI_NAND_INTR_UDELAY_COUNT * 10))) {
        status = spi_nand_get_feature(nand_info.ecc_status_offset);
        rt_hw_us_delay(1);
    }
}

static int spi_nand_page_read_to_cache(uint32_t row_addr)
{
    uint32_t retry = 0;
    uint32_t r_row_addr = ((row_addr & 0xff0000) >> 16) | (row_addr & 0xff00) |
                          ((row_addr & 0xff) << 16);

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2,
                  0 << TRX_DATA_SIZE_SHIFT | 3 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                  r_row_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_PAGE_READ_TO_CACHE);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);
    spi_nand_polling_oip();
    return 0;
}

static void spi_nand_set_read_from_cache_mode(uint32_t col_addr)
{
    if (nand_info.flags & FLAGS_ENABLE_X4_BIT) {
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3,
                      BIT_REG_TRX_DMA_EN | SPI_NAND_CTRL3_IO_TYPE_X4_MODE | BIT_REG_TRX_DUMMY_HIZ);
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                      col_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_READ_FROM_CACHEX4);
    } else if (nand_info.flags & FLAGS_ENABLE_X2_BIT) {
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3,
                      BIT_REG_TRX_DMA_EN | SPI_NAND_CTRL3_IO_TYPE_X2_MODE | BIT_REG_TRX_DUMMY_HIZ);
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                      col_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_READ_FROM_CACHEX2);
    } else {
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3,
                      BIT_REG_TRX_DMA_EN | BIT_REG_TRX_DUMMY_HIZ);
        mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                      col_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_READ_FROM_CACHE);
    }
}

static int spi_nand_read_from_cache(uint32_t col_addr, uint32_t len, void *buf)
{
    uint32_t r_col_addr = ((col_addr & 0xff00) >> 8) | ((col_addr & 0xff) << 8);

    spi_nand_rw_dma_setup(buf, len, 0);

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2,
                  len << TRX_DATA_SIZE_SHIFT | 3 << TRX_CMD_CONT_SIZE_SHIFT);

    spi_nand_set_read_from_cache_mode(r_col_addr);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while ((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BITS_REG_TRX_DMA_DONE_INT) == 0)
        ;

    uint32_t intr = mmio_read_32(ctrl_base + REG_SPI_NAND_INT);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, intr);
    return 0;
}

static int spi_nand_parse_ecc(uint32_t page)
{
    uint32_t statusc0 = spi_nand_get_feature(nand_info.ecc_status_offset);
    uint8_t ecc_sts = (statusc0 & nand_info.ecc_status_mask) >> nand_info.ecc_status_shift;

    if (ecc_sts == nand_info.ecc_status_uncorr_val) {
        rt_kprintf("[E] SPINAND ECC UNCORR at page %u\n", page);
        return CVI_SPI_NAND_ECC_UNCORR;
    }
    if (ecc_sts)
        return CVI_SPI_NAND_ECC_CORR;

    return CVI_SPI_NAND_ECC_NOERR;
}

int cvi_spi_nand_read_page(uint32_t page, void *buf, uint32_t len)
{
    uint32_t col_addr = 0;
    uint32_t blk_id = page >> nand_info.pages_per_block_shift;

    if (len > SPI_NAND_DMA_BOUF_SIZE)
        return CVI_SPI_NAND_ECC_UNCORR;

    spi_nand_ctrl_ecc(1);
    spi_nand_page_read_to_cache(page);

    if ((nand_info.flags & FLAGS_SET_PLANE_BIT) && (blk_id % 2 == 1))
        col_addr |= SPI_NAND_PLANE_BIT_OFFSET;

    /* DMA 落对齐 bounce，失效按 cache line 取整（bounce 独占这些 line）。 */
    rt_hw_cpu_dcache_ops(RT_HW_CACHE_INVALIDATE, spi_nand_dma_buf,
                         (int)spi_nand_cacheline_round(len));
    spi_nand_read_from_cache(col_addr, len, spi_nand_dma_buf);
    rt_memcpy(buf, spi_nand_dma_buf, len);

    return spi_nand_parse_ecc(page);
}

/* ---------- 写路径：program load(DMA) / program execute / write_page ---------- */

/* 把 buf 经 DMA 写进 write cache 的 col_addr 处（program load，不落盘）。 */
static void spi_nand_program_load(uint32_t col_addr, const void *buf, uint32_t len)
{
    uint32_t r_col_addr = ((col_addr & 0xff00) >> 8) | ((col_addr & 0xff) << 8);
    /* 写方向一律用单线 program load(0x02)。FLAGS_ENABLE_X2/X4_BIT 只描述器件
     * 支持 quad/dual READ-from-cache（u-boot 仅在 spi_nand_read_from_cache
     * cvsnfc.c:1098/1100 消费），与写无关。u-boot 的 spi_nand_prog_load 两处调用
     * (cvsnfc.c:1535/1631) 均传 qe=0 → 写恒走 X1(0x02)、CTRL3=RW|DMA_EN、无
     * IO_TYPE/无 DUMMY_HIZ；quad load(0x32) 需先置器件 QE 位，u-boot 从不启用。
     * 本移植原按读侧 X4 标志选 0x32，却用 X1 时序发送 → 位打散(fe/ef 周期)+溢出
     * 污染 OOB。写不追求速度，X1 最稳，与 u-boot 实际写路径一致。 */
    uint8_t cmd = SPI_NAND_CMD_PROGRAM_LOAD;

    /* 源数据先拷进对齐 bounce，dcache FLUSH 只作用于 bounce（独占 cache line），
     * 避免对非对齐调用者缓冲做 clean+invalidate 丢邻块脏数据。 */
    rt_memcpy(spi_nand_dma_buf, buf, len);
    rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH, spi_nand_dma_buf,
                         (int)spi_nand_cacheline_round(len));
    spi_nand_rw_dma_setup(spi_nand_dma_buf, len, 1);

    /* CMD_CONT_SIZE = 命令后的续传字节数（不含命令字节本身）。
     * program load(0x02) = cmd + 2 字节列地址、无 dummy → 续传 2 字节；
     * 对照 u-boot cvsnfc.c:1450 spi_nand_prog_load 的 `2 << CMD_CONT_SIZE`。
     * 读路径(0x03)因带 1 dummy 才用 3，写路径误用 3 会多打一个字节，
     * 使 2048 数据整体后移 1 字节、末字节溢出到 col 2048（OOB badblock）。 */
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2,
                  len << TRX_DATA_SIZE_SHIFT | 2 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, BIT_REG_TRX_RW | BIT_REG_TRX_DMA_EN);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                  r_col_addr << TRX_CMD_CONT0_SHIFT | cmd);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while ((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BITS_REG_TRX_DMA_DONE_INT) == 0)
        ;

    uint32_t intr = mmio_read_32(ctrl_base + REG_SPI_NAND_INT);
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, intr);
}

/* program execute（落盘，row_addr）。 */
static void spi_nand_program_execute(uint32_t row_addr)
{
    uint32_t retry = 0;
    uint32_t r_row_addr = ((row_addr & 0xff0000) >> 16) | (row_addr & 0xff00) |
                          ((row_addr & 0xff) << 16);

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2, 3 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                  r_row_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_PROGRAM_EXECUTE);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);
    spi_nand_polling_oip();
}

int cvi_spi_nand_write_page(uint32_t page, const void *buf, uint32_t len)
{
    uint32_t col_addr = 0;
    uint32_t blk_id = page >> nand_info.pages_per_block_shift;
    uint8_t st;

    if (len > SPI_NAND_DMA_BOUF_SIZE)
        return -1;

    spi_nand_ctrl_ecc(1);
    if (spi_nand_write_enable() != 0)
        return -1;

    if ((nand_info.flags & FLAGS_SET_PLANE_BIT) && (blk_id % 2 == 1))
        col_addr |= SPI_NAND_PLANE_BIT_OFFSET;

    spi_nand_program_load(col_addr, buf, len);
    spi_nand_program_execute(page);

    st = spi_nand_get_feature(0xc0);
    return (st & SPI_NAND_STATUS_P_FAIL) ? -1 : 0;
}

/* ---------- 擦块 ---------- */

int cvi_spi_nand_erase_block(uint32_t block)
{
    uint32_t row_addr = block << nand_info.pages_per_block_shift; /* 块首页 */
    uint32_t r_row_addr = ((row_addr & 0xff0000) >> 16) | (row_addr & 0xff00) |
                          ((row_addr & 0xff) << 16);
    uint32_t retry = 0;
    uint8_t st;

    if (block >= nand_info.block_total)
        return -1;

    if (spi_nand_write_enable() != 0)
        return -1;

    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL2, 3 << TRX_CMD_CONT_SIZE_SHIFT);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CTRL3, 0);
    mmio_write_32(ctrl_base + REG_SPI_NAND_TRX_CMD0,
                  r_row_addr << TRX_CMD_CONT0_SHIFT | SPI_NAND_CMD_BLOCK_ERASE);

    spi_nand_setup_intr();
    mmio_setbits_32(ctrl_base + REG_SPI_NAND_TRX_CTRL0, BIT_REG_TRX_START);

    while (((mmio_read_32(ctrl_base + REG_SPI_NAND_INT) & BIT_REG_TRX_DONE_INT) == 0) &&
           (retry++ < SPI_NAND_INTR_UDELAY_COUNT))
        rt_hw_us_delay(1);

    mmio_setbits_32(ctrl_base + REG_SPI_NAND_INT_CLR, BIT_REG_TRX_DONE_INT);
    spi_nand_polling_oip();

    st = spi_nand_get_feature(0xc0);
    return (st & SPI_NAND_STATUS_E_FAIL) ? -1 : 0;
}

/* ---------- OOB 坏块标记读/写 ---------- */

int cvi_spi_nand_read_bbm(uint32_t block, uint8_t *marker)
{
    uint32_t page = block << nand_info.pages_per_block_shift;
    uint32_t total = nand_info.page_size + nand_info.oob_size;
    uint8_t *tmp;

    if (block >= nand_info.block_total)
        return -1;

    tmp = rt_malloc(total);
    if (tmp == RT_NULL)
        return -1;

    cvi_spi_nand_read_page(page, tmp, total);
    *marker = tmp[nand_info.page_size + nand_info.badblock_pos];
    rt_free(tmp);
    return 0;
}

int cvi_spi_nand_write_bbm(uint32_t block, uint8_t marker)
{
    uint32_t page = block << nand_info.pages_per_block_shift;
    uint32_t col_addr = nand_info.page_size + nand_info.badblock_pos; /* OOB 内偏移 */
    uint8_t st;

    if (block >= nand_info.block_total)
        return -1;

    if ((nand_info.flags & FLAGS_SET_PLANE_BIT) && (block % 2 == 1))
        col_addr |= SPI_NAND_PLANE_BIT_OFFSET;

    spi_nand_ctrl_ecc(0); /* 坏块标记写 OOB 原始字节，关 ECC 避免改写 marker */
    if (spi_nand_write_enable() != 0)
        return -1;

    spi_nand_program_load(col_addr, &marker, 1);
    spi_nand_program_execute(page);

    st = spi_nand_get_feature(0xc0);
    return (st & SPI_NAND_STATUS_P_FAIL) ? -1 : 0;
}

int cvi_spi_nand_ctrl_init(void)
{
    ctrl_base = CVI_SPI_NAND_BASE;
    spi_nand_setup_intr();
    return cvi_spi_nand_reset();
}

#endif /* BOOT_SPINAND */
