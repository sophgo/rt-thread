/*
 * RT-Thread SPACC (Crypto DMA) driver (skeleton)
 * Ported with reference to Linux driver semantics
 */

#include "drv_spacc.h"

#include <rtdevice.h>
#include <rthw.h>
#include <rtthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPACC_DEVICE_NAME    "spacc"
#define HWCRYPTO_DEVICE_NAME RT_HWCRYPTO_DEFAULT_NAME

static rt_err_t spacc_symm_cipher_ioctl(SPACC_ALGO_E algo, void *args, spacc_dev_t *spacc)
{
    return cvi_spacc_symm_cipher_hw(algo, (spacc_symm_cipher_config_s *)args,
                                    &spacc->result_size);
}

static rt_err_t spacc_control(rt_device_t dev, int cmd, void *args)
{
    spacc_dev_t *spacc = (spacc_dev_t *)dev;

    if (args == RT_NULL) return -RT_EINVAL;

    switch (cmd) {
    case IOCTL_SPACC_DES_ACTION:
        return spacc_symm_cipher_ioctl(SPACC_ALGO_DES, args, spacc);
    case IOCTL_SPACC_TDES_ACTION:
        return spacc_symm_cipher_ioctl(SPACC_ALGO_TDES, args, spacc);
    case IOCTL_SPACC_AES_ACTION:
        return spacc_symm_cipher_ioctl(SPACC_ALGO_AES, args, spacc);
    case IOCTL_SPACC_SM4_ACTION:
        return spacc_symm_cipher_ioctl(SPACC_ALGO_SM4, args, spacc);
    case IOCTL_SPACC_BASE64_ACTION:
        return cvi_spacc_base64_hw((spacc_base64_config_s *)args, &spacc->result_size);
    case IOCTL_SPACC_SHA1_ACTION:
        return cvi_spacc_hash_hw(SPACC_ALGO_SHA1, (spacc_hash_config_s *)args,
                                 &spacc->result_size);
    case IOCTL_SPACC_SHA256_ACTION:
        return cvi_spacc_hash_hw(SPACC_ALGO_SHA256, (spacc_hash_config_s *)args,
                                 &spacc->result_size);
    case IOCTL_SPACC_SM3_ACTION:
        return cvi_spacc_hash_hw(SPACC_ALGO_SM3, (spacc_hash_config_s *)args, &spacc->result_size);
    default:
        return -RT_EINVAL;
    }
}

static rt_err_t spacc_init(rt_device_t dev)
{
    RT_UNUSED(dev);
    return RT_EOK;
}

static rt_err_t spacc_open(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(oflag);
    return RT_EOK;
}

static rt_err_t spacc_close(rt_device_t dev)
{
    RT_UNUSED(dev);
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops spacc_device_ops = {
    .init    = spacc_init,
    .open    = spacc_open,
    .close   = spacc_close,
    .read    = RT_NULL,
    .write   = RT_NULL,
    .control = spacc_control,
};
#endif
int rt_hw_spacc_init(void)
{
    spacc_dev_t *spacc = &g_spacc;
    rt_memset(spacc, 0, sizeof(*spacc));
#if defined(RT_USING_DEVICE_OPS)
    spacc->dev.type = RT_Device_Class_Miscellaneous;
    spacc->dev.ops  = &spacc_device_ops;
#else
    spacc->dev.init      = spacc_init;
    spacc->dev.open      = spacc_open;
    spacc->dev.close     = spacc_close;
    spacc->dev.control   = spacc_control;
    spacc->dev.user_data = RT_NULL;
#endif
    return rt_device_register(&spacc->dev, SPACC_DEVICE_NAME, RT_DEVICE_FLAG_RDWR);
}
INIT_DEVICE_EXPORT(rt_hw_spacc_init);

#ifdef RT_USING_HWCRYPTO
/**********
 * Interface for RTT
 * TODO:
 *   1. Fully support the `update` function of hash algorithm, can be realized by a buffer.
 *   2. Support multi-thread, currently is *Not* thread-safe.
 *   3. Add `base64` and `SM4` algorithm
 *   4. The code structure should be improved.
 **********/

/**********
 * Hash related
 **********/
/***** SHA-1 *****/
static spacc_hash_config_s hash_cfg;

rt_err_t cvi_hash_update(struct hwcrypto_hash *hash_ctx, const rt_uint8_t *in, rt_size_t length)
{
    hash_cfg.src = (rt_uint64_t)(rt_ubase_t)in;
    hash_cfg.len = length;
    return RT_EOK;
}

rt_err_t cvi_sha1_finish(struct hwcrypto_hash *hash_ctx, rt_uint8_t *out, rt_size_t length)
{
    if (length != 20) {
        rt_kprintf("SHA-1 output length must be 20 bytes, got %d bytes\n", length);
        return -RT_EINVAL;
    }
    hash_cfg.dst = (rt_uint64_t)(rt_ubase_t)out;
    return cvi_spacc_hash_hw(SPACC_ALGO_SHA1, &hash_cfg, &g_spacc.result_size);
}

/***** SHA-256 *****/
rt_err_t cvi_sha2_finish(struct hwcrypto_hash *hash_ctx, rt_uint8_t *out, rt_size_t length)
{
    if (hash_ctx->parent.type != HWCRYPTO_TYPE_SHA256) {
        rt_kprintf("Only support SHA-256");
        return -RT_EINVAL;
    }
    if (length != 32) {
        rt_kprintf("SHA-256 output length must be 32 bytes, got %d bytes\n", length);
        return -RT_EINVAL;
    }
    hash_cfg.dst = (rt_uint64_t)(rt_ubase_t)out;
    return cvi_spacc_hash_hw(SPACC_ALGO_SHA256, &hash_cfg, &g_spacc.result_size);
}

static struct hwcrypto_hash_ops cvi_hwcrypto_sha1_ops = {
    .update = cvi_hash_update,
    .finish = cvi_sha1_finish,
};

static struct hwcrypto_hash_ops cvi_hwcrypto_sha2_ops = {
    .update = cvi_hash_update,
    .finish = cvi_sha2_finish,
};

/**********
 * Symmetric cipher related
 **********/
static rt_err_t cvi_symmetric_cipher_common_setting(struct hwcrypto_symmetric      *symmetric_ctx,
                                                    struct hwcrypto_symmetric_info *symmetric_info,
                                                    spacc_symm_cipher_config_s     *cfg)
{
    cfg->src = (rt_uint64_t)(rt_ubase_t)symmetric_info->in;
    cfg->dst = (rt_uint64_t)(rt_ubase_t)symmetric_info->out;
    cfg->len = (rt_size_t)symmetric_info->length;

    switch (symmetric_info->mode) {
    case HWCRYPTO_MODE_ENCRYPT:
        cfg->action = SPACC_ACTION_ENCRYPT;
        break;
    case HWCRYPTO_MODE_DECRYPT:
        cfg->action = SPACC_ACTION_DECRYPT;
        break;
    default:
        rt_kprintf("Unsupported symmetric algo code: %d\n", symmetric_info->mode);
        return -RT_EINVAL;
    }

    cfg->key = (rt_uint64_t)(rt_ubase_t)symmetric_ctx->key;
    cfg->iv  = (rt_uint64_t)(rt_ubase_t)symmetric_ctx->iv;
    return RT_EOK;
}

/***** DES *****/
static rt_err_t cvi_des_crypt(struct hwcrypto_symmetric      *symmetric_ctx,
                              struct hwcrypto_symmetric_info *symmetric_info)
{
    spacc_symm_cipher_config_s cfg;
    if (symmetric_ctx->key_bitlen != 64) {
        rt_kprintf("[ERR] Key bits mismatch: expected 64 bits for DES, got %d bits\n",
                   symmetric_ctx->key_bitlen);
        return -RT_EINVAL;
    }
    cfg.key_mode = SPACC_DES_KEY_SIZE_64BITS;
    switch (symmetric_ctx->parent.type) {
    case HWCRYPTO_TYPE_DES_CBC:
        cfg.mode = SPACC_ALGO_MODE_CBC;
        break;
    case HWCRYPTO_TYPE_DES_ECB:
        cfg.mode = SPACC_ALGO_MODE_ECB;
        break;
    default:
        rt_kprintf("Unsupported symmetric algo code for DES: %d\n", symmetric_ctx->parent.type);
        return -RT_EINVAL;
    }

    rt_err_t ret = cvi_symmetric_cipher_common_setting(symmetric_ctx, symmetric_info, &cfg);
    if (ret != RT_EOK) return ret;
    ret = cvi_spacc_symm_cipher_hw(SPACC_ALGO_DES, &cfg, &g_spacc.result_size);
    return ret;
}

/***** TDES *****/
static rt_err_t cvi_3des_crypt(struct hwcrypto_symmetric      *symmetric_ctx,
                               struct hwcrypto_symmetric_info *symmetric_info)
{
    spacc_symm_cipher_config_s cfg;
    if (symmetric_ctx->key_bitlen != 192) {
        rt_kprintf("[ERR] Key bits mismatch: expected 192 bits for 3DES, got %d bits\n",
                   symmetric_ctx->key_bitlen);
        return -RT_EINVAL;
    }
    cfg.key_mode = SPACC_TDES_KEY_SIZE_192BITS;
    switch (symmetric_ctx->parent.type) {
    case HWCRYPTO_TYPE_3DES_CBC:
        cfg.mode = SPACC_ALGO_MODE_CBC;
        break;
    case HWCRYPTO_TYPE_3DES_ECB:
        cfg.mode = SPACC_ALGO_MODE_ECB;
        break;
    default:
        rt_kprintf("Unsupported symmetric algo code for 3DES: %d\n", symmetric_ctx->parent.type);
        return -RT_EINVAL;
    }

    rt_err_t ret = cvi_symmetric_cipher_common_setting(symmetric_ctx, symmetric_info, &cfg);
    if (ret != RT_EOK) return ret;
    ret = cvi_spacc_symm_cipher_hw(SPACC_ALGO_TDES, &cfg, &g_spacc.result_size);
    return ret;
}

/***** AES *****/
static rt_err_t cvi_aes_crypt(struct hwcrypto_symmetric      *symmetric_ctx,
                              struct hwcrypto_symmetric_info *symmetric_info)
{
    spacc_symm_cipher_config_s cfg;
    rt_uint16_t                key_bitlen = symmetric_ctx->key_bitlen;
    if (key_bitlen == 128) {
        cfg.key_mode = SPACC_AES_KEY_SIZE_128BITS;
    } else if (key_bitlen == 192) {
        cfg.key_mode = SPACC_AES_KEY_SIZE_192BITS;
    } else if (key_bitlen == 256) {
        cfg.key_mode = SPACC_AES_KEY_SIZE_256BITS;
    } else {
        rt_kprintf("[ERR] Key bits mismatch: expected 128/192/256 bits for AES, got %d bits\n",
                   symmetric_ctx->key_bitlen);
        return -RT_EINVAL;
    }

    switch (symmetric_ctx->parent.type) {
    case HWCRYPTO_TYPE_AES_CBC:
        cfg.mode = SPACC_ALGO_MODE_CBC;
        break;
    case HWCRYPTO_TYPE_AES_ECB:
        cfg.mode = SPACC_ALGO_MODE_ECB;
        break;
    case HWCRYPTO_TYPE_AES_CTR:
        cfg.mode = SPACC_ALGO_MODE_CTR;
        break;
    case HWCRYPTO_TYPE_AES_OFB:
        cfg.mode = SPACC_ALGO_MODE_OFB;
        break;
    default:
        rt_kprintf("Unsupported symmetric algo code for AES: %d\n", symmetric_ctx->parent.type);
        return -RT_EINVAL;
    }

    rt_err_t ret = cvi_symmetric_cipher_common_setting(symmetric_ctx, symmetric_info, &cfg);
    if (ret != RT_EOK) return ret;
    ret = cvi_spacc_symm_cipher_hw(SPACC_ALGO_AES, &cfg, &g_spacc.result_size);
    return ret;
}

static struct hwcrypto_symmetric_ops cvi_hwcrypto_des_ops = {
    .crypt = cvi_des_crypt,
};
static struct hwcrypto_symmetric_ops cvi_hwcrypto_3des_ops = {
    .crypt = cvi_3des_crypt,
};
static struct hwcrypto_symmetric_ops cvi_hwcrypto_aes_ops = {
    .crypt = cvi_aes_crypt,
};

/**********
 * RTT CRYPTO
 **********/
static rt_err_t cvi_hwcrypto_create(struct rt_hwcrypto_ctx *ctx)
{
    if (!ctx) return -RT_EINVAL;

    switch (ctx->type & HWCRYPTO_MAIN_TYPE_MASK) {
    case HWCRYPTO_TYPE_DES:
        ((struct hwcrypto_symmetric *)ctx)->ops = &cvi_hwcrypto_des_ops;
        break;
    case HWCRYPTO_TYPE_3DES:
        ((struct hwcrypto_symmetric *)ctx)->ops = &cvi_hwcrypto_3des_ops;
        break;
    case HWCRYPTO_TYPE_AES:
        ((struct hwcrypto_symmetric *)ctx)->ops = &cvi_hwcrypto_aes_ops;
        break;
    case HWCRYPTO_TYPE_SHA1:
        ((struct hwcrypto_hash *)ctx)->ops = &cvi_hwcrypto_sha1_ops;
        break;
    case HWCRYPTO_TYPE_SHA2:
        ((struct hwcrypto_hash *)ctx)->ops = &cvi_hwcrypto_sha2_ops;
        break;
    }
    return RT_EOK;
}

static inline void cvi_hwcrypto_destroy(struct rt_hwcrypto_ctx *ctx)
{
    RT_UNUSED(ctx);
}

static inline rt_err_t cvi_hwcrypto_copy(struct rt_hwcrypto_ctx       *des,
                                         const struct rt_hwcrypto_ctx *src)
{
    RT_UNUSED(des);
    RT_UNUSED(src);
    return RT_EOK;
}

static inline void cvi_hwcrypto_reset(struct rt_hwcrypto_ctx *ctx)
{
    RT_UNUSED(ctx);
}

static struct rt_hwcrypto_device cvi_hwcrypto_device;

static struct rt_hwcrypto_ops cvi_hwcrypto_ops = {
    .create  = cvi_hwcrypto_create,
    .destroy = cvi_hwcrypto_destroy,
    .copy    = cvi_hwcrypto_copy,
    .reset   = cvi_hwcrypto_reset,
};

int rt_hwcrypto_init(void)
{
    cvi_hwcrypto_device.ops = &cvi_hwcrypto_ops;

    rt_err_t ret = rt_hwcrypto_register(&cvi_hwcrypto_device, HWCRYPTO_DEVICE_NAME);
    if (ret != RT_EOK) {
        rt_kprintf("Failed to register hwcrypto device: %d\n", ret);
        return ret;
    }
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hwcrypto_init);
#endif
