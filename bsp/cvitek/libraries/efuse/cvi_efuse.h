#ifndef __CVI_EFUSE_REGS_H__
#define __CVI_EFUSE_REGS_H__

/*
 * CVITEK efuse controller register definitions and word-level primitive
 * declarations. Implemented in libraries/efuse/cvi_efuse.c; the UAPI types
 * and higher-level entry points live in drivers/efuse/drv_efuse.h.
 */

#define EFUSE_BASE (0x03050000)

#define EFUSE_SHADOW_REG (EFUSE_BASE + 0x100)
#define EFUSE_SIZE       0x100

#define EFUSE_MODE    (0x0)
#define EFUSE_ADR     (0x4)
#define EFUSE_DIR_CMD (0x8)
#define EFUSE_RD_DATA (0xC)
#define EFUSE_STATUS  (0x10)
#define EFUSE_ONE_WAY (0x14)

#define EFUSE_BIT_AREAD   BIT(0)
#define EFUSE_BIT_MREAD   BIT(1)
#define EFUSE_BIT_PRG     BIT(2)
#define EFUSE_BIT_PWR_DN  BIT(3)
#define EFUSE_BIT_CMD     BIT(4)
#define EFUSE_BIT_BUSY    BIT(0)
#define EFUSE_CMD_REFRESH (0x30)

#define EFUSE_FTSN0   0x100
#define EFUSE_FTSN1   0x104
#define EFUSE_SW_INFO 0x12c
enum EFUSE_READ_TYPE { EFUSE_AREAD, EFUSE_MREAD };

#endif /* __CVI_EFUSE_REGS_H__ */
