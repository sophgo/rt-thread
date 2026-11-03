/*
 * nandflash.c - RT-Thread SPI NAND 适配层（系统层，探测 + 读）。
 *
 * 职责：
 *   - 实现 struct rt_mtd_nand_driver_ops：read_id + read_page（真实现），
 *     write/move/erase/check/mark 留桩（返回 -RT_MTD_EIO，见各桩 TODO）。
 *   - rt_flash_init_nand()：初始化控制器 -> read_id -> 匹配支持表 ->
 *     填 rt_mtd_nand_device 几何 -> 注册 "nand0"。匹配不到时优雅打印后返回
 *     错误码，绝不崩（对齐验收要求）。
 *
 * 控制器/支持表分别在 cvitek_spi_nand.c / spi_nand_ids.c。
 */
#if defined(BOOT_SPINAND)

#include <rtthread.h>
#include <drivers/mtd_nand.h>

#include "cvitek_spi_nand.h"
#include "spi_nand_ids.h"

static struct rt_mtd_nand_device nand_mtd_device;

/* ---------- ops：read_id ---------- */
static rt_err_t nand_read_id(struct rt_mtd_nand_device *device)
{
    uint8_t id[SPI_NAND_MAX_ID_LEN] = {0};

    if (cvi_spi_nand_read_id(id) != 0)
        return -RT_MTD_EIO;

    rt_kprintf("spi nand id: %02x %02x %02x\n", id[0], id[1], id[2]);
    return RT_EOK;
}

/* ---------- ops：read_page（data + 可选 spare/oob） ----------
 * 说明（OOB/spare 处理）：控制器一次 read_from_cache 从 cache 的 col_addr=0
 * 起连续读。本实现按 [data | spare] 拼一次 DMA 读：
 *   - data_len<=page_size 时读 data；
 *   - 若同时要 spare，则一次读 data_len+spare_len 字节（spare 紧跟在 page 数据
 *     之后的 OOB 区），再把尾部 spare_len 字节 memcpy 给 spare 缓冲。
 * 本次范围只保证 data 路径；spare 读取按此拼接实现，未做 OOB layout/ECC byte
 * 精细区分（坏块/ECC 校验回读不在范围）。 */
static rt_err_t nand_read_page(struct rt_mtd_nand_device *device, rt_off_t page,
                               rt_uint8_t *data, rt_uint32_t data_len,
                               rt_uint8_t *spare, rt_uint32_t spare_len)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    int ecc;

    if (data && data_len) {
        ecc = cvi_spi_nand_read_page((uint32_t)page, data, data_len);
        if (ecc == CVI_SPI_NAND_ECC_UNCORR)
            return -RT_MTD_EECC;
    }

    if (spare && spare_len) {
        /* spare/oob：从整页读一次，取 page 之后的 spare_len 字节。用一段临时缓冲
         * (page + oob) 承接一次 DMA 读，避免单独访问 cache 尾部。 */
        rt_uint32_t total = info->page_size + spare_len;
        rt_uint8_t *tmp = rt_malloc(total);

        if (tmp == RT_NULL)
            return -RT_MTD_ENOMEM;

        ecc = cvi_spi_nand_read_page((uint32_t)page, tmp, total);
        rt_memcpy(spare, tmp + info->page_size, spare_len);
        rt_free(tmp);

        if (ecc == CVI_SPI_NAND_ECC_UNCORR)
            return -RT_MTD_EECC;
    }

    return RT_EOK;
}

/* ---------- ops：write/move/erase/check/mark ---------- */
static rt_err_t nand_write_page(struct rt_mtd_nand_device *device, rt_off_t page,
                                const rt_uint8_t *data, rt_uint32_t data_len,
                                const rt_uint8_t *spare, rt_uint32_t spare_len)
{
    if (data && data_len) {
        if (cvi_spi_nand_write_page((uint32_t)page, data, data_len) != 0)
            return -RT_MTD_EIO;
    }
    /* 本次范围：spare 独立写入不支持（UFFS 走 data 区 + 自身 OOB 语义时再评估）。 */
    if (spare && spare_len)
        return -RT_MTD_EIO;
    return RT_EOK;
}

static rt_err_t nand_move_page(struct rt_mtd_nand_device *device, rt_off_t src, rt_off_t dst)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    rt_uint8_t *tmp = rt_malloc(info->page_size);
    rt_err_t ret = RT_EOK;
    int ecc;

    if (tmp == RT_NULL)
        return -RT_MTD_ENOMEM;
    ecc = cvi_spi_nand_read_page((uint32_t)src, tmp, info->page_size);
    if (ecc == CVI_SPI_NAND_ECC_UNCORR) {
        rt_free(tmp);
        return -RT_MTD_EECC;
    }
    if (cvi_spi_nand_write_page((uint32_t)dst, tmp, info->page_size) != 0)
        ret = -RT_MTD_EIO;
    rt_free(tmp);
    return ret;
}

static rt_err_t nand_erase_block(struct rt_mtd_nand_device *device, rt_uint32_t block)
{
    return (cvi_spi_nand_erase_block(block) == 0) ? RT_EOK : -RT_MTD_EIO;
}

static rt_err_t nand_check_block(struct rt_mtd_nand_device *device, rt_uint32_t block)
{
    uint8_t marker = 0xff;

    if (cvi_spi_nand_read_bbm(block, &marker) != 0)
        return -RT_MTD_EIO;
    /* badblock_pos 字节 != 0xFF => 坏块。 */
    return (marker == 0xff) ? RT_EOK : -RT_MTD_EBUSY;
}

static rt_err_t nand_mark_block(struct rt_mtd_nand_device *device, rt_uint32_t block)
{
    return (cvi_spi_nand_write_bbm(block, 0x00) == 0) ? RT_EOK : -RT_MTD_EIO;
}

static const struct rt_mtd_nand_driver_ops nand_ops = {
    nand_read_id,
    nand_read_page,
    nand_write_page,
    nand_move_page,
    nand_erase_block,
    nand_check_block,
    nand_mark_block,
};

int rt_flash_init_nand(void)
{
    uint8_t id[SPI_NAND_MAX_ID_LEN] = {0};
    const struct spi_nand_chip_info *chip;
    const struct cvi_spi_nand_info *info;

    if (cvi_spi_nand_ctrl_init() != 0) {
        rt_kprintf("spi nand: controller init failed\n");
        return -RT_ERROR;
    }

    if (cvi_spi_nand_read_id(id) != 0) {
        rt_kprintf("spi nand: read id failed\n");
        return -RT_ERROR;
    }

    chip = spi_nand_ids_match(id);
    if (chip == RT_NULL) {
        /* 优雅退出，不解引用、不 assert、不崩。 */
        rt_kprintf("unsupported spi nand id: %02x %02x %02x\n", id[0], id[1], id[2]);
        return -RT_ERROR;
    }

    cvi_spi_nand_setup(chip);
    info = cvi_spi_nand_get_info();

    rt_kprintf("spi nand: %s detected, id %02x %02x %02x\n",
               info->name, info->id[0], info->id[1], info->id[2]);
    rt_kprintf("spi nand: page %u B, oob %u B, block %u B, pages/block %u, blocks %u\n",
               info->page_size, info->oob_size, info->block_size,
               info->pages_per_block, info->block_total);

    rt_memset(&nand_mtd_device, 0, sizeof(nand_mtd_device));
    nand_mtd_device.page_size = (rt_uint16_t)info->page_size;
    nand_mtd_device.oob_size = (rt_uint16_t)info->oob_size;
    nand_mtd_device.oob_free = 0;
    nand_mtd_device.plane_num = 1;
    nand_mtd_device.pages_per_block = info->pages_per_block;
    nand_mtd_device.block_total = (rt_uint16_t)info->block_total;
    nand_mtd_device.block_start = 0;
    nand_mtd_device.block_end = info->block_total;
    nand_mtd_device.ops = &nand_ops;
    nand_mtd_device.priv = RT_NULL;

    return rt_mtd_nand_register_device("nand0", &nand_mtd_device);
}

#endif /* BOOT_SPINAND */
