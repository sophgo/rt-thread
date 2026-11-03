#include <dfs_posix.h>
#include <drivers/byteorder.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "drv_spacc.h"
#define SPACC_DEV_NAME "spacc"

static rt_align(64) unsigned char key[32] = {
    0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
    0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61};
static rt_align(64) unsigned char iv[16]    = {0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
                                               0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61};
static rt_align(64) unsigned char src16[16] = {0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x0b, 0x0b, 0x0b,
                                               0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
static rt_align(64) unsigned char src8[16]  = {0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x3, 0x3, 0x3};
static rt_align(64) unsigned char dst16[16];

static void dump_buf(const unsigned char *buf, int size)
{
    int i;
    for (i = 0; i < size; i++) rt_kprintf("0x%02x ", buf[i]);
    rt_kprintf("\n");
}

static void dump_key_iv(const unsigned char *key_buf, int key_len, const unsigned char *iv_buf,
                        int iv_len)
{
    rt_kprintf("key : ");
    dump_buf(key_buf, key_len);
    if (iv_len > 0) {
        rt_kprintf("iv  : ");
        dump_buf(iv_buf, iv_len);
    } else {
        rt_kprintf("iv  : <unused>\n");
    }
}

static SPACC_ALGO_MODE_E parse_mode(const char *s)
{
    if (!s) return SPACC_ALGO_MODE_ECB;
    if (!strcmp(s, "ecb") || !strcmp(s, "ECB")) return SPACC_ALGO_MODE_ECB;
    if (!strcmp(s, "cbc") || !strcmp(s, "CBC")) return SPACC_ALGO_MODE_CBC;
    if (!strcmp(s, "ctr") || !strcmp(s, "CTR")) return SPACC_ALGO_MODE_CTR;
    return SPACC_ALGO_MODE_ECB;
}

static SPACC_KEY_SIZE_E parse_keymode(int bits)
{
    if (bits == 256) return SPACC_AES_KEY_SIZE_256BITS;
    if (bits == 192) return SPACC_AES_KEY_SIZE_192BITS;
    return SPACC_AES_KEY_SIZE_128BITS;
}

static const char *mode_str(SPACC_ALGO_MODE_E m)
{
    switch (m) {
    case SPACC_ALGO_MODE_ECB:
        return "ecb";
    case SPACC_ALGO_MODE_CBC:
        return "cbc";
    case SPACC_ALGO_MODE_CTR:
        return "ctr";
    default:
        return "unknown";
    }
}

/* test  AES all mode x all key length, complete encrypt and decrypt and verify
 */
static int spacc_test_aes(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    SPACC_ALGO_MODE_E modes[] = {SPACC_ALGO_MODE_ECB, SPACC_ALGO_MODE_CBC, SPACC_ALGO_MODE_CTR};
    int               key_bits_list[] = {128, 192, 256};
    int               total = 0, passed = 0;

    unsigned char *src_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    unsigned char *enc_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    unsigned char *dec_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    if (!src_buffer || !enc_buffer || !dec_buffer) {
        rt_kprintf("[ERR] alloc buffer failed\n");
        goto FREE_MEM;
    }

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }

    for (unsigned mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
        for (unsigned ki = 0; ki < sizeof(key_bits_list) / sizeof(key_bits_list[0]); ki++) {
            total++;

            rt_memcpy(src_buffer, src16, sizeof(src16));
            rt_memset(enc_buffer, 0x00, sizeof(src16));
            rt_memset(dec_buffer, 0x00, sizeof(src16));

            rt_kprintf("src : ");
            dump_buf(src_buffer, sizeof(src16));

            spacc_aes_config_s cfg;
            /* encrypt */
            rt_memset(&cfg, 0, sizeof(cfg));
            cfg.src      = (rt_uint64_t)(rt_ubase_t)src_buffer;
            cfg.dst      = (rt_uint64_t)(rt_ubase_t)enc_buffer;
            cfg.len      = sizeof(src16);
            cfg.key      = (rt_ubase_t)key;
            cfg.iv       = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
            cfg.mode     = modes[mi];
            cfg.key_mode = parse_keymode(key_bits_list[ki]);
            cfg.action   = SPACC_ACTION_ENCRYPT;
            cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
            dump_key_iv(key, key_bits_list[ki] / 8, iv,
                        (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 16);
            if (rt_device_control(dev, IOCTL_SPACC_AES_ACTION, &cfg) != RT_EOK) {
                rt_kprintf("[AES %s %d enc] control failed\n", mode_str(modes[mi]),
                           key_bits_list[ki]);
                continue;
            }
            rt_kprintf("enc : ");
            dump_buf(enc_buffer, sizeof(src16));

            /* decrypt */
            rt_memset(&cfg, 0, sizeof(cfg));
            cfg.src      = (rt_uint64_t)(rt_ubase_t)enc_buffer;
            cfg.dst      = (rt_uint64_t)(rt_ubase_t)dec_buffer;
            cfg.len      = sizeof(src16);
            cfg.key      = (rt_ubase_t)key;
            cfg.iv       = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
            cfg.mode     = modes[mi];
            cfg.key_mode = parse_keymode(key_bits_list[ki]);
            cfg.action   = SPACC_ACTION_DECRYPT;
            cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
            dump_key_iv(key, key_bits_list[ki] / 8, iv,
                        (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 16);
            if (rt_device_control(dev, IOCTL_SPACC_AES_ACTION, &cfg) != RT_EOK) {
                rt_kprintf("[AES %s %d dec] control failed\n", mode_str(modes[mi]),
                           key_bits_list[ki]);
                continue;
            }

            rt_kprintf("dec : ");
            dump_buf(dec_buffer, sizeof(src16));

            int ok = (rt_memcmp(src_buffer, dec_buffer, sizeof(src16)) == 0);
            if (ok) passed++;
            rt_kprintf("[AES %s %d] %s\n", mode_str(modes[mi]), key_bits_list[ki],
                       ok ? "PASS" : "FAIL");
        }
    }

    rt_device_close(dev);
FREE_MEM:
    rt_free_align(src_buffer);
    rt_free_align(enc_buffer);
    rt_free_align(dec_buffer);

    rt_kprintf("AES summary: %d/%d PASS\n", passed, total);
    return (passed == total) ? 0 : -1;
}
MSH_CMD_EXPORT(spacc_test_aes, spacc_aes_test);

/* test SM4 all mode, complete encrypt and decrypt and verify */
static int spacc_test_sm4(int argc, char **argv)
{
    SPACC_ALGO_MODE_E modes[] = {SPACC_ALGO_MODE_ECB, SPACC_ALGO_MODE_CBC, SPACC_ALGO_MODE_CTR};
    int               total = 0, passed = 0;
    unsigned char    *src_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    unsigned char    *enc_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    unsigned char    *dec_buffer = (unsigned char *)rt_malloc_align(sizeof(src16), 16);
    if (!src_buffer || !enc_buffer || !dec_buffer) {
        rt_kprintf("[ERR] alloc buffer failed\n");
        goto FREE_MEM;
    }
    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }
    for (unsigned mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
        rt_memcpy(src_buffer, src16, sizeof(src16));
        rt_memset(enc_buffer, 0x00, sizeof(src16));
        rt_memset(dec_buffer, 0x00, sizeof(src16));

        rt_kprintf("src : ");
        dump_buf(src_buffer, sizeof(src16));

        spacc_sm4_config_s cfg;
        rt_memset(&cfg, 0, sizeof(cfg));
        cfg.src      = (rt_uint64_t)(rt_ubase_t)src_buffer;
        cfg.dst      = (rt_uint64_t)(rt_ubase_t)enc_buffer;
        cfg.len      = sizeof(src16);
        cfg.key      = (rt_ubase_t)key; /* first 16B as SM4 key */
        cfg.iv       = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
        cfg.mode     = modes[mi];
        cfg.key_mode = SPACC_AES_KEY_SIZE_128BITS;
        cfg.action   = SPACC_ACTION_ENCRYPT;
        cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
        dump_key_iv(key, 16, iv, (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 16);

        total++;
        if (rt_device_control(dev, IOCTL_SPACC_SM4_ACTION, &cfg) != RT_EOK) {
            rt_kprintf("[SM4 %s enc] control failed\n", mode_str(modes[mi]));
            continue;
        }
        rt_kprintf("enc : ");
        dump_buf(enc_buffer, sizeof(src16));
        /* decrypt */
        rt_memset(&cfg, 0, sizeof(cfg));
        cfg.src      = (rt_uint64_t)(rt_ubase_t)enc_buffer;
        cfg.dst      = (rt_uint64_t)(rt_ubase_t)dec_buffer;
        cfg.len      = sizeof(src16);
        cfg.key      = (rt_ubase_t)key;
        cfg.iv       = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
        cfg.mode     = modes[mi];
        cfg.key_mode = SPACC_AES_KEY_SIZE_128BITS;
        cfg.action   = SPACC_ACTION_DECRYPT;
        cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
        dump_key_iv(key, 16, iv, (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 16);
        if (rt_device_control(dev, IOCTL_SPACC_SM4_ACTION, &cfg) != RT_EOK) {
            rt_kprintf("[SM4 %s dec] control failed\n", mode_str(modes[mi]));
            continue;
        }

        rt_kprintf("dec : ");
        dump_buf(dec_buffer, sizeof(src16));

        int ok = (rt_memcmp(src_buffer, dec_buffer, sizeof(src16)) == 0);
        if (ok) passed++;
        rt_kprintf("[SM4 %s] %s\n", mode_str(modes[mi]), ok ? "PASS" : "FAIL");
    }
FREE_MEM:
    rt_free_align(src_buffer);
    rt_free_align(enc_buffer);
    rt_free_align(dec_buffer);
    rt_kprintf("SM4 summary: %d/%d PASS\n", passed, total);
    return (passed == total) ? 0 : -1;
}
MSH_CMD_EXPORT(spacc_test_sm4, spacc_sm4_test);

static int spacc_test_base64(int argc, char **argv)
{
    /* simple encode->decode roundtrip */
    const char    *msg = "hello"; /* 5B */
    size_t         in_len, enc_len;
    unsigned char *in, *enc, *dec;
    int            loop = 1, index = 0;
    int            ret = -RT_ERROR;

    if (argc != 1 && argc != 3) {
        rt_kprintf("spacc_test_base64 [msg] [loop]\n");
        return -RT_EINVAL;
    }
    if (argc == 3) {
        in_len = rt_strlen(argv[1]);
        loop   = atoi(argv[2]);
    } else {
        in_len = rt_strlen(msg);
    }
    enc_len = ((in_len + 2) / 3) * 4 + 1;
    ++in_len;

    for (index = 0; index < loop; index++) {
        in  = (unsigned char *)rt_malloc_align(in_len, 16);
        enc = (unsigned char *)rt_malloc_align(enc_len, 16);
        dec = (unsigned char *)rt_malloc_align(in_len, 16);
        if (!in || !enc || !dec) {
            rt_kprintf("malloc failed!\n");
            ret = -RT_ENOMEM;
            goto FREE_MEM;
        }
        if (argc == 3) {
            rt_memcpy(in, argv[1], in_len);
        } else {
            rt_memcpy(in, msg, in_len);
        }
        rt_memset(enc, 0, enc_len);
        rt_memset(dec, 0, in_len);

        spacc_base64_config_s cfg;
        rt_device_t           dev = rt_device_find(SPACC_DEV_NAME);
        if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
            rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
            ret = -RT_ERROR;
            goto FREE_MEM;
        }
        /* encode */
        rt_memset(&cfg, 0, sizeof(cfg));
        cfg.src    = (rt_uint64_t)(rt_ubase_t)in;
        cfg.dst    = (rt_uint64_t)(rt_ubase_t)enc;
        cfg.len    = in_len;
        cfg.action = SPACC_ACTION_ENCRYPT;
        if (rt_device_control(dev, IOCTL_SPACC_BASE64_ACTION, &cfg) != RT_EOK) {
            rt_kprintf("[BASE64 enc] control failed\n");
            goto CLOSE_DEV;
        }
        rt_kprintf("b64 enc: ");
        dump_buf(enc, enc_len);

        enc[enc_len - 1] = '\0';
        rt_kprintf("base64 enc : %s\n", enc);

        /* decode */
        rt_memset(&cfg, 0, sizeof(cfg));
        cfg.src    = (rt_uint64_t)(rt_ubase_t)enc;
        cfg.dst    = (rt_uint64_t)(rt_ubase_t)dec;
        cfg.len    = enc_len;
        cfg.action = SPACC_ACTION_DECRYPT;
        if (rt_device_control(dev, IOCTL_SPACC_BASE64_ACTION, &cfg) != RT_EOK) {
            rt_kprintf("[BASE64 dec] control failed\n");
            ret = -RT_ERROR;
            goto CLOSE_DEV;
        }
        rt_device_close(dev);

        rt_kprintf("b64 dec: ");
        dump_buf(dec, in_len);

        dec[in_len - 1] = '\0';
        rt_kprintf("base64 dec: %s\n", dec);

        int b64_ok = (rt_memcmp(in, dec, in_len) == 0);
        if (b64_ok) {
            rt_kprintf("[PASS] BASE64 roundtrip\n");
            ret = RT_EOK;
        } else {
            rt_kprintf("[FAIL] BASE64 roundtrip, count [%d]\n", index);
            ret = -RT_ERROR;
            break;
        }
    CLOSE_DEV:
        rt_device_close(dev);
    FREE_MEM:
        rt_free_align(dec);
        rt_free_align(enc);
        rt_free_align(in);
    }
    return ret;
}
MSH_CMD_EXPORT(spacc_test_base64, spacc_base64_test);

static int spacc_test_sha256(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    unsigned char *blk = (unsigned char *)rt_malloc_align(64, 16);
    unsigned char *out = (unsigned char *)rt_malloc_align(32, 16);
    if (!blk || !out) {
        rt_kprintf("[ERR] alloc buffer failed\n");
        goto FREE_MEM;
    }
    /*
       expect result:
        i : 0, 0xba4df22c
        i : 1, 0xea3b05f
        i : 2, 0x2a3be826
        i : 3, 0x9ee2b9c5
        i : 4, 0x5c1e161b
        i : 5, 0x5e42a71f
        i : 6, 0x62330473
        i : 7, 0x24988b93
      */
    rt_memset(blk, 0x00, 64);
    rt_memcpy(blk, "hello", 5);

    blk[5]               = 0x80; /* simple padding */
    uint64_t bits_len_be = rt_cpu_to_be64(40);
    memcpy(&blk[56], &bits_len_be, 8);

    spacc_hash_config_s cfg;
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src = (rt_uint64_t)(rt_ubase_t)blk;
    cfg.dst = (rt_uint64_t)(rt_ubase_t)out;
    cfg.len = 64; /* driver requires 64B alignment */

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }
    if (rt_device_control(dev, IOCTL_SPACC_SHA256_ACTION, &cfg) != RT_EOK) {
        rt_kprintf("[SHA256] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("SHA256: ");
    dump_buf(out, 32);
CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(blk);
    rt_free_align(out);
    return 0;
}
MSH_CMD_EXPORT(spacc_test_sha256, spacc_sha256_test);

static int spacc_test_sm3(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    unsigned char *blk = (unsigned char *)rt_malloc_align(64, 16);
    unsigned char *out = (unsigned char *)rt_malloc_align(32, 16);
    if (!blk || !out) {
        rt_kprintf("[ERR] alloc buffer failed\n");
        goto FREE_MEM;
    }
    rt_memset(blk, 0x00, 64);
    rt_memcpy(blk, "hello", 5);
    blk[5]               = 0x80; /* simple padding */
    uint64_t bits_len_be = rt_cpu_to_be64(40);
    memcpy(&blk[56], &bits_len_be, 8);

    spacc_hash_config_s cfg;
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src = (rt_uint64_t)(rt_ubase_t)blk;
    cfg.dst = (rt_uint64_t)(rt_ubase_t)out;
    cfg.len = 64;

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }
    if (rt_device_control(dev, IOCTL_SPACC_SM3_ACTION, &cfg) != RT_EOK) {
        rt_kprintf("[SM3] control failed\n");
        goto CLOSE_DEV;
    }

    rt_kprintf("SM3   : ");
    dump_buf(out, 32);
CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(blk);
    rt_free_align(out);
    return 0;
}
// can not support sm3
// MSH_CMD_EXPORT(spacc_test_sm3, spacc_sm3_test);

static int spacc_test_tdes(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    /* test DES/TDES all mode, complete encrypt and decrypt and verify */
    SPACC_ALGO_MODE_E modes[]  = {SPACC_ALGO_MODE_ECB, SPACC_ALGO_MODE_CBC, SPACC_ALGO_MODE_CTR};
    const char       *names[2] = {"DES", "TDES"};
    int               total = 0, passed = 0;

    unsigned char *src = (unsigned char *)rt_malloc_align(8, 16);
    unsigned char *enc = (unsigned char *)rt_malloc_align(8, 16);
    unsigned char *dec = (unsigned char *)rt_malloc_align(8, 16);
    if (!src || !enc || !dec) {
        rt_kprintf("[ERR] alloc buffer failed\n");
        goto FREE_MEM;
    }
    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("open %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }
    for (int which = 0; which < 2; which++) {
        for (unsigned mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
            rt_memcpy(src, src8, 8);
            rt_memset(enc, 0x00, 8);
            rt_memset(dec, 0x00, 8);

            rt_kprintf("src : ");
            dump_buf(src, 8);

            spacc_des_config_s cfg;
            rt_memset(&cfg, 0, sizeof(cfg));
            cfg.src    = (rt_uint64_t)(rt_ubase_t)src;
            cfg.dst    = (rt_uint64_t)(rt_ubase_t)enc;
            cfg.len    = 8;
            cfg.key    = (rt_ubase_t)key; /* use 8B/24B as key */
            cfg.iv     = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
            cfg.mode   = modes[mi];
            cfg.action = SPACC_ACTION_ENCRYPT;
            /* DES: 8B key, TDES: 24B key */
            dump_key_iv(key, (which == 0) ? 8 : 24, iv, (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 8);
            total++;
            int enc_ok;
            if (which == 0)
                enc_ok = (rt_device_control(dev, IOCTL_SPACC_DES_ACTION, &cfg) == RT_EOK);
            else
                enc_ok = (rt_device_control(dev, IOCTL_SPACC_TDES_ACTION, &cfg) == RT_EOK);
            if (!enc_ok) {
                rt_kprintf("[%s %s enc] control failed\n", names[which], mode_str(modes[mi]));
                continue;
            }

            rt_kprintf("enc : ");
            dump_buf(enc, 8);

            /* decrypt */
            rt_memset(&cfg, 0, sizeof(cfg));
            cfg.src    = (rt_uint64_t)(rt_ubase_t)enc;
            cfg.dst    = (rt_uint64_t)(rt_ubase_t)dec;
            cfg.len    = 8;
            cfg.key    = (rt_ubase_t)key;
            cfg.iv     = (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : (rt_ubase_t)iv;
            cfg.mode   = modes[mi];
            cfg.action = SPACC_ACTION_DECRYPT;
            dump_key_iv(key, (which == 0) ? 8 : 24, iv, (modes[mi] == SPACC_ALGO_MODE_ECB) ? 0 : 8);
            int dec_ok;
            if (which == 0)
                dec_ok = (rt_device_control(dev, IOCTL_SPACC_DES_ACTION, &cfg) == RT_EOK);
            else
                dec_ok = (rt_device_control(dev, IOCTL_SPACC_TDES_ACTION, &cfg) == RT_EOK);

            rt_kprintf("dec : ");
            dump_buf(dec, 8);

            int ok = (enc_ok && dec_ok && (rt_memcmp(src, dec, 8) == 0));
            if (ok) passed++;
            rt_kprintf("[%s %s] %s\n", names[which], mode_str(modes[mi]), ok ? "PASS" : "FAIL");
        }
    }
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(src);
    rt_free_align(enc);
    rt_free_align(dec);
    rt_kprintf("DES/TDES summary: %d/%d PASS\n", passed, total);
    return (passed == total) ? 0 : -1;
}
MSH_CMD_EXPORT(spacc_test_tdes, spacc_tdes_test);

int spacc_test(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    int total = 0, passed = 0;

    /* AES: all mode x all key length, complete encrypt and decrypt and verify */
    total++;
    if (spacc_test_aes(0, RT_NULL) == 0) passed++;

    /* SM4 all mode, complete encrypt and decrypt and verify */
    total++;
    if (spacc_test_sm4(0, RT_NULL) == 0) passed++;

    /* DES/TDES all mode, complete encrypt and decrypt and verify */
    total++;
    if (spacc_test_tdes(0, RT_NULL) == 0) passed++;

    /* BASE64 roundtrip, complete encode and decode */
    total++;
    if (spacc_test_base64(0, RT_NULL) == 0) passed++;

    rt_kprintf("spacc_test_all summary: %d/%d PASS\n", passed, total);
    return (passed == total) ? 0 : -1;
}
MSH_CMD_EXPORT(spacc_test, spacc_all);