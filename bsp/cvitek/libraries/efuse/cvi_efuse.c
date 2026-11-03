/*
 * CVITEK efuse controller register primitives (read/program/refresh).
 * Register layout in cvi_efuse.h; UAPI types live in drv_efuse.h.
 */
#include "cvi_efuse.h"

#include <stdio.h>

#include "drv_efuse.h"
#include "mmio.h"

#define ERROR(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#define VERBOSE(fmt, ...) printf(fmt, ##__VA_ARGS__)

efuse_error_t cvi_efuse_wait_idle()
{
    uint32_t      status;
    uint32_t      time_count = 0;
    efuse_error_t ret        = EFUSE_OK;
    do {
        status = mmio_read_32(EFUSE_BASE + EFUSE_STATUS);
        time_count++;

        if (time_count > 0x1000) {
            printf("wait idle timeout\n");
            return EFUSE_ERROR;
        }
    } while ((status & 0x1) != 0);

    return ret;
}

efuse_error_t cvi_efuse_pwr_on()
{
    efuse_error_t ret = EFUSE_OK;

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }
    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x10);
    return ret;
}

efuse_error_t cvi_efuse_refresh_shadow()
{
    efuse_error_t ret = EFUSE_OK;

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x30);

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    return ret;
}

// read one line (4 bytes) from efuse
efuse_error_t cvi_efuse_read_word_from_efuse(uint32_t addr, uint32_t *data)
{
    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    mmio_write_32(EFUSE_BASE + EFUSE_ADR, (addr << 1));
    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x12);

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    *data = mmio_read_32(EFUSE_BASE + EFUSE_RD_DATA);
    return EFUSE_OK;
}

// read one line (4 bytes) from shadow
efuse_error_t cvi_efuse_read_word_from_shadow(uint32_t addr, uint32_t *data)
{
    *data = mmio_read_32(EFUSE_BASE + EFUSE_FTSN0 + (addr << 2));
    return EFUSE_OK;
}

// program 4 bytes
efuse_error_t cvi_efuse_program_word(uint32_t addr, const uint32_t data)
{
    int      i;
    uint32_t exist_data = 0;

    cvi_efuse_read_word_from_shadow(addr, &exist_data);
    exist_data = (~exist_data) & data;

    if (exist_data == 0) {
        printf("no data need write\n");
        return EFUSE_OK;
    }

    for (i = 0; i < 32; i++) {
        int bit = exist_data & (1 << i);

        if (bit) {
            if (cvi_efuse_wait_idle() != EFUSE_OK) {
                printf("cvi_efuse_wait_idle failed\n");
                return EFUSE_ERROR;
            }

            uint16_t w_addr = (i << 7) | ((addr & 0x3F) << 1);
            mmio_write_32(EFUSE_BASE + EFUSE_ADR, (w_addr & 0xFFF));
            mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x14);

            if (cvi_efuse_wait_idle() != EFUSE_OK) {
                printf("cvi_efuse_wait_idle failed\n");
                return EFUSE_ERROR;
            }

            w_addr |= 0x1;
            mmio_write_32(EFUSE_BASE + EFUSE_ADR, (w_addr & 0xFFF));
            mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x14);
        }
    }

    cvi_efuse_refresh_shadow();

    return EFUSE_OK;
}

// program 1 bit
efuse_error_t cvi_efuse_program_bit(uint32_t addr, const uint32_t bit)
{
    uint32_t exist_data = 0;

    cvi_efuse_read_word_from_shadow(addr, &exist_data);

    if (exist_data & (1 << bit)) {
        // printf("no bit need write\n");
        return EFUSE_OK;
    }

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    uint16_t w_addr = (bit << 7) | ((addr & 0x3F) << 1);
    mmio_write_32(EFUSE_BASE + EFUSE_ADR, (w_addr & 0xFFF));
    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x14);

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    w_addr |= 0x1;
    mmio_write_32(EFUSE_BASE + EFUSE_ADR, (w_addr & 0xFFF));
    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x14);

    cvi_efuse_refresh_shadow();

    return EFUSE_OK;
}

efuse_error_t cvi_efuse_pwr_off()
{
    efuse_error_t ret = EFUSE_OK;

    if (cvi_efuse_wait_idle() != EFUSE_OK) {
        printf("cvi_efuse_wait_idle failed\n");
        return EFUSE_ERROR;
    }

    mmio_write_32(EFUSE_BASE + EFUSE_MODE, 0x18);

    return ret;
}

/*
 * SD/USB/UART download-mode config.
 *
 * SW_INFO carries one 2-bit field per source; the field lives in a one-way
 * word, so a value can only ever gain bits:
 *   0b00/0b11 -> download enabled, 0b01 -> fastboot (button/pin driven),
 *   0b10      -> disabled.
 * Program a bit only when the current value permits it; anything already in an
 * incompatible state is left untouched and reported as an error rather than
 * being forced (the hardware cannot clear bits back).
 */
static efuse_error_t cvi_efuse_dl_config(uint32_t shift, enum SD_USB_UART_DL_MODE_E mode)
{
    uint32_t      data = 0;
    efuse_error_t ret  = EFUSE_OK;

    ret = cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &data);
    if (ret != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return ret;
    }

    data = (data >> shift) & 0x3;

    switch (mode) {
    case SD_USB_UART_DL_ENABLE:
        /* Restore normal download by setting the other OTP bit: 01/10 -> 11.
         * No bit is cleared; 00/11 already allow normal download. */
        if (data == 0x1)
            ret = cvi_efuse_program_bit(EFUSE_SW_INFO_WORD, shift + 1);
        else if (data == 0x2)
            ret = cvi_efuse_program_bit(EFUSE_SW_INFO_WORD, shift);
        break;
    case SD_USB_UART_DL_FASTBOOT:
        if (data == 0x0)
            ret = cvi_efuse_program_bit(EFUSE_SW_INFO_WORD, shift);
        else if (data != 0x1)
            ret = EFUSE_ERROR;
        break;
    case SD_USB_UART_DL_DISABLE:
        if (data == 0x0)
            ret = cvi_efuse_program_bit(EFUSE_SW_INFO_WORD, shift + 1);
        else if (data != 0x2)
            ret = EFUSE_ERROR;
        break;
    default:
        ret = EFUSE_ERROR;
        break;
    }

    return ret;
}

static uint32_t cvi_efuse_get_dl_config(uint32_t shift)
{
    uint32_t data = 0;

    if (cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &data) != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return 0;
    }

    return (data >> shift) & 0x3;
}

efuse_error_t cvi_efuse_sd_dl_config(enum SD_USB_UART_DL_MODE_E mode)
{
    return cvi_efuse_dl_config(EFUSE_SW_INFO_SD_DL_SHIFT, mode);
}

efuse_error_t cvi_efuse_usb_dl_config(enum SD_USB_UART_DL_MODE_E mode)
{
    return cvi_efuse_dl_config(EFUSE_SW_INFO_USB_DL_SHIFT, mode);
}

efuse_error_t cvi_efuse_uart_dl_config(enum SD_USB_UART_DL_MODE_E mode)
{
    return cvi_efuse_dl_config(EFUSE_SW_INFO_UART_DL_SHIFT, mode);
}

uint32_t cvi_efuse_get_sd_dl_config(void)
{
    return cvi_efuse_get_dl_config(EFUSE_SW_INFO_SD_DL_SHIFT);
}

uint32_t cvi_efuse_get_usb_dl_config(void)
{
    return cvi_efuse_get_dl_config(EFUSE_SW_INFO_USB_DL_SHIFT);
}

uint32_t cvi_efuse_get_uart_dl_config(void)
{
    return cvi_efuse_get_dl_config(EFUSE_SW_INFO_UART_DL_SHIFT);
}
