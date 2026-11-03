/*
 * CVITEK SPACC (CRYPTODMA) descriptor build + engine trigger operations.
 * Register layout and shared types in cvi_spacc.h.
 */
#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/byteorder.h>
#include <mmio.h>

#include "cache.h"

#include "cvi_spacc.h"

static rt_align(64) rt_uint32_t dma_desc[22];
static const rt_uint32_t sha256_init_value[8] = {
    rt_cpu_to_be32(0x6A09E667), rt_cpu_to_be32(0xBB67AE85), rt_cpu_to_be32(0x3C6EF372),
    rt_cpu_to_be32(0xA54FF53A), rt_cpu_to_be32(0x510E527F), rt_cpu_to_be32(0x9B05688C),
    rt_cpu_to_be32(0x1F83D9AB), rt_cpu_to_be32(0x5BE0CD19),
};
static const rt_uint32_t sm3_init_value[8] = {
    rt_cpu_to_be32(0x7380166f), rt_cpu_to_be32(0x4914b2b9), rt_cpu_to_be32(0x172442d7),
    rt_cpu_to_be32(0xda8a0600), rt_cpu_to_be32(0xa96f30bc), rt_cpu_to_be32(0x163138aa),
    rt_cpu_to_be32(0xe38dee4d), rt_cpu_to_be32(0xb0fb0e4e),
};
static const rt_uint32_t sha1_init_value[5] = {
    rt_cpu_to_be32(0x67452301), rt_cpu_to_be32(0xEFCDAB89), rt_cpu_to_be32(0x98BADCFE),
    rt_cpu_to_be32(0x10325476), rt_cpu_to_be32(0xC3D2E1F0),
};

void dbg_reg_dump()
{
#if 0
    static char *reg_names[] = {
        "CTRL",       "CIPHER",     "NEXT_PTR_ADDR_L", "NEXT_PTR_ADDR_H", "SRC_ADDR_L",
        "SRC_ADDR_H", "DST_ADDR_L", "DST_ADDR_H",      "SRC_LEN_L",       "SRC_LEN_H",
    };
    for (int i = 0; i < 10; i++) {
        rt_kprintf("CRYPTODMA_BASE+0x%08x: 0x%08x\n", i * 4, mmio_read_32(CRYPTODMA_BASE + i * 4));
    }
    rt_kprintf("desc: %p\n", dma_desc);
    for (int i = 0; i < 10; i++) {
        rt_kprintf("SPACC[desc[%d]/%s]: 0x%08x\n", i, reg_names[i], dma_desc[i]);
    }
    for (int i = 10; i < 22; i++) {
        rt_kprintf("SPACC[desc[%d]]: 0x%08x\n", i, dma_desc[i]);
    }
#endif
}

static inline int trigger_cryptodma_engine_and_wait_finish(void)
{
    uint32_t status = 0;
    // Necessary to clear interrupt
    mmio_write_32(CRYPTODMA_WR_INT, 0x7);
    // Set cryptodma control
    mmio_write_32(CRYPTODMA_INT_MASK, 0x0);
    /* flush/invalidate handled at descriptor/data setup sites */
    // Trigger cryptodma engine
    mmio_write_32(CRYPTODMA_DMA_CTRL, DMA_WRITE_MAX_BURST << 24 | DMA_READ_MAX_BURST << 16 |
                                          DMA_DESCRIPTOR_MODE | DMA_ENABLE);
    uint32_t timeout = 0;

    do {
        status = mmio_read_32(CRYPTODMA_WR_INT);
        timeout++;
        if (timeout > 3000000) {
            rt_kprintf("timeout: %d\n", timeout);
            return -RT_ETIMEOUT;
        }
    } while (status == 0);
    return RT_EOK;
}

/* ==== Helper functions ====  */
static inline void flush_addr_cache(void *addr, int size)
{
    rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH | RT_HW_CACHE_INVALIDATE, addr, size);
}
// Necessary to flush/invalidate
static inline void set_desc_addr(void)
{
    mmio_write_32(CRYPTODMA_DES_BASE_L, (uint32_t)((uint64_t)dma_desc & 0xFFFFFFFF));
    mmio_write_32(CRYPTODMA_DES_BASE_H, (uint32_t)((uint64_t)dma_desc >> 32));
    flush_addr_cache((void *)dma_desc, (int)sizeof(dma_desc));
}

static inline void desc_set_src_addr_and_len(uintptr_t src, uint64_t src_len)
{
    dma_desc[CRYPTODMA_SRC_LEN_L]  = (uint32_t)(src_len & 0xFFFFFFFF);
    dma_desc[CRYPTODMA_SRC_LEN_H]  = (uint32_t)(src_len >> 32);
    dma_desc[CRYPTODMA_SRC_ADDR_L] = (uint32_t)((uint64_t)src & 0xFFFFFFFF);
    dma_desc[CRYPTODMA_SRC_ADDR_H] = (uint32_t)((uint64_t)src >> 32);
    flush_addr_cache((void *)src, (int)src_len);
}

static inline void desc_set_dst_addr(uintptr_t dst, uint64_t dst_len)
{
    dma_desc[CRYPTODMA_DST_ADDR_L] = (uint32_t)((uint64_t)dst & 0xFFFFFFFF);
    dma_desc[CRYPTODMA_DST_ADDR_H] = (uint32_t)((uint64_t)dst >> 32);
    flush_addr_cache((void *)dst, (int)dst_len);
}

/* ==== Symmetric Cipher descriptor helper functions ==== */
static inline void desc_set_key_mode(SPACC_KEY_SIZE_E key_size_mode, const void *key)
{
    rt_base_t key_size;
    switch (key_size_mode) {
    // DES & TDES
    case SPACC_DES_KEY_SIZE_64BITS:
        key_size = 8;
        dma_desc[CRYPTODMA_CIPHER] &= ~CTL_DES_KEY_MODE_64B_MASK;
        break;
    case SPACC_TDES_KEY_SIZE_192BITS:
        key_size = 24;
        dma_desc[CRYPTODMA_CIPHER] |= CTL_TDES_KEY_MODE_192B;
        break;
    // AES & SM4
    case SPACC_AES_KEY_SIZE_256BITS:
        key_size = 32;
        dma_desc[CRYPTODMA_CIPHER] |= CTL_AES_KEY_MODE_256B;
        break;
    case SPACC_AES_KEY_SIZE_192BITS:
        key_size = 24;
        dma_desc[CRYPTODMA_CIPHER] |= CTL_AES_KEY_MODE_192B;
        break;
    case SPACC_AES_KEY_SIZE_128BITS:  // AES 128 bit & SM4
    case SPACC_SM4_KEY_SIZE_128BITS:
    default:
        key_size = 16;
        dma_desc[CRYPTODMA_CIPHER] |= CTL_AES_KEY_MODE_128B;
    }
    rt_memcpy(&dma_desc[CRYPTODMA_KEY], key, key_size);
}

static inline void desc_set_action(SPACC_ACTION_E act)
{
    if (act == SPACC_ACTION_ENCRYPT)
        dma_desc[CRYPTODMA_CIPHER] |= CTL_ENCRYPT_ENABLE;
    else {
        dma_desc[CRYPTODMA_CIPHER] &= ~CTL_ENCRYPT_ENABLE;
    }
}
int cvi_spacc_symm_cipher_hw(SPACC_ALGO_E algo, spacc_symm_cipher_config_s *cfg,
                             rt_uint32_t *result_size)
{
    // Check input parameters
    if (cfg == RT_NULL) return -RT_EINVAL;
    if (cfg->src == 0 || cfg->len == 0) return -RT_EINVAL;
    if (cfg->dst == 0) { /* allow in-place explicitly */
        rt_kprintf("cfg->dst: 0x%08x: Allow in-place explicitly\n", cfg->dst);
        cfg->dst = cfg->src;
    }
    if ((cfg->src & 0x0Fu) || (cfg->dst & 0x0Fu)) {
        rt_kprintf("cfg->src: 0x%08x, cfg->dst: 0x%08x: Must be 16B aligned\n", cfg->src, cfg->dst);
        return -RT_EINVAL;
    }
    if (cfg->key == 0) {
        rt_kprintf("Key is required for symmetric cipher.\n");
        return -RT_EINVAL;
    }
    if ((cfg->mode == SPACC_ALGO_MODE_CBC || cfg->mode == SPACC_ALGO_MODE_CTR) && cfg->iv == 0) {
        rt_kprintf("iv is required for CBC/CTR mode.\n");
        return -RT_EINVAL;
    }
    int algo_flag, iv_size, len_align_mask;
    switch (algo) {
    case SPACC_ALGO_DES:
        algo_flag      = DES_USE_DES;
        iv_size        = 8;
        len_align_mask = 0x07u;
        cfg->key_mode  = SPACC_DES_KEY_SIZE_64BITS;
        break;
    case SPACC_ALGO_TDES:
        algo_flag      = DES_USE_DES;
        iv_size        = 8;
        len_align_mask = 0x07u;
        cfg->key_mode  = SPACC_TDES_KEY_SIZE_192BITS;
        break;
    case SPACC_ALGO_AES:
        algo_flag      = DES_USE_AES;
        iv_size        = 16;
        len_align_mask = 0x0Fu;
        break;
    case SPACC_ALGO_SM4:
        algo_flag      = DES_USE_SM4;
        iv_size        = 16;
        len_align_mask = 0x0Fu;
        break;
    default:
        rt_kprintf("Unsupported symm cipher algo code: %d\n", algo);
        return -RT_EINVAL;
    }
    // Check length
    if ((cfg->len & len_align_mask)) {
        rt_kprintf("cfg->len: 0x%08x: Must be mutiple of 8/16 bytes for (DES,TDES/AES,SM4)\n",
                   cfg->len);
        return -RT_EINVAL;
    }

    rt_memset(dma_desc, 0, sizeof(dma_desc));
    dma_desc[CRYPTODMA_CTRL] = DES_USE_DESCRIPTOR_KEY | algo_flag | 0xF | DES_USE_DESCRIPTOR_IV;

    // Set mode and IV
    switch (cfg->mode) {
    case SPACC_ALGO_MODE_CBC:
        dma_desc[CRYPTODMA_CIPHER] |= CTL_CBC_ENABLE;
        rt_memcpy(&dma_desc[CRYPTODMA_IV], (const void *)cfg->iv, iv_size);
        break;
    case SPACC_ALGO_MODE_CTR:
        dma_desc[CRYPTODMA_CIPHER] |= CTL_CTR_ENABLE;
        rt_memcpy(&dma_desc[CRYPTODMA_IV], (const void *)cfg->iv, iv_size);
        break;
    case SPACC_ALGO_MODE_ECB:
    case SPACC_ALGO_MODE_OFB:
    default:
        break;
    }
    desc_set_key_mode(cfg->key_mode, (const void *)cfg->key);
    desc_set_action(cfg->action);

    desc_set_src_addr_and_len((uintptr_t)cfg->src, cfg->len);
    desc_set_dst_addr((uintptr_t)cfg->dst, cfg->len);
    set_desc_addr();

    int ret = trigger_cryptodma_engine_and_wait_finish();
    flush_addr_cache((void *)(uintptr_t)cfg->dst, (int)cfg->len);
    dbg_reg_dump();
    if (ret != RT_EOK) {
        rt_kprintf("trigger_cryptodma_engine_and_wait_finish failed: %d\n", ret);
        return ret;
    }
    *result_size = (rt_uint32_t)cfg->len;
    return RT_EOK;
}

int cvi_spacc_base64_hw(const spacc_base64_config_s *cfg, rt_uint32_t *result_size)
{
    if (cfg->src == 0 || cfg->len == 0) return -RT_EINVAL;
    if (cfg->dst == 0) return -RT_EINVAL;
    if ((cfg->src & 0x0Fu) || (cfg->dst & 0x0Fu)) {
        rt_kprintf("cfg->src: 0x%08x, cfg->dst: 0x%08x: Must be 16B aligned\n", cfg->src, cfg->dst);
        return -RT_EINVAL;
    }
    rt_memset(dma_desc, 0, sizeof(dma_desc));
    dma_desc[CRYPTODMA_CTRL] = DES_USE_BASE64 | 0xF;

    if (cfg->action == SPACC_ACTION_ENCRYPT) {
        dma_desc[CRYPTODMA_CIPHER] |= CTL_ENCRYPT_ENABLE;
        *result_size = (rt_uint32_t)(((cfg->len + 2) / 3) * 4);
    } else {
        *result_size = (rt_uint32_t)((cfg->len / 4) * 3);
    }

    /* DMA layout: src len in L/H; do in-place or to dst */
    desc_set_src_addr_and_len((uintptr_t)cfg->src, cfg->len);
    desc_set_dst_addr((uintptr_t)cfg->dst, *result_size);

    dma_desc[BASE64_SIZE] = *result_size;
    set_desc_addr();

    int ret = trigger_cryptodma_engine_and_wait_finish();
    /* target cache consistency processing */
    flush_addr_cache((void *)(uintptr_t)cfg->dst, (int)*result_size);
    dbg_reg_dump();
    if (ret != RT_EOK) return ret;
    return RT_EOK;
}

int cvi_spacc_hash_hw(SPACC_ALGO_E algo, const spacc_hash_config_s *cfg, rt_uint32_t *result_size)
{
    // Check input parameters
    if (cfg == RT_NULL) return -RT_EINVAL;
    if (cfg->src == 0 || cfg->len == 0) return -RT_EINVAL;
    if (cfg->dst == 0) return -RT_EINVAL;
    if (cfg->len & 0x3Fu) {
        rt_kprintf("cfg->len 0x%08x: Length of SHA/SM3 must be 64B aligned.\n",
                   (rt_uint32_t)cfg->len);
        return -RT_EINVAL;
    }
    if ((cfg->src & 0x0Fu) || (cfg->dst & 0x0Fu)) {
        rt_kprintf("cfg->src: 0x%08x, cfg->dst: 0x%08x: Must be 16B aligned\n", cfg->src, cfg->dst);
        return -RT_EINVAL;
    }
    rt_memset(dma_desc, 0, sizeof(dma_desc));
    rt_uint32_t *init_val_ptr = NULL;
    rt_base_t    param_reg_addr;
    rt_uint32_t  word_cnt;
    switch (algo) {
    case SPACC_ALGO_SHA1:
        dma_desc[CRYPTODMA_CTRL]   = DES_USE_DESCRIPTOR_KEY | DES_USE_SHA | 0xF;
        dma_desc[CRYPTODMA_CIPHER] = CTL_HASH_PARAM_ENABLE;
        init_val_ptr               = (rt_uint32_t *)sha1_init_value;
        param_reg_addr             = CRYPTODMA_SHA_PARA;
        word_cnt                   = 5;
        break;
    case SPACC_ALGO_SHA256:
        dma_desc[CRYPTODMA_CTRL]   = DES_USE_DESCRIPTOR_KEY | DES_USE_SHA | 0xF;
        dma_desc[CRYPTODMA_CIPHER] = CTL_SHA256_ENABLE | CTL_HASH_PARAM_ENABLE;
        init_val_ptr               = (rt_uint32_t *)sha256_init_value;
        param_reg_addr             = CRYPTODMA_SHA_PARA;
        word_cnt                   = 8;
        break;
    case SPACC_ALGO_SM3:
        dma_desc[CRYPTODMA_CTRL]   = DES_USE_DESCRIPTOR_KEY | DES_USE_SM3 | 0xF;
        dma_desc[CRYPTODMA_CIPHER] = CTL_HASH_PARAM_ENABLE;
        init_val_ptr               = (rt_uint32_t *)sm3_init_value;
        param_reg_addr             = CRYPTODMA_SM3_PARA;
        word_cnt                   = 8;
        break;
    default:
        rt_kprintf("Unsupported hash algo code: %d\n", algo);
        return -RT_EINVAL;
    }
    // Clear hash output first
    for (rt_uint32_t i = 0; i < word_cnt; i++) {
        mmio_write_32(param_reg_addr + i * 4, 0x12345678);
        dma_desc[CRYPTODMA_KEY + i] = init_val_ptr[i];
    }
    desc_set_src_addr_and_len((uintptr_t)cfg->src, cfg->len);
    set_desc_addr();

    int ret = trigger_cryptodma_engine_and_wait_finish();
    dbg_reg_dump();
    if (ret != RT_EOK) return ret;
    /* Read back */
    for (uint32_t i = 0; i < word_cnt; ++i) {
        ((uint32_t *)cfg->dst)[i] = mmio_read_32(param_reg_addr + i * 4);
    }
    *result_size = word_cnt * 8;
    return RT_EOK;
}
