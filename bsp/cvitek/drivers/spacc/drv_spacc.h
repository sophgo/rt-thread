#ifndef _CVITEK_SPACC_REGS_H_
#define _CVITEK_SPACC_REGS_H_

/* SPACC register layout, descriptor format and algorithm/config types are
 * provided by the IP library header. This adapter header keeps the rt_device
 * / hwcrypto facing surface for module consumers. */
#include "cvi_spacc.h"

#ifdef RT_USING_HWCRYPTO
#include <drivers/crypto.h>
#endif
#include <rtdevice.h>
#include <rtthread.h>

/* IOCTLs must match drv_spacc.c */
#define IOCTL_SPACC_CREATE_MEMPOOL   (0x1000)
#define IOCTL_SPACC_GET_MEMPOOL_SIZE (0x1001)
#define IOCTL_SPACC_DES_ACTION       (0x1002)
#define IOCTL_SPACC_TDES_ACTION      (0x1003)
#define IOCTL_SPACC_AES_ACTION       (0x1004)
#define IOCTL_SPACC_SM4_ACTION       (0x1005)
#define IOCTL_SPACC_BASE64_ACTION    (0x1006)
#define IOCTL_SPACC_SHA1_ACTION      (0x1007)
#define IOCTL_SPACC_SHA256_ACTION    (0x1008)
#define IOCTL_SPACC_SM3_ACTION       (0x1009)

typedef struct spacc_dev {
    struct rt_device dev;
    /* MMIO base/irq are reserved for future HW hookup */
    int         irqno;
    rt_uint32_t result_size;
} spacc_dev_t;

static spacc_dev_t g_spacc;

#endif
