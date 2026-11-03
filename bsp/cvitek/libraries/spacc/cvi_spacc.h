#ifndef __CVI_SPACC_REGS_H__
#define __CVI_SPACC_REGS_H__

/*
 * CVITEK SPACC (CRYPTODMA) register layout, descriptor format and shared
 * algorithm/config types. The descriptor/engine operations are implemented
 * in libraries/spacc/cvi_spacc.c; the rt_device / hwcrypto adapter lives in
 * drivers/spacc/drv_spacc.h.
 */

#include <rtthread.h>

#define BIT(x) (1 << (x))

#define CRYPTODMA_BASE 0x2060000
// Register offset
#define CRYPTODMA_DMA_CTRL              (CRYPTODMA_BASE + 0x0)
#define CRYPTODMA_INT_MASK              (CRYPTODMA_BASE + 0x4)
#define CRYPTODMA_DES_BASE_L            (CRYPTODMA_BASE + 0x8)
#define CRYPTODMA_DES_BASE_H            (CRYPTODMA_BASE + 0xC)
#define CRYPTODMA_WR_INT                (CRYPTODMA_BASE + 0x10)
#define CRYPTODMA_DES_KEY               (CRYPTODMA_BASE + 0x100)
#define CRYPTODMA_DES_IV                (CRYPTODMA_BASE + 0x180)
#define CRYPTODMA_SHA_PARA              (CRYPTODMA_BASE + 0x1C0)
#define CRYPTODMA_SM3_PARA              (CRYPTODMA_BASE + 0x1E0)
#define CRYPTODMA_BASE64_CUSTOMIZE_CODE (CRYPTODMA_BASE + 0x200)

// DMA Descriptor
#define CRYPTODMA_CTRL            0x00
#define CRYPTODMA_CIPHER          0x01
#define CRYPTODMA_NEXT_PTR_ADDR_L 0x02
#define CRYPTODMA_NEXT_PTR_ADDR_H 0x03
#define CRYPTODMA_SRC_ADDR_L      0x04
#define CRYPTODMA_SRC_ADDR_H      0x05
#define CRYPTODMA_DST_ADDR_L      0x06
#define CRYPTODMA_DST_ADDR_H      0x07
#define CRYPTODMA_SRC_LEN_L       0x08
#define CRYPTODMA_SRC_LEN_H       0x09
#define BASE64_SIZE               0x0A
#define CRYPTODMA_KEY             0x0A
#define CRYPTODMA_IV              0x12

#define DES_USE_BYPASS         BIT(8)
#define DES_USE_AES            BIT(9)
#define DES_USE_DES            BIT(10)
#define DES_USE_SM4            BIT(11)
#define DES_USE_SHA            BIT(12)
#define DES_USE_BASE64         BIT(13)
#define DES_USE_SM3            BIT(14)
#define DES_USE_KEY0           BIT(16)
#define DES_USE_KEY1           BIT(17)
#define DES_USE_KEY2           BIT(18)
#define DES_USE_DESCRIPTOR_KEY BIT(19)
#define DES_USE_IV0            BIT(20)
#define DES_USE_IV1            BIT(21)
#define DES_USE_IV2            BIT(22)
#define DES_USE_DESCRIPTOR_IV  BIT(23)

// Cipher control for AES & SM4 & DES & TDES
#define CTL_ENCRYPT_ENABLE    BIT(0)
#define CTL_CBC_ENABLE        BIT(1)
#define CTL_CTR_ENABLE        BIT(2)
#define CTL_AES_KEY_MODE_128B BIT(5)
#define CTL_AES_KEY_MODE_192B BIT(4)
#define CTL_AES_KEY_MODE_256B BIT(3)
#define CTL_SM4_KEY_MODE_128B  // Placeholder, SM4 uses 128-bit by default, no register is needed
#define CTL_DES_KEY_MODE_64B_MASK BIT(3)
#define CTL_TDES_KEY_MODE_192B    BIT(3)
#define CTL_HASH_PARAM_ENABLE     BIT(0)
#define CTL_SHA256_ENABLE         BIT(1)

// DMA control
#define DMA_ENABLE          BIT(0)
#define DMA_DESCRIPTOR_MODE BIT(1)
#define DMA_READ_MAX_BURST  16
#define DMA_WRITE_MAX_BURST 16

/* ==== Types aligned with user-provided cvitek_spacc.h (Linux userspace) ====
 */
typedef enum {
    SPACC_ALGO_DES,
    SPACC_ALGO_TDES,
    SPACC_ALGO_AES,
    SPACC_ALGO_SM4,
    SPACC_ALGO_BASE64,
    SPACC_ALGO_SHA1,
    SPACC_ALGO_SHA256,
    SPACC_ALGO_SM3,
} SPACC_ALGO_E;

typedef enum {
    SPACC_ALGO_MODE_ECB,
    SPACC_ALGO_MODE_CBC,
    SPACC_ALGO_MODE_CTR,
    SPACC_ALGO_MODE_OFB,
} SPACC_ALGO_MODE_E;

typedef enum {
    SPACC_AES_KEY_SIZE_128BITS,
    SPACC_AES_KEY_SIZE_192BITS,
    SPACC_AES_KEY_SIZE_256BITS,
    SPACC_SM4_KEY_SIZE_128BITS,  // Unified naming style
    SPACC_DES_KEY_SIZE_64BITS,
    SPACC_TDES_KEY_SIZE_192BITS
} SPACC_KEY_SIZE_E;

typedef enum {
    SPACC_ACTION_ENCRYPT,
    SPACC_ACTION_DECRYPT,
} SPACC_ACTION_E;

typedef enum {
    SPACC_KEY_SOURCE_DESCRIPTOR,
    SPACC_KEY_SOURCE_OTP,
} SPACC_KEY_SOURCE_E;

/*
    source data address, destinate data address and data length.
    src/dst address must be 16 bytes aligned for all algorithm!
*/
#define SPACC_COMM_FIELDS \
    rt_uint64_t src;      \
    rt_uint64_t dst;      \
    rt_uint64_t len

/*
    hash config. input length must be 64 bytes aligned
    output buffer pointer (20/32 bytes)
*/
typedef struct spacc_hash_config {
    SPACC_COMM_FIELDS;
} spacc_hash_config_s;

typedef struct spacc_base64_config {
    SPACC_COMM_FIELDS;
    SPACC_ACTION_E action;  // SPACC_ACTION_ENCRYPT for encoding, SPACC_ACTION_DECRYPT for decoding
} spacc_base64_config_s;

typedef struct spacc_symm_cipher_config {
    SPACC_COMM_FIELDS;
    SPACC_ACTION_E     action;
    rt_uint64_t        key;
    rt_uint64_t        iv;
    SPACC_ALGO_MODE_E  mode;
    SPACC_KEY_SIZE_E   key_mode;
    SPACC_KEY_SOURCE_E otp;
} spacc_symm_cipher_config_s;

typedef spacc_symm_cipher_config_s spacc_des_config_s;
typedef spacc_symm_cipher_config_s spacc_tdes_config_s;
typedef spacc_symm_cipher_config_s spacc_aes_config_s;
typedef spacc_symm_cipher_config_s spacc_sm4_config_s;

#undef SPACC_COMM_FIELDS

/* Descriptor/engine level operations (implemented in cvi_spacc.c).
 * result_size semantics follow the old in-driver behaviour: bytes produced
 * for cipher/base64, 20/32/8*word bytes for SHA-1/SHA-256/SM3. */
int  cvi_spacc_symm_cipher_hw(SPACC_ALGO_E algo, spacc_symm_cipher_config_s *cfg,
                              rt_uint32_t *result_size);
int  cvi_spacc_base64_hw(const spacc_base64_config_s *cfg, rt_uint32_t *result_size);
int  cvi_spacc_hash_hw(SPACC_ALGO_E algo, const spacc_hash_config_s *cfg,
                       rt_uint32_t *result_size);
void dbg_reg_dump(void);

#endif /* __CVI_SPACC_REGS_H__ */
