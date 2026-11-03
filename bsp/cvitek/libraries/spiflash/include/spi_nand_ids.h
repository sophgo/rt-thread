/*
 * spi_nand_ids.h - RT-Thread system-layer SPI NAND device support table.
 *
 * 精简自 u-boot-2021.10 drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c
 * (SPI_NAND_ID_TAB_VER "1.3", struct cvsnfc_chip_info)。
 * u-boot 版深绑 MTD 框架（driver 回调指针 / read[]/write[]/erase[] op 指针 /
 * diesize 多 die），系统层「探测 + 读」用不到，全部裁掉；只保留探测与
 * read_page / ECC 状态解析所需字段，用本文件的精简 struct spi_nand_chip_info。
 */
#ifndef __SPI_NAND_IDS_H__
#define __SPI_NAND_IDS_H__

#if defined(BOOT_SPINAND)

#include <stdint.h>

/* 器件 ID 最大字节数（厂商 ID + 器件 ID，最多 3 字节）。 */
#define SPI_NAND_MAX_ID_LEN 3
/* 匹配时默认先比较的字节数（厂商 + 器件），与 u-boot DEFAULT_ID_LEN 一致。 */
#define SPI_NAND_DEFAULT_ID_LEN 2

/* 坏块标记位置（沿用 boot loader / u-boot 定义）。 */
#ifndef BBP_LAST_PAGE
#define BBP_LAST_PAGE 0x01
#endif
#ifndef BBP_FIRST_PAGE
#define BBP_FIRST_PAGE 0x02
#endif
#ifndef BBP_FIRST_2_PAGE
#define BBP_FIRST_2_PAGE 0x03
#endif

/* 读模式/QE flags（与 boot loader cv_spi_nand.h 的同名宏取值一致）。 */
#ifndef FLAGS_SET_PLANE_BIT
#define FLAGS_SET_PLANE_BIT (1UL << 0)
#endif
#ifndef FLAGS_SET_QE_BIT
#define FLAGS_SET_QE_BIT (1UL << 1)
#endif
#ifndef FLAGS_ENABLE_X2_BIT
#define FLAGS_ENABLE_X2_BIT (1UL << 2)
#endif
#ifndef FLAGS_ENABLE_X4_BIT
#define FLAGS_ENABLE_X4_BIT (1UL << 3)
#endif

/* SPI NAND 频率档位（u-boot cvsnfc_spi_ids.h 同名枚举，仅作打印/记录）。 */
#define SPI_NAND_FREQ_XTAL 0
#define SPI_NAND_FREQ_23MHz 1
#define SPI_NAND_FREQ_26MHz 2
#define SPI_NAND_FREQ_31MHz 3
#define SPI_NAND_FREQ_37MHz 4
#define SPI_NAND_FREQ_46MHz 5
#define SPI_NAND_FREQ_62MHz 6
#define SPI_NAND_FREQ_93MHz 7

/*
 * 精简支持表项。字段映射（u-boot struct cvsnfc_chip_info -> 本 struct）：
 *   name / id / id_len / chipsize / erasesize / pagesize / oobsize /
 *   badblock_pos / flags / ecc_* / sck_l / sck_h / max_freq / sample_param /
 *   xtal_switch —— 一一保留。
 * 裁掉：diesize、driver（vendor 回调指针）、read[]/write[]/erase[]（op 指针）。
 */
struct spi_nand_chip_info {
    const char *name;
    uint8_t id[SPI_NAND_MAX_ID_LEN];
    uint8_t id_len;
    uint64_t chipsize;  /* 字节 */
    uint32_t erasesize; /* block 大小，字节 */
    uint32_t pagesize;  /* 字节 */
    uint32_t oobsize;   /* 字节 */
    uint32_t badblock_pos;
    uint32_t flags;
    uint8_t ecc_en_feature_offset;
    uint8_t ecc_en_mask;
    uint8_t ecc_status_offset;
    uint8_t ecc_status_mask;
    uint8_t ecc_status_shift;
    uint8_t ecc_status_uncorr_val;
    uint8_t sck_l;
    uint8_t sck_h;
    uint16_t max_freq;
    uint32_t sample_param;
    uint8_t xtal_switch;
};

/* 支持表（以 id_len == 0 的哨兵项结尾），定义在 spi_nand_ids.c。 */
extern const struct spi_nand_chip_info spi_nand_flash_table[];

/*
 * 按 read_id 读到的 ID 字节匹配支持表。
 *   id      : read_id 得到的 ID 字节（至少 SPI_NAND_MAX_ID_LEN 字节）
 * 返回匹配到的表项指针；未匹配返回 NULL（调用方负责优雅打印，不崩）。
 */
const struct spi_nand_chip_info *spi_nand_ids_match(const uint8_t *id);

#endif /* BOOT_SPINAND */
#endif /* __SPI_NAND_IDS_H__ */
