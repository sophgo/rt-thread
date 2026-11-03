#include "drv_efuse.h"

#include <stdio.h>
#include <string.h>

#include "errno.h"
#include "mmio.h"
#define ERROR(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#define VERBOSE(fmt, ...) printf(fmt, ##__VA_ARGS__)

/* TOP + GP_REG3: raw chip id (e.g. 0x1811C), same source u-boot reads.
 * Deliberately not CVI_SYS_GetChipId(): the enum it returns has no CV1815
 * entry and falls back to CV1835, which would pick the wrong CUSTOMER magic. */
#define EFUSE_CHIP_ID_REG 0x0300008C

static uint32_t cvi_efuse_customer_magic(void)
{
    uint32_t chip = mmio_read_32(EFUSE_CHIP_ID_REG);

    /* AUX0 exists only on CV181x (1810C/1811C/1812C, ver <= 3) and CV1815. */
    if (((chip & 0xFFF0F) == 0x1810C && ((chip >> 4) & 0xF) <= 3) || (chip & 0xFFFFF) == 0x1815)
        return EFUSE_CUSTOMER_MAGIC_CV181X_AUX0;

    /* CV180x 1800C/1800B (ver <= 3) use AUX0 too, but at a different pinmux
     * offset -- the CV181x magic would point at a non-existent pin. */
    if (((chip & 0xFFF0F) == 0x1800C || (chip & 0xFFF0F) == 0x1800B) && ((chip >> 4) & 0xF) <= 3)
        return EFUSE_CUSTOMER_MAGIC_CV180X_AUX0;

    return EFUSE_CUSTOMER_MAGIC_USB_ID;
}

/* Configure only the requested download sources. Unselected sources and
 * sources already in fastboot/disabled mode are left alone. Reverting adds
 * the second OTP bit (01/10 -> 11); it never clears a programmed bit. */
CVI_S32 CVI_EFUSE_ConfigFastBoot(CVI_U32 sources, CVI_BOOL revert)
{
    uint32_t sw_info  = 0;
    uint32_t pending  = 0;
    uint32_t customer = 0;
    uint32_t magic    = 0;
    uint32_t value    = 0;
    CVI_S32  ret      = CVI_FAILURE;
    int      i;

    if (sources & ~EFUSE_FASTBOOT_ALL) return CVI_FAILURE;
    if (!sources) return CVI_SUCCESS;
    if (cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &sw_info) != EFUSE_OK)
        return CVI_FAILURE;

    for (i = 0; i < 3; i++) {
        uint32_t shift = EFUSE_SW_INFO_SD_DL_SHIFT + i * 2;
        uint32_t mode  = (sw_info >> shift) & 0x3;

        if (!(sources & (1U << i))) continue;
        if (revert) {
            if (mode == 1 || mode == 2) pending |= (0x3U << shift) & ~sw_info;
        } else if (mode == 0) {
            pending |= 1U << shift;
        } else if (mode == 3) {
            printf("download source %d already reverted; cannot set fastboot again\n", i);
            return CVI_FAILURE;
        }
    }

    if (!pending) return CVI_SUCCESS;

    /* Only newly configured button download needs CUSTOMER. Revert must
     * work even if another provisioning scheme owns the CUSTOMER word. */
    if (!revert) {
        if (cvi_efuse_read_word_from_shadow(EFUSE_CUSTOMER_WORD, &customer) != EFUSE_OK)
            return CVI_FAILURE;
        magic = cvi_efuse_customer_magic();
        if ((customer | magic) != magic) {
            printf("customer word already set (0x%x), expected 0x%x\n", customer, magic);
            return CVI_FAILURE;
        }
    }

    if (cvi_efuse_pwr_on() != EFUSE_OK) return CVI_FAILURE;

    if (!revert && customer != magic) {
        if (cvi_efuse_program_word(EFUSE_CUSTOMER_WORD, magic) != EFUSE_OK ||
            cvi_efuse_read_word_from_shadow(EFUSE_CUSTOMER_WORD, &value) != EFUSE_OK ||
            value != magic)
            goto out;
    }

    if (cvi_efuse_program_word(EFUSE_SW_INFO_WORD, pending) != EFUSE_OK ||
        cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &value) != EFUSE_OK ||
        value != (sw_info | pending))
        goto out;

    ret = CVI_SUCCESS;
out:
    if (cvi_efuse_pwr_off() != EFUSE_OK) ret = CVI_FAILURE;
    return ret;
}

/* Preserve the explicit API's all-source behavior. Boot/menu configuration
 * is applied by efuse_fastboot(), rather than changing this API's contract. */
CVI_S32 CVI_EFUSE_EnableFastBoot(void)
{
    return CVI_EFUSE_ConfigFastBoot(EFUSE_FASTBOOT_ALL, CVI_FALSE);
}

CVI_S32 CVI_EFUSE_IsFastBootEnabled(void)
{
    uint32_t value    = 0;
    uint32_t fastboot = 0;
    int      i;

    if (cvi_efuse_read_word_from_shadow(EFUSE_SW_INFO_WORD, &value) != EFUSE_OK) return CVI_FAILURE;

    /* any of the three sources in fastboot mode is enough, same as u-boot */
    for (i = 0; i < 3; i++) {
        if (((value >> (EFUSE_SW_INFO_SD_DL_SHIFT + i * 2)) & 0x3) == 0x1) fastboot = 1;
    }
    if (!fastboot) return CVI_FAILURE;

    if (cvi_efuse_read_word_from_shadow(EFUSE_CUSTOMER_WORD, &value) != EFUSE_OK)
        return CVI_FAILURE;

    return (value == cvi_efuse_customer_magic()) ? CVI_SUCCESS : CVI_FAILURE;
}

/* Apply the explicitly enabled boot-time provisioning configuration. */
void efuse_fastboot(void)
{
    CVI_U32  sources = EFUSE_FASTBOOT_ALL;
    CVI_BOOL revert  = CVI_FALSE;

#ifdef BSP_USING_EFUSE_FASTBOOT
    sources = 0;
#ifdef BSP_EFUSE_FASTBOOT_SD
    sources |= EFUSE_FASTBOOT_SD;
#endif
#ifdef BSP_EFUSE_FASTBOOT_USB
    sources |= EFUSE_FASTBOOT_USB;
#endif
#ifdef BSP_EFUSE_FASTBOOT_UART
    sources |= EFUSE_FASTBOOT_UART;
#endif
#ifdef BSP_EFUSE_FASTBOOT_REVERT
    revert = CVI_TRUE;
#endif
#endif

    if (CVI_EFUSE_ConfigFastBoot(sources, revert) == CVI_SUCCESS)
        printf("efuse fastboot %s complete (sources=0x%x)\n", revert ? "revert" : "provision",
               sources);
    else
        printf("efuse fastboot %s failed (sources=0x%x)\n", revert ? "revert" : "provision",
               sources);
}

efuse_error_t cvi_efuse_disable_uart_dl()
{
    efuse_error_t ret = EFUSE_OK;

    ret = cvi_efuse_program_bit(0xb, 27);

    return ret;
}

efuse_error_t cvi_efuse_is_uart_dl_enable()
{
    efuse_error_t ret  = EFUSE_OK;
    uint32_t      data = 0;

    ret = cvi_efuse_read_word_from_shadow(0xb, &data);
    if (ret != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return EFUSE_ERROR;
    }

    if ((data & (1 << 27)) == 0) {
        return EFUSE_ERROR;
    }

    return ret;
}

efuse_error_t cvi_efuse_set_sd_dl_button()
{
    efuse_error_t ret  = EFUSE_OK;
    uint32_t      data = 0;

    ret = cvi_efuse_read_word_from_shadow(0xb, &data);
    if (ret != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return EFUSE_ERROR;
    }

    if ((data & (1 << 23)) != 0) {
        return EFUSE_ERROR;
    }

    ret = cvi_efuse_program_bit(0xb, 22);

    return ret;
}

efuse_error_t cvi_efuse_is_sd_dl_button()
{
    efuse_error_t ret  = EFUSE_OK;
    uint32_t      data = 0;

    ret = cvi_efuse_read_word_from_shadow(0xb, &data);
    if (ret != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return EFUSE_ERROR;
    }

    if ((data & (1 << 23)) != 0 || (data & (1 << 22)) == 0) {
        return EFUSE_ERROR;
    }

    return ret;
}

int cvi_efuse_get_chip_sn(void *data, uint32_t *size)
{
    EFUSE_PARAM_CHK(data, EFUSE_ERROR);
    EFUSE_PARAM_CHK(size, EFUSE_ERROR);

    efuse_error_t ret   = EFUSE_OK;
    uint32_t      uid_3 = 0xDEAFBEEF;
    uint32_t      uid_4 = 0xDEAFBEEF;

    ret = cvi_efuse_read_word_from_shadow(0x3, &uid_3);
    ret |= cvi_efuse_read_word_from_shadow(0x4, &uid_4);
    if (ret != EFUSE_OK) {
        printf("cvi_efuse_read_word_from_shadow failed\n");
        return EFUSE_ERROR;
    }

    ((uint32_t *)data)[0] = uid_3;
    ((uint32_t *)data)[1] = uid_4;
    *size                 = 8;

    return *size;
}

efuse_error_t cvi_efuse_init(cvi_efuse_t *efuse, int32_t idx)
{
    EFUSE_PARAM_CHK(efuse, EFUSE_ERROR);
    efuse_error_t ret = EFUSE_OK;
    if (cvi_efuse_pwr_on() != EFUSE_OK) {
        printf("cvi_efuse_pwr_on failed\n");
        return EFUSE_ERROR;
    }
    efuse->info.start = 0x0;
    efuse->info.end   = 0xFC;

    return ret;
}

void cvi_efuse_uninit(cvi_efuse_t *efuse)
{
    EFUSE_PARAM_CHK_NORETVAL(efuse);

    if (cvi_efuse_pwr_off() != EFUSE_OK) {
        printf("cvi_efuse_pwr_off failed\n");
        return;
    }

    memset(efuse, 0, sizeof(cvi_efuse_t));
}

int32_t cvi_efuse_read(cvi_efuse_t *efuse, uint32_t addr, void *data, uint32_t size)
{
    EFUSE_PARAM_CHK(efuse, EFUSE_ERROR);
    if ((size & 0x3) != 0) {
        printf("size is not 4 bytes aligned,size = %d\n", size);
        return EFUSE_ERROR;
    }
    if ((addr & 0x3) != 0) {
        printf("addr is not 4 bytes aligned,addr = %d\n", addr);
        return EFUSE_ERROR;
    }

    int i;

    if (addr < efuse->info.start || addr > efuse->info.end || (addr + size) > efuse->info.end) {
        printf("addr + size is out of range,addr = %d, size = %d, start = %d, end = %d\n", addr,
               size, efuse->info.start, efuse->info.end);
        return EFUSE_ERROR;
    }
    for (i = 0; i < (size >> 2); i++) {
        if (cvi_efuse_read_word_from_shadow((addr + i * 4) / 4, (uint32_t *)data + i) != EFUSE_OK) {
            printf("cvi_efuse_read_word_from_shadow failed\n");
            return (i << 2);
        }
    }
    return (i << 2);
}

int32_t cvi_efuse_program(cvi_efuse_t *efuse, uint32_t addr, const void *data, uint32_t size)
{
    EFUSE_PARAM_CHK(efuse, EFUSE_ERROR);
    if ((size & 0x3) != 0) {
        printf("size is not 4 bytes aligned,size = %d\n", size);
        return EFUSE_ERROR;
    }
    if ((addr & 0x3) != 0) {
        printf("addr is not 4 bytes aligned,addr = %d\n", addr);
        return EFUSE_ERROR;
    }

    int       i;
    uint32_t *p = (uint32_t *)data;

    if (addr < efuse->info.start || addr > efuse->info.end || (addr + size) > efuse->info.end) {
        printf("addr + size is out of range,addr = %d, size = %d, start = %d, end = %d\n", addr,
               size, efuse->info.start, efuse->info.end);
        return EFUSE_ERROR;
    }

    for (i = 0; i < (size >> 2); i++) {
        if (cvi_efuse_program_word((addr + i * 4) / 4, p[i]) != EFUSE_OK) {
            printf("cvi_efuse_program_word failed\n");
            return (i << 2);
        }
    }

    return (i << 2);
}

efuse_error_t cvi_efuse_get_info(cvi_efuse_t *efuse, cvi_efuse_info_t *info)
{
    EFUSE_PARAM_CHK(efuse, EFUSE_ERROR);
    EFUSE_PARAM_CHK(info, EFUSE_ERROR);

    info->start = efuse->info.start;
    info->end   = efuse->info.end;
    return EFUSE_OK;
}

/* Bonding0[11:10] = 0b10 selects the 750MHz CPU boot clock. */
#define EFUSE_BONDING0_WORD          0xa
#define EFUSE_BONDING0_BOOT_FREQ_BIT 11
#define EFUSE_BONDING0_BOOT_FREQ_LO  10

CVI_S32 CVI_EFUSE_IsBootFreqHigher(void)
{
    uint32_t value = 0;

    if (cvi_efuse_read_word_from_shadow(EFUSE_BONDING0_WORD, &value) != EFUSE_OK)
        return CVI_FAILURE;

    return ((value >> EFUSE_BONDING0_BOOT_FREQ_LO) & 0x3) == 0x2 ? CVI_SUCCESS : CVI_FAILURE;
}

CVI_S32 CVI_EFUSE_BootFreqHigher(void)
{
    uint32_t value = 0;
    CVI_S32  ret   = CVI_FAILURE;

    if (CVI_EFUSE_IsBootFreqHigher() == CVI_SUCCESS) return CVI_SUCCESS;

    if (cvi_efuse_read_word_from_shadow(EFUSE_BONDING0_WORD, &value) != EFUSE_OK)
        return CVI_FAILURE;
    /* bit10 set would make the field 0b11, which is undefined. */
    if (value & (1U << EFUSE_BONDING0_BOOT_FREQ_LO)) {
        printf("bonding0 bit%d already set (0x%x), refuse to program boot freq\n",
               EFUSE_BONDING0_BOOT_FREQ_LO, value);
        return CVI_FAILURE;
    }

    if (cvi_efuse_pwr_on() != EFUSE_OK) return CVI_FAILURE;

    if (cvi_efuse_program_bit(EFUSE_BONDING0_WORD, EFUSE_BONDING0_BOOT_FREQ_BIT) == EFUSE_OK &&
        CVI_EFUSE_IsBootFreqHigher() == CVI_SUCCESS)
        ret = CVI_SUCCESS;

    if (cvi_efuse_pwr_off() != EFUSE_OK) ret = CVI_FAILURE;
    return ret;
}

void efuse_boot_freq_higher(void)
{
    if (CVI_EFUSE_IsBootFreqHigher() == CVI_SUCCESS) {
        printf("efuse boot freq higher: already set\n");
        return;
    }

    if (CVI_EFUSE_BootFreqHigher() == CVI_SUCCESS)
        printf("efuse boot freq higher: programmed, takes effect from next power-on\n");
    else
        printf("efuse boot freq higher: failed\n");
}
