#ifndef __CVI_EFUSE_H__
#define __CVI_EFUSE_H__

#include <stdint.h>

#include "common.h"
#include "cvi_type.h"
#include "rtos_types.h"

#define EFUSE_SW_INFO_SD_DL_SHIFT   22
#define EFUSE_SW_INFO_USB_DL_SHIFT  24
#define EFUSE_SW_INFO_UART_DL_SHIFT 26

/* shadow word index (cvi_efuse_read_word_from_shadow() takes the word, not the
 * byte address: sw_info = byte 0x2C, customer = byte 0x04) */
#define EFUSE_SW_INFO_WORD  0xb
#define EFUSE_CUSTOMER_WORD 0x1

/* customer word magic: which pin drives the download request.
 * AUX0 exists on CV181x (0x1E1E64) and CV180x (0x1E1564) only; other chips
 * fall back to the TOP USB_ID pin. Encoded as pinmux offset/func/gpio/bank,
 * see the EC_ fields in fsbl plat platform_device.c. */
#define EFUSE_CUSTOMER_MAGIC_CV181X_AUX0 0x1E1E64
#define EFUSE_CUSTOMER_MAGIC_CV180X_AUX0 0x1E1564
#define EFUSE_CUSTOMER_MAGIC_USB_ID      0x1

typedef enum {
    EFUSE_OK          = 0,
    EFUSE_ERROR       = -1,
    EFUSE_BUSY        = -2,
    EFUSE_TIMEOUT     = -3,
    EFUSE_UNSUPPORTED = -4
} efuse_error_t;
typedef struct {
    uint32_t start; /* Efuse start address */
    uint32_t end;   /* Efuse end address */
} cvi_efuse_info_t;

typedef struct {
    cvi_efuse_info_t info;
} cvi_efuse_t;
#define EFUSE_PARAM_CHK(para, err)                        \
    do {                                                  \
        if ((unsigned long)para == (unsigned long)NULL) { \
            return (err);                                 \
        }                                                 \
    } while (0)
#ifndef EFUSE_PARAM_CHK_NORETVAL
#define EFUSE_PARAM_CHK_NORETVAL(para)                    \
    do {                                                  \
        if ((unsigned long)para == (unsigned long)NULL) { \
            return;                                       \
        }                                                 \
    } while (0)
#endif
typedef enum {
    CVI_EFUSE_AREA_USER = 0,
    CVI_EFUSE_AREA_DEVICE_ID,
    CVI_EFUSE_AREA_HASH0_PUBLIC,
    CVI_EFUSE_AREA_LOADER_EK,
    CVI_EFUSE_AREA_DEVICE_EK,
    CVI_EFUSE_AREA_CHIP_SN,
    CVI_EFUSE_AREA_LAST
} CVI_EFUSE_AREA_E;

typedef enum {
    CVI_EFUSE_LOCK_HASH0_PUBLIC,
    CVI_EFUSE_LOCK_LOADER_EK,
    CVI_EFUSE_LOCK_DEVICE_EK,
    CVI_EFUSE_LOCK_LAST
} CVI_EFUSE_LOCK_E;

enum SD_USB_UART_DL_MODE_E {
    SD_USB_UART_DL_ENABLE,
    SD_USB_UART_DL_FASTBOOT,
    SD_USB_UART_DL_DISABLE,
};

efuse_error_t cvi_efuse_pwr_on();
efuse_error_t cvi_efuse_wait_idle();
efuse_error_t cvi_efuse_refresh_shadow();
efuse_error_t cvi_efuse_read_word_from_efuse(uint32_t addr, uint32_t *data);
efuse_error_t cvi_efuse_read_word_from_shadow(uint32_t addr, uint32_t *data);
efuse_error_t cvi_efuse_program_word(uint32_t addr, const uint32_t data);
efuse_error_t cvi_efuse_program_bit(uint32_t addr, const uint32_t bit);
efuse_error_t cvi_efuse_pwr_off();

efuse_error_t cvi_efuse_sd_dl_config(enum SD_USB_UART_DL_MODE_E mode);
efuse_error_t cvi_efuse_usb_dl_config(enum SD_USB_UART_DL_MODE_E mode);
efuse_error_t cvi_efuse_uart_dl_config(enum SD_USB_UART_DL_MODE_E mode);
uint32_t      cvi_efuse_get_sd_dl_config();
uint32_t      cvi_efuse_get_usb_dl_config();
uint32_t      cvi_efuse_get_uart_dl_config();
CVI_S32       cvi_efuse_get_chip_sn(void *data, uint32_t *size);

CVI_S32 CVI_EFUSE_GetSize(CVI_EFUSE_AREA_E area, CVI_U32 *size);
CVI_S32 CVI_EFUSE_Read(CVI_EFUSE_AREA_E area, CVI_U8 *buf, CVI_U32 buf_size);
CVI_S32 CVI_EFUSE_Write(CVI_EFUSE_AREA_E area, const CVI_U8 *buf, CVI_U32 buf_size);

CVI_S32 CVI_EFUSE_Lock(CVI_EFUSE_LOCK_E lock);
CVI_S32 CVI_EFUSE_IsLocked(CVI_EFUSE_LOCK_E lock);
CVI_S32 CVI_EFUSE_LockWrite(CVI_EFUSE_LOCK_E lock);
CVI_S32 CVI_EFUSE_IsWriteLocked(CVI_EFUSE_LOCK_E lock);

#define EFUSE_FASTBOOT_SD   (1U << 0)
#define EFUSE_FASTBOOT_USB  (1U << 1)
#define EFUSE_FASTBOOT_UART (1U << 2)
#define EFUSE_FASTBOOT_ALL  (EFUSE_FASTBOOT_SD | EFUSE_FASTBOOT_USB | EFUSE_FASTBOOT_UART)

/* Selected sources: 00 -> 01 (button download); 01/10 are skipped.
 * Revert: 01/10 -> 11 (normal download), 00/11 are skipped. After 11,
 * fastboot/disable cannot be programmed again. CUSTOMER is retained. */
CVI_S32 CVI_EFUSE_ConfigFastBoot(CVI_U32 sources, CVI_BOOL revert);
CVI_S32 CVI_EFUSE_EnableFastBoot(void);
CVI_S32 CVI_EFUSE_IsFastBootEnabled(void);
void    efuse_fastboot(void);
CVI_S32 CVI_EFUSE_BootFreqHigher(void);
CVI_S32 CVI_EFUSE_IsBootFreqHigher(void);
void    efuse_boot_freq_higher(void);

#endif /* __CVI_EFUSE_H__ */
