/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved.
 */

#include "spiflash.h"

#include <drivers/mtd_nor.h>

#include "cvitek_spi_nor.h"
#include "cvitek_spi_nand.h"
#include "pinctrl.h"

// Global SPI flash device structure
static struct cvi_spif g_cvi_spif = {0};

// Maximum read size for SPI flash operations (32KB)
#define SPI_FLASH_READ_SIZE_MAX (0x8000UL)

/**
 * @brief Configure pin multiplexing for SPI NOR flash pins
 */
static void rt_hw_spiflash_pinmux_config()
{
#if defined(__CV180X__)
    PINMUX_CONFIG(SPINOR_HOLD_X, SPINOR_HOLD_X);
    PINMUX_CONFIG(SPINOR_SCK, SPINOR_SCK);
    PINMUX_CONFIG(SPINOR_MOSI, SPINOR_MOSI);
    PINMUX_CONFIG(SPINOR_WP_X, SPINOR_WP_X);
    PINMUX_CONFIG(SPINOR_MISO, SPINOR_MISO);
    PINMUX_CONFIG(SPINOR_CS_X, SPINOR_CS_X);
#elif defined(__CV181X__)
    PINMUX_CONFIG(EMMC_DAT2, SPINOR_HOLD_X);
    PINMUX_CONFIG(EMMC_CLK, SPINOR_SCK);
    PINMUX_CONFIG(EMMC_DAT0, SPINOR_MOSI);
    PINMUX_CONFIG(EMMC_DAT3, SPINOR_WP_X);
    PINMUX_CONFIG(EMMC_CMD, SPINOR_MISO);
    PINMUX_CONFIG(EMMC_DAT1, SPINOR_CS_X);
#endif
}

/**
 * @brief Initialize SPI flash hardware
 *
 * @return Pointer to initialized SPI flash structure, NULL if failed
 */
struct cvi_spif *hal_spiflash_init(void)
{
    struct cvi_spif *spif = NULL;
    uint32_t         reg;

    spif = &g_cvi_spif;

    memset(spif, 0x0, sizeof(struct cvi_spif));

    if (spif->io_base != NULL) return 0;

    if (spif->io_base == NULL) spif->io_base = (void *)SPI_NOR_REGBASE;

    // Enable clock gate for SPI NOR flash
    reg = readl((void *)0x03002004);
    writel(reg | 0x1, (void *)0x03002004);

    // Configure SPI clock to 75MHz
    cvi_spif_clk_setup(spif, 1);
    cvi_spif_setup_flash(spif);

    // Configure SPI pins
    rt_hw_spiflash_pinmux_config();

    // Scan and initialize flash device
    if (spi_nor_rescan(&spif->nor)) {
        printf("scan flash failed!\n");
        return NULL;
    }

    return spif;
}

/**
 * @brief Read data from SPI flash in chunks
 *
 * @param device MTD device structure
 * @param offset Starting address to read from
 * @param data Buffer to store read data
 * @param size Number of bytes to read
 *
 * @return Number of bytes read, negative on error
 */
int32_t hal_spiflash_read(struct rt_mtd_nor_device *device, uint32_t offset, void *data,
                          uint32_t size)
{
    struct spi_nor *nor = &g_cvi_spif.nor;

    int32_t  len = 0;
    uint32_t read_size;
    int      ret;

    if (nor->read == NULL) {
        printf("there is no read operation!\n");
        return -1;
    }

    // Read data in chunks
    while (size) {
        if (size >= SPI_FLASH_READ_SIZE_MAX)
            read_size = SPI_FLASH_READ_SIZE_MAX;
        else
            read_size = size & (SPI_FLASH_READ_SIZE_MAX - 1);

        ret = nor->read(nor, offset, read_size, data);
        if (ret < 0) {
            printf("read data failed, ret:%d\n", ret);
            return -1;
        }
        offset += read_size;
        data += read_size;
        len += read_size;
        size -= read_size;
    }
    return len;
}

/**
 * @brief Write data to SPI flash
 *
 * @param device MTD device structure
 * @param offset Starting address to write to
 * @param data Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written, negative on error
 */
int32_t hal_spiflash_write(struct rt_mtd_nor_device *device, uint32_t offset, const void *data,
                           uint32_t size)
{
    struct spi_nor *nor = &g_cvi_spif.nor;
    return spi_nor_write(nor, offset, data, size);
}

/**
 * @brief Erase the specified range of flash memory in SPI flash
 *
 * @param device MTD device structure
 * @param offset Starting address to erase
 * @param size Number of bytes to erase
 *
 * @return 0 on success, negative on error
 */
int32_t hal_spiflash_erase(struct rt_mtd_nor_device *device, uint32_t offset, uint32_t size)
{
    struct spi_nor *nor = &g_cvi_spif.nor;

    if (nor->erase == NULL) {
        printf("there is no erase operation!\n");
        return -1;
    }

    return nor->erase(nor, offset, size);
}

/**
 * @brief Get flash device information
 *
 * @param flash_info Structure to store flash information
 *
 * @return 0 on success, negative on error
 */
int32_t hal_spiflash_get_flash_info(hal_spiflash_info_t *flash_info)
{
    struct spi_nor    *nor  = &g_cvi_spif.nor;
    struct flash_info *info = nor->info;
    uint8_t           *id   = info->id;
    int                i;

    memset(flash_info, 0, sizeof(hal_spiflash_info_t));
    flash_info->flash_name = info->name;

    // Convert flash ID bytes to single value
    for (i = 0; i < info->id_len; i++) flash_info->flash_id |= id[i] << (info->id_len - 1 - i);

    flash_info->flash_size  = info->sector_size * info->n_sectors;
    flash_info->page_size   = info->page_size;
    flash_info->sector_size = nor->erase_size;
    return 0;
}

/**
 * @brief Read register from SPI flash
 *
 * @param cmd_code Command code for register read
 * @param data Buffer to store read data
 * @param size Number of bytes to read
 *
 * @return Number of bytes read, negative on error
 */
int32_t hal_spiflash_read_reg(uint8_t cmd_code, uint8_t *data, uint32_t size)
{
    struct spi_nor *nor = &g_cvi_spif.nor;

    if (nor->read_reg == NULL) {
        printf("there is no read register operation!\n");
        return -1;
    }

    return nor->read_reg(nor, cmd_code, data, size);
}

/**
 * @brief Write register to SPI flash
 *
 * @param cmd_code Command code for register write
 * @param data Data to write
 * @param size Number of bytes to write
 *
 * @return Number of bytes written, negative on error
 */
int32_t hal_spiflash_write_reg(uint8_t cmd_code, uint8_t *data, uint32_t size)
{
    struct spi_nor *nor = &g_cvi_spif.nor;

    if (nor->write_reg == NULL) {
        printf("there is no write register operation!\n");
        return -1;
    }

    return nor->write_reg(nor, cmd_code, data, size);
}

static struct rt_mtd_nor_device mtd_device;

struct rt_mtd_nor_driver_ops cvi_mtd_flash_ops = {
    RT_NULL,
    hal_spiflash_read,
    hal_spiflash_write,
    hal_spiflash_erase,
};

/**
 * @brief Initialize flash device and register with RT-Thread
 *
 * @return RT_EOK on success
 */
int rt_flash_init(void)
{
#if defined(BOOT_SPINAND)
    // SPI NAND 板：走 NAND 探测/注册路径，不注册 NOR。
    return rt_flash_init_nand();
#else
    rt_err_t result = RT_EOK;

    struct cvi_spif *spif = hal_spiflash_init();

    // NULL 防护：扫不到 NOR 器件时优雅返回，不再解引用 spif->nor.*（防 mcause 0x5）。
    if (!spif) {
        rt_kprintf("rt_flash_init: no spi nor flash found, skip mtd register\n");
        return -RT_ERROR;
    }

    // Set MTD device parameters
    mtd_device.block_size  = spif->nor.erase_size;
    mtd_device.block_start = 0;
    mtd_device.block_end   = spif->nor.info->sector_size * spif->nor.info->n_sectors;
    mtd_device.ops         = &cvi_mtd_flash_ops;

    // Register MTD device
    rt_mtd_nor_register_device("norflash0", &mtd_device);

    return result;
#endif
}
INIT_DEVICE_EXPORT(rt_flash_init);
