/*
 * cvitek_spi_nand.h - cvitek SPI NAND 控制器层（RT-Thread 系统层）。
 *
 * 寄存器定义与控制器读/feature/reset/DMA 序列移植自 RTT boot loader
 *   cvi_rtt/solutions/cv18xx_boot/application/cvi_spinand.c + cv_spi_nand.h
 * （同一颗控制器、已 RTT shim 跑通）。READID / RESET 的寄存器序列取自
 * u-boot cvsnfc.c 的 cvsnfc_send_cmd_readid / cvsnfc_send_cmd_reset
 * （boot loader 版靠 FIP 参数拿几何、没有 read_id）。
 *
 * 本层只做「探测 + 读」：reset / read_id / 按几何 read_page（含 ECC 状态解析）。
 * write / erase / 坏块留给上层适配层桩，不在本层实现。
 */
#ifndef __CVITEK_SPI_NAND_H__
#define __CVITEK_SPI_NAND_H__

#if defined(BOOT_SPINAND)

#include <stdint.h>
#include "spi_nand_ids.h"

/* SPI NAND 控制器基址：HSPERI(0x04000000) + 0x60000（同 boot loader）。 */
#define CVI_SPI_NAND_BASE 0x04060000

/* ECC 解析返回值。 */
#define CVI_SPI_NAND_ECC_NOERR 0
#define CVI_SPI_NAND_ECC_CORR 1
#define CVI_SPI_NAND_ECC_UNCORR 2

/*
 * 探测得到的器件运行态信息：几何 + ECC 状态字段 + 读模式 flags，
 * 由 cvi_spi_nand_setup() 从匹配到的 spi_nand_chip_info 填入。
 */
struct cvi_spi_nand_info {
    const char *name;
    uint8_t id[SPI_NAND_MAX_ID_LEN];
    uint8_t id_len;

    uint32_t page_size;      /* 字节 */
    uint32_t oob_size;       /* 字节 */
    uint32_t block_size;     /* 字节 */
    uint32_t pages_per_block;
    uint32_t block_total;
    uint8_t pages_per_block_shift;

    uint32_t flags;          /* FLAGS_ENABLE_X2/X4/SET_QE/SET_PLANE_BIT */
    uint8_t badblock_pos;

    uint8_t ecc_en_feature_offset;
    uint8_t ecc_en_mask;
    uint8_t ecc_status_offset;
    uint8_t ecc_status_mask;
    uint8_t ecc_status_shift;
    uint8_t ecc_status_uncorr_val;
};

/* 初始化控制器（中断/复位），返回 0 成功。 */
int cvi_spi_nand_ctrl_init(void);

/* 复位器件（RESET 0xFF），返回 0 成功。 */
int cvi_spi_nand_reset(void);

/*
 * 发 READID(0x9F) 读器件 ID。
 *   id_out : 至少 SPI_NAND_MAX_ID_LEN 字节，写入读到的 ID。
 * 返回 0 成功。
 */
int cvi_spi_nand_read_id(uint8_t *id_out);

/* 用匹配到的支持表项装载运行态几何/ECC 信息（供后续 read_page 使用）。 */
void cvi_spi_nand_setup(const struct spi_nand_chip_info *chip);

/* 取当前运行态信息指针（cvi_spi_nand_setup 之后有效）。 */
const struct cvi_spi_nand_info *cvi_spi_nand_get_info(void);

/*
 * 读一页数据（page read to cache -> read from cache via DMA）。
 *   page : 页号（row address）
 *   buf  : 目标缓冲，长度 >= len
 *   len  : 读取字节数（<= page_size）
 * 返回 CVI_SPI_NAND_ECC_NOERR / _CORR / _UNCORR。
 */
int cvi_spi_nand_read_page(uint32_t page, void *buf, uint32_t len);

/* 写一页（write_enable -> program_load(DMA) -> program_execute -> 查 P_FAIL）。
 * page: 行地址；buf: 源数据(>=len)；len: <=page_size。返回 0 成功，<0 P_FAIL。 */
int cvi_spi_nand_write_page(uint32_t page, const void *buf, uint32_t len);

/* 擦一块（write_enable -> block_erase(0xD8, row=块首页) -> polling -> 查 E_FAIL）。
 * block: 块号。返回 0 成功，<0 E_FAIL。 */
int cvi_spi_nand_erase_block(uint32_t block);

/* 读/写 block 首页 OOB 的 badblock_pos 字节（坏块判定/标记）。返回 0 成功。 */
int cvi_spi_nand_read_bbm(uint32_t block, uint8_t *marker);
int cvi_spi_nand_write_bbm(uint32_t block, uint8_t marker);

/*
 * 系统层 NAND 入口（适配层 nandflash.c 实现）：探测 -> read_id -> 匹配支持表
 * -> 填 rt_mtd_nand_device 几何 -> 注册 "nand0"。供 spiflash.c 的
 * rt_flash_init() 在 BOOT_SPINAND 分支下调用。返回 RT_EOK / 负错误码。
 */
int rt_flash_init_nand(void);

#endif /* BOOT_SPINAND */
#endif /* __CVITEK_SPI_NAND_H__ */
