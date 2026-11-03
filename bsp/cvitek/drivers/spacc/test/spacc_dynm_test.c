#include <dfs_posix.h>
#include <drivers/byteorder.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "drv_spacc.h"
#define SPACC_DEV_NAME "spacc"

static void dump_hex_buf(const uint8_t *buf, int size)
{
    for (int i = 0; i < size; i++) rt_kprintf("%02x", buf[i]);
    rt_kprintf("\n");
}

static const char *algo_name_map(SPACC_ALGO_E algo)
{
    switch (algo) {
    case SPACC_ALGO_DES:
        return "DES";
    case SPACC_ALGO_TDES:
        return "TDES";
    case SPACC_ALGO_AES:
        return "AES";
    case SPACC_ALGO_SM4:
        return "SM4";
    case SPACC_ALGO_BASE64:
        return "BASE64";
    case SPACC_ALGO_SHA1:
        return "SHA1";
    case SPACC_ALGO_SHA256:
        return "SHA256";
    case SPACC_ALGO_SM3:
        return "SM3";
    default:
        return "UNKNOWN";
    }
}

static const char *symm_algo_mode_name_map(SPACC_ALGO_MODE_E mode)
{
    switch (mode) {
    case SPACC_ALGO_MODE_ECB:
        return "ECB";
    case SPACC_ALGO_MODE_CBC:
        return "CBC";
    case SPACC_ALGO_MODE_CTR:
        return "CTR";
    case SPACC_ALGO_MODE_OFB:
        return "OFB";
    default:
        return "UNKNOWN";
    }
}

static inline SPACC_KEY_SIZE_E parse_aes_keymode(int key_len)
{
    if (key_len == 32) return SPACC_AES_KEY_SIZE_256BITS;
    if (key_len == 24) return SPACC_AES_KEY_SIZE_192BITS;
    return SPACC_AES_KEY_SIZE_128BITS;
}

static int spacc_test_symm_cipher(int argc, char **argv)
{
    static const SPACC_ALGO_MODE_E modes_map[] = {SPACC_ALGO_MODE_ECB, SPACC_ALGO_MODE_CBC,
                                                  SPACC_ALGO_MODE_CTR};

    SPACC_ALGO_MODE_E mode = SPACC_ALGO_MODE_ECB;
    SPACC_ALGO_E      algo = SPACC_ALGO_AES;

    int ctl_act        = IOCTL_SPACC_AES_ACTION;
    int expect_key_len = 16;
    int expect_iv_len  = 16;
    int block_size     = 16;

    const char *plaintext_str = NULL;
    int         plaintext_len = 0;
    const char *key_str       = NULL;
    int         key_len       = 0;
    const char *iv_str        = NULL;
    int         iv_len        = 0;
    int         ret           = -RT_ERROR;

    int                        padded_len;
    spacc_symm_cipher_config_s cfg;

    // parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-des") == 0) {
            algo           = SPACC_ALGO_DES;
            ctl_act        = IOCTL_SPACC_DES_ACTION;
            expect_key_len = 8;
            expect_iv_len  = 8;
            block_size     = 8;
        } else if (strcmp(argv[i], "-tdes") == 0) {
            algo           = SPACC_ALGO_TDES;
            ctl_act        = IOCTL_SPACC_TDES_ACTION;
            expect_key_len = 24;
            expect_iv_len  = 8;
            block_size     = 8;
        } else if (strcmp(argv[i], "-aes") == 0) {
            /* Use default setting */
        } else if (strcmp(argv[i], "-sm4") == 0) {
            algo    = SPACC_ALGO_SM4;
            ctl_act = IOCTL_SPACC_SM4_ACTION;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            int mode_ind = atoi(argv[++i]);
            if (mode_ind < 0 || mode_ind > 2) {
                rt_kprintf("[WARN] Invalid mode: %s, use 0(ECB), 1(CBC), 2(CTR), default 0(ECB)\n",
                           mode_ind);
            } else {
                mode = modes_map[mode_ind];
            }
        } else if (strcmp(argv[i], "-in") == 0 && i + 1 < argc) {
            plaintext_str = argv[++i];
            plaintext_len = strlen(plaintext_str);
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            key_str = argv[++i];
            key_len = strlen(key_str);
        } else if (strcmp(argv[i], "-iv") == 0 && i + 1 < argc) {
            iv_str = argv[++i];
            iv_len = strlen(iv_str);
            if (iv_len == 0) {
                rt_kprintf("[WARN] Empty string provided for -iv-str\n");
            }
        }
    }
    // Check plaintext length
    if (plaintext_len == 0) {
        rt_kprintf("[ERR] Empty string provided for -in-str\n");
        return -RT_EINVAL;
    }
    // Check key length
    if (key_len == 0) {
        rt_kprintf("[ERR] Empty string provided for -k-str\n");
        return -RT_EINVAL;
    }
    if (algo == SPACC_ALGO_AES) {
        if (!(key_len == 16 || key_len == 24 || key_len == 32)) {
            rt_kprintf("[ERR] Key length mismatch: expected 16/24/32 bytes for AES, got %d bytes\n",
                       key_len);
            return -RT_EINVAL;
        }
    } else {
        if (expect_key_len != key_len) {
            rt_kprintf("[ERR] Key length mismatch: expected %d bytes, got %d bytes\n",
                       expect_key_len, key_len);
            return -RT_EINVAL;
        }
    }
    // Check IV length, IV is required for CBC & CTR
    if (mode != SPACC_ALGO_MODE_ECB && expect_iv_len != iv_len) {
        rt_kprintf("[ERR] IV length mismatch: expected %d bytes, got %d bytes\n", expect_iv_len,
                   iv_len);
        return -RT_EINVAL;
    }

    // Ensure padded length is multiple of 'block_size' bytes, PKCS#7 requires padding even if
    // plaintext is aligned
    padded_len = ((plaintext_len + (block_size - 1)) / block_size) * block_size;
    if (0 == plaintext_len % block_size) {
        padded_len += block_size;
    }
    // malloc aligned memory for buffers
    unsigned char *padded_plaintext = (unsigned char *)rt_malloc_align(padded_len, 16);
    unsigned char *encrypted_data   = (unsigned char *)rt_malloc_align(padded_len, 16);
    unsigned char *decrypted_data   = (unsigned char *)rt_malloc_align(padded_len, 16);
    if (!padded_plaintext || !encrypted_data || !decrypted_data) {
        rt_kprintf("[ERR] Failed to allocate memory for data buffers\n");
        ret = -RT_ENOMEM;
        goto FREE_MEM;
    }
    // PKCS#7 padding
    rt_memcpy(padded_plaintext, plaintext_str, plaintext_len);
    int padding_len = padded_len - plaintext_len;
    rt_memset(padded_plaintext + plaintext_len, padding_len, padding_len);

    // Display test parameters
    rt_kprintf("\n-----Symmetric Cipher Test Parameters-----\n");
    rt_kprintf("Symm Cipher Algo:\t%s\n", algo_name_map(algo));
    rt_kprintf("Cipher Mode:\t%s\n", symm_algo_mode_name_map(mode));
    rt_kprintf("Key Len:\t%d bytes\n", key_len);
    rt_kprintf("Plaintext Len:\t%d bytes\n", plaintext_len);
    rt_kprintf("Padded Len:\t%d bytes\n", padded_len);
    rt_kprintf("Plaintext (HEX):\t");
    dump_hex_buf((unsigned char *)plaintext_str, plaintext_len);
    rt_kprintf("Key (HEX):\t");
    dump_hex_buf((unsigned char *)key_str, key_len);
    if (mode != SPACC_ALGO_MODE_ECB) {
        rt_kprintf("IV (HEX):\t");
        dump_hex_buf((unsigned char *)iv_str, iv_len);
    }
    rt_kprintf("\n");

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("[ERR] Find and open device %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }

    // Encrypt operation
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src    = (rt_uint64_t)padded_plaintext;
    cfg.dst    = (rt_uint64_t)encrypted_data;
    cfg.len    = padded_len;
    cfg.action = SPACC_ACTION_ENCRYPT;

    cfg.key  = (rt_uint64_t)key_str;
    cfg.iv   = (mode == SPACC_ALGO_MODE_ECB) ? 0 : (rt_uint64_t)iv_str;
    cfg.mode = mode;
    if (algo == SPACC_ALGO_AES || algo == SPACC_ALGO_SM4) {
        cfg.key_mode = parse_aes_keymode(key_len);
        cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
    }
    rt_kprintf("Encrypting...\n");
    if (rt_device_control(dev, ctl_act, &cfg) != RT_EOK) {
        rt_kprintf("[ERR] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("Encrypted Data (HEX):\t");
    dump_hex_buf(encrypted_data, padded_len);

    // Decrypt operation
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src    = (rt_uint64_t)encrypted_data;
    cfg.dst    = (rt_uint64_t)decrypted_data;
    cfg.len    = padded_len;
    cfg.action = SPACC_ACTION_DECRYPT;
    cfg.key    = (rt_uint64_t)key_str;
    cfg.iv     = (mode == SPACC_ALGO_MODE_ECB) ? 0 : (rt_uint64_t)iv_str;
    cfg.mode   = mode;
    if (algo == SPACC_ALGO_AES || algo == SPACC_ALGO_SM4) {
        cfg.key_mode = parse_aes_keymode(key_len);
        cfg.otp      = SPACC_KEY_SOURCE_DESCRIPTOR;
    }
    rt_kprintf("Decrypting...\n");
    if (rt_device_control(dev, ctl_act, &cfg) != RT_EOK) {
        rt_kprintf("[ERR] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("Decrypted Data (HEX):\t");
    dump_hex_buf(decrypted_data, padded_len);

    // self encrypt & decrypt verification
    if (rt_memcmp(padded_plaintext, decrypted_data, padded_len) == 0) {
        ret = RT_EOK;
    }
    rt_kprintf("Encrypt & Decrypt Result:\t%s\n", (ret == RT_EOK) ? "[PASS]" : "[FAIL]");
CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(padded_plaintext);
    rt_free_align(encrypted_data);
    rt_free_align(decrypted_data);
    return ret;
}
MSH_CMD_EXPORT(spacc_test_symm_cipher, spacc_test_symm_cipher);

static int spacc_test_hash(int argc, char **argv)
{
    static const char *hash_algo_names_map[] = {"SHA1", "SHA256", "SM3"};

    SPACC_ALGO_E algo = SPACC_ALGO_SHA256;

    const char *msg          = NULL;
    int         msg_len      = 0;
    int         hash_msg_len = 32;
    int         ctl_act      = IOCTL_SPACC_SHA256_ACTION;
    int         ret          = -RT_ERROR;

    int                 padded_len;
    spacc_hash_config_s cfg;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-sha1") == 0) {
            algo         = SPACC_ALGO_SHA1;
            hash_msg_len = 20;
            ctl_act      = IOCTL_SPACC_SHA1_ACTION;
        } else if (strcmp(argv[i], "-sha256") == 0) {
            algo    = SPACC_ALGO_SHA256;
            ctl_act = IOCTL_SPACC_SHA256_ACTION;
        } else if (strcmp(argv[i], "-sm3") == 0) {
            algo    = SPACC_ALGO_SM3;
            ctl_act = IOCTL_SPACC_SM3_ACTION;
        } else if (strcmp(argv[i], "-in") == 0 && i + 1 < argc) {
            msg     = argv[++i];
            msg_len = strlen(msg);
        }
    }
    // Check message length
    if (msg_len == 0) {
        rt_kprintf("[ERR] Empty string provided for -in-str\n");
        return -RT_EINVAL;
    }
    // Ensure padded length is multiple of 64B (512 bits) as required by SHA-256/SM3
    padded_len = ((msg_len + 8 + 63) / 64) * 64;
    if (padded_len < msg_len + 9) {  // At least 1 byte for 0x80 and 8 bytes for length
        padded_len += 64;
    }
    // malloc aligned memory for buffers
    unsigned char *padded_msg = (unsigned char *)rt_malloc_align(padded_len, 16);
    unsigned char *hash_msg   = (unsigned char *)rt_malloc_align(hash_msg_len, 16);
    if (!padded_msg || !hash_msg) {
        rt_kprintf("[ERR] Failed to allocate memory for data buffers\n");
        ret = -RT_ENOMEM;
        goto FREE_MEM;
    }
    // Copy original message and apply standard padding
    rt_memset(padded_msg, 0, padded_len);
    rt_memcpy(padded_msg, msg, msg_len);
    padded_msg[msg_len] = 0x80;  // Append '1' bit

    // Append message length as 64-bit big-endian value at the end
    uint64_t bit_len = (uint64_t)msg_len * 8;
    bit_len          = rt_cpu_to_be64(bit_len);
    memcpy(padded_msg + padded_len - 8, &bit_len, 8);

    // Display test parameters
    rt_kprintf("\n-----Hash Test Parameters-----\n");
    rt_kprintf("Hash algo:\t%s\n", algo_name_map(algo));
    rt_kprintf("Msg Len:\t%d bytes\n", msg_len);
    rt_kprintf("Padded Len:\t%d bytes\n", padded_len);
    rt_kprintf("Msg (HEX):\t");
    dump_hex_buf((unsigned char *)msg, msg_len);

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("[ERR] Find and open device %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }

    // Hash operation
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src = (rt_uint64_t)padded_msg;
    cfg.dst = (rt_uint64_t)hash_msg;
    cfg.len = padded_len;

    rt_kprintf("Hash...\n");
    if (rt_device_control(dev, ctl_act, &cfg) != RT_EOK) {
        rt_kprintf("[ERR] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("Hash Data (HEX):\t");
    dump_hex_buf(hash_msg, hash_msg_len);
    ret = RT_EOK;

CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(padded_msg);
    rt_free_align(hash_msg);
    return ret;
}
MSH_CMD_EXPORT(spacc_test_hash, spacc_test_hash);

static int spacc_test_base64_dynm(int argc, char **argv)
{
    const char *data     = NULL;
    int         data_len = 0;
    int         ret      = -RT_ERROR;

    int                   encode_data_len;
    int                   decode_data_len;
    spacc_base64_config_s cfg;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-in") == 0 && i + 1 < argc) {
            data     = argv[++i];
            data_len = strlen(data);
        }
    }
    // Check data length
    if (data_len == 0) {
        rt_kprintf("[ERR] Empty string provided for -in-str\n");
        return -RT_EINVAL;
    }
    encode_data_len = ((data_len + 2) / 3) * 4;
    decode_data_len = (encode_data_len / 4) * 3;
    // decode_data_len = data_len;
    // malloc aligned memory for buffers
    unsigned char *raw_data    = (unsigned char *)rt_malloc_align(data_len, 16);
    unsigned char *encode_data = (unsigned char *)rt_malloc_align(encode_data_len, 16);
    unsigned char *decode_data = (unsigned char *)rt_malloc_align(decode_data_len, 16);
    if (!raw_data || !encode_data || !decode_data) {
        rt_kprintf("[ERR] Failed to allocate memory for data buffers\n");
        ret = -RT_ENOMEM;
        goto FREE_MEM;
    }
    // Copy original message and apply standard padding
    rt_memcpy(raw_data, data, data_len);
    rt_memset(encode_data, 0, encode_data_len);
    rt_memset(decode_data, 0, decode_data_len);

    // Display test parameters
    rt_kprintf("\n-----Base64 Test Parameters-----\n");
    rt_kprintf("Data Len:\t%d bytes\n", data_len);
    rt_kprintf("Encoded Len:\t%d bytes\n", encode_data_len);
    rt_kprintf("Data (HEX):\t");
    dump_hex_buf(raw_data, data_len);

    rt_device_t dev = rt_device_find(SPACC_DEV_NAME);
    if (!dev || rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) {
        rt_kprintf("[ERR] Find and open device %s failed\n", SPACC_DEV_NAME);
        goto FREE_MEM;
    }

    // Encode operation
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src    = (rt_uint64_t)raw_data;
    cfg.dst    = (rt_uint64_t)encode_data;
    cfg.len    = data_len;
    cfg.action = SPACC_ACTION_ENCRYPT;

    rt_kprintf("Encode...\n");
    if (rt_device_control(dev, IOCTL_SPACC_BASE64_ACTION, &cfg) != RT_EOK) {
        rt_kprintf("[ERR] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("Encode Data (HEX):\t");
    dump_hex_buf(encode_data, encode_data_len);

    // Decode operation
    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.src    = (rt_uint64_t)encode_data;
    cfg.dst    = (rt_uint64_t)decode_data;
    cfg.len    = encode_data_len;
    cfg.action = SPACC_ACTION_DECRYPT;

    rt_kprintf("Decode...\n");
    if (rt_device_control(dev, IOCTL_SPACC_BASE64_ACTION, &cfg) != RT_EOK) {
        rt_kprintf("[ERR] control failed\n");
        goto CLOSE_DEV;
    }
    rt_kprintf("Decode Data (HEX):\t");
    dump_hex_buf(decode_data, decode_data_len);

    if (rt_memcmp(raw_data, decode_data, data_len) == 0) {
        ret = RT_EOK;
    }
    rt_kprintf("Encode & Decode Result:\t%s\n", (ret == RT_EOK) ? "[PASS]" : "[FAIL]");

CLOSE_DEV:
    rt_device_close(dev);
FREE_MEM:
    rt_free_align(raw_data);
    rt_free_align(encode_data);
    rt_free_align(decode_data);
    return ret;
}
MSH_CMD_EXPORT(spacc_test_base64_dynm, spacc_test_base64_dynm);
