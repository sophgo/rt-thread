#include <fal.h>
#include <rtthread.h>

#define TEST_PARTITION_NAME "prim"
#define TEST_SECTOR_SIZE    4096

void align_offset_and_size(uint32_t *offset, uint32_t *size)
{
    uint32_t orig_offset = *offset;
    uint32_t orig_size   = *size;
    // 1. Align downward offset
    *offset = orig_offset & ~(TEST_SECTOR_SIZE - 1);

    // 2. Calculate original end position (offset + size)
    uint32_t original_end = orig_offset + orig_size;

    // 3. Calculate aligned end position (round up to nearest sector boundary)
    uint32_t aligned_end = (original_end + TEST_SECTOR_SIZE - 1) & ~(TEST_SECTOR_SIZE - 1);

    // 4. Adjust size to ensure it covers the original range
    *size = aligned_end - *offset;
}

uint8_t check_0xff_32bit(const uint8_t *buf, size_t size)
{
    const uint32_t *p = (const uint32_t *)buf;

    for (size_t i = 0; i < size / 4; i++) {
        if (p[i] != 0xFFFFFFFF) return -1;
    }
    // For rest bytes
    for (size_t i = (size / 4) * 4; i < size; i++) {
        if (buf[i] != 0xFF) return -1;
    }
    return 0;
}

static int spiflash_mount(void)
{
    struct rt_device *flash_dev = RT_NULL;

    rt_kprintf(
        "Currently, to use the ELM FAT filesystem on NOR flash, you must set "
        "RT_DFS_ELM_MAX_SECTOR_SIZE to 4096.\n");

    flash_dev = fal_blk_device_create(TEST_PARTITION_NAME);
    if (flash_dev == RT_NULL) {
        rt_kprintf("Failed to create device.\n");
        return -RT_ERROR;
    }

    if (dfs_mount(TEST_PARTITION_NAME, "/mnt/data", "elm", 0, 0) != 0) {
        rt_kprintf("file system initialization failed!\n");
        if (dfs_mkfs("elm", TEST_PARTITION_NAME) == 0) {
            if (dfs_mount(TEST_PARTITION_NAME, "/mnt/data", "elm", 0, 0) == 0) {
                rt_kprintf("mount to '/mnt/data' success!\n");
            }
        }
    } else {
        rt_kprintf("mount to '/mnt/data' success!\n");
    }
}

static int spif_erase_write_read(const struct fal_partition *part, int size, int offset,
                                 uint8_t *write_buf, uint8_t *read_buf)
{
    int       result;
    rt_tick_t start, end;
    uint32_t  elapsed_ticks, elapsed_ms;
    int       ret        = 0;
    int       erase_size = size;
    float     rate;

    rt_kprintf("offset: %d, size: %d\n", offset, size);

    align_offset_and_size(&offset, &erase_size);
    rt_kprintf("erase_size: %d\n", erase_size);
    start         = rt_tick_get();
    result        = fal_partition_erase(part, offset, erase_size);
    end           = rt_tick_get();
    elapsed_ticks = end - start;
    elapsed_ms    = elapsed_ticks * (1000 / RT_TICK_PER_SECOND);
    if (elapsed_ms == 0) {
        rt_kprintf("Erase time: shows 0ms. Try testing with a larger size for better accuracy.\n");
    } else {
        rate = (float)erase_size / (float)elapsed_ms;
        rt_kprintf("Erase time: %d ms, %d KB/s\n", elapsed_ms, (int)rate);
    }
    if (result < 0) {
        rt_kprintf("[FAIL] (erase error %d)\n", result);
        return -1;
    }

    rt_memset(read_buf, 0, size);
    result = fal_partition_read(part, offset, read_buf, size);
    if (result < 0) {
        rt_kprintf("[FAIL] (read error %d)\n", result);
        return -1;
    }
    // Check if the erasure was successful
    if (check_0xff_32bit(read_buf, size) != 0) {
        rt_kprintf("[FAIL] Data after erase is not 0xFF\n");
        return -1;
    }

    start         = rt_tick_get();
    result        = fal_partition_write(part, offset, write_buf, size);
    end           = rt_tick_get();
    elapsed_ticks = end - start;
    elapsed_ms    = elapsed_ticks * (1000 / RT_TICK_PER_SECOND);
    if (elapsed_ms == 0) {
        rt_kprintf("Write time: shows 0ms. Try testing with a larger size for better accuracy.\n");
    } else {
        rate = (float)size / (float)elapsed_ms;
        rt_kprintf("Write time: %d ms, %d KB/s\n", elapsed_ms, (int)rate);
    }
    if (result < 0) {
        rt_kprintf("[FAIL] (write error %d)\n", result);
        return -1;
    }

    rt_memset(read_buf, 0, size);
    start         = rt_tick_get();
    result        = fal_partition_read(part, offset, read_buf, size);
    end           = rt_tick_get();
    elapsed_ticks = end - start;
    elapsed_ms    = elapsed_ticks * (1000 / RT_TICK_PER_SECOND);
    if (elapsed_ms == 0) {
        rt_kprintf("Read time: shows 0ms. Try testing with a larger size for better accuracy.\n");
    } else {
        rate = (float)size / (float)elapsed_ms;
        rt_kprintf("Read time: %d ms, %d KB/s\n", elapsed_ms, (int)rate);
    }
    if (result < 0) {
        rt_kprintf("[FAIL] (read error %d)\n", result);
        return -1;
    }

    if (rt_memcmp(write_buf, read_buf, size) == 0) {
        rt_kprintf("[PASS] Data correct\n\n");

    } else {
        rt_kprintf("[FAIL] Data mismatch\n");

        for (int i = 0; i < size; i++) {
            if (write_buf[i] != read_buf[i]) {
                rt_kprintf("Diff at 0x%04X: W 0x%02X vs R 0x%02X\n", i, write_buf[i], read_buf[i]);
            }
        }
        ret = -1;
    }

    return ret;
}

static int spif_full_partition_ewr_test(void)
{
    const struct fal_partition *part   = RT_NULL;
    int                         offset = 0;
    int                         result;
    int                         ret = 0;
    int                         size;

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    size = part->len;

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        write_buf[i] = i & 0xFF;
    }

    ret = spif_erase_write_read(part, size, offset, write_buf, read_buf);

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}

static int spif_multi_offset_ewr_test(void)
{
    const struct fal_partition *part = RT_NULL;
    int                         result;
    int                         ret       = 0;
    int                         size      = 512;
    int                         offset[4] = {256, 257, 258, 259};

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }
    for (int j = 0; j < size; j++) {
        write_buf[j] = j & 0xFF;
    }

    for (int i = 0; i < sizeof(offset) / sizeof(offset[0]); i++) {
        rt_kprintf("[5.%d] offset: %d\n", i + 1, offset[i]);

        ret = spif_erase_write_read(part, size, offset[i], write_buf, read_buf);
        if (ret != 0) {
            rt_kprintf("[FAIL] spif_erase_write_read failed\n");
            goto exit;
        }
    }

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}
static int spif_multi_length_ewr_test(void)
{
    const struct fal_partition *part        = RT_NULL;
    const int                   TEST_OFFSET = (1 * TEST_SECTOR_SIZE);
    int                         result;
    int                         ret    = 0;
    int                         size[] = {1, 256, 257, 258, 515, 1024, 1024 * 1024};

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    for (int i = 0; i < sizeof(size) / sizeof(size[0]); i++) {
        rt_kprintf("[4.%d] length: %d\n", i + 1, size[i]);
        write_buf = rt_malloc(size[i]);
        read_buf  = rt_malloc(size[i]);

        if (write_buf == NULL || read_buf == NULL) {
            rt_kprintf("[FAIL] malloc failed\n");
            return -1;
        }

        for (int j = 0; j < size[i]; j++) {
            write_buf[j] = j & 0xFF;
        }

        ret = spif_erase_write_read(part, size[i], TEST_OFFSET, write_buf, read_buf);
        if (ret != 0) {
            rt_kprintf("[FAIL] spif_erase_write_read failed\n");
            goto exit;
        }
        if (write_buf) {
            rt_free(write_buf);
            write_buf = NULL;
        }
        if (read_buf) {
            rt_free(read_buf);
            read_buf = NULL;
        }
    }

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}

static int spif_ewr_boundary_test(void)
{
    const struct fal_partition *part           = RT_NULL;
    const int                   TEST_DATA_SIZE = 512;
    int                         result;
    int                         ret  = 0;
    int                         size = TEST_DATA_SIZE;

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        write_buf[i] = i & 0xFF;
    }

    // 1. partition start
    rt_kprintf("[3.1] partition start test: \n");
    ret = spif_erase_write_read(part, size, 0, write_buf, read_buf);
    if (ret != 0) {
        rt_kprintf("[FAIL] partition start test failed\n");
        goto exit;
    }

    // 2. partition end
    rt_kprintf("[3.2] partition end test: \n");
    ret = spif_erase_write_read(part, size, part->len - size, write_buf, read_buf);
    if (ret != 0) {
        rt_kprintf("[FAIL] partition end test failed\n");
        goto exit;
    }

    // 3. Crossing two blocks
    rt_kprintf("[3.3] crossing two blocks test: \n");
    ret = spif_erase_write_read(part, size, TEST_SECTOR_SIZE - size / 2, write_buf, read_buf);
    if (ret != 0) {
        rt_kprintf("[FAIL] crossing two blocks test failed\n");
        goto exit;
    }

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }

    return ret;
}

static int spif_data_pattern_sensitivity_test(void)
{
    const struct fal_partition *part   = RT_NULL;
    int                         offset = (1 * TEST_SECTOR_SIZE);
    int                         result;
    int                         ret  = 0;
    int                         size = 256;

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        if (i % 2 == 0) {
            write_buf[i] = 0x55;
        } else {
            write_buf[i] = 0xaa;
        }
    }

    ret = spif_erase_write_read(part, size, offset, write_buf, read_buf);

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}

static int spif_ewr_stability_test(void)
{
    const struct fal_partition *part   = RT_NULL;
    int                         offset = (1 * TEST_SECTOR_SIZE);
    int                         result;
    int                         ret        = 0;
    int                         size       = 256;
    int                         test_times = 50;

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        write_buf[i] = i & 0xFF;
    }

    for (int i = 0; i < test_times; i++) {
        ret = spif_erase_write_read(part, size, offset, write_buf, read_buf);
        if (ret != 0) {
            rt_kprintf("[FAIL] %s failed\n", __func__);
            goto exit;
        } else {
            rt_kprintf("Stability test: times: [%d] [PASS]\n", i + 1);
        }
    }

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}

static int spif_ewr_test(int size, int offset)
{
    const struct fal_partition *part = RT_NULL;
    int                         result;
    int                         ret = 0;

    rt_uint8_t *write_buf = NULL;
    rt_uint8_t *read_buf  = NULL;

    part = fal_partition_find(TEST_PARTITION_NAME);
    if (!part) {
        rt_kprintf("[FAIL] Partition '%s' not found!\n", TEST_PARTITION_NAME);
        goto exit;
    }

    rt_kprintf("FAL Flash get partition '%s' success\n", TEST_PARTITION_NAME);

    write_buf = rt_malloc(size);
    read_buf  = rt_malloc(size);

    if (write_buf == NULL || read_buf == NULL) {
        rt_kprintf("[FAIL] malloc failed\n");
        return -1;
    }

    for (int i = 0; i < size; i++) {
        write_buf[i] = i & 0xFF;
    }

    ret = spif_erase_write_read(part, size, offset, write_buf, read_buf);

exit:
    if (write_buf) {
        rt_free(write_buf);
        write_buf = NULL;
    }
    if (read_buf) {
        rt_free(read_buf);
        read_buf = NULL;
    }
    return ret;
}

int test_spif(int argc, char **argv)
{
    uint8_t is_test_all = 0;
    int     size        = 256;
    int     offset      = (1 * TEST_SECTOR_SIZE);

    rt_kprintf("--- %s start ---\n", __func__);

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "-a") == 0) {
            is_test_all = 1;
            continue;
        }

        if (strcmp(a, "-m") == 0) {
            spiflash_mount();
            continue;
        }

        if (strcmp(a, "-l") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -l requires a decimal value(byte)\n");
                goto Usage;
            }
            char *end = NULL;

            size = strtol(argv[++i], &end, 10);
            if (*end != '\0' || size < 0) {
                rt_kprintf("Error: -l requires a decimal numeric-only and positive parameter\n");
                goto Usage;
            }
            continue;
        }

        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -o(offset) requires a decimal value: offset in sector\n");
                goto Usage;
            }
            char *end = NULL;
            int   tmp = strtol(argv[++i], &end, 10);

            if (*end != '\0' || tmp < 0) {
                rt_kprintf("Error: -o(offset) requires a decimal value: offset in sector\n");
                goto Usage;
            }
            offset = tmp;
            continue;
        }

        rt_kprintf("Error: unknown option: %s\n", a);
        goto Usage;
    }

    int result = 0;
    int ret;

    if (!is_test_all) {
        result = spif_ewr_test(size, offset);
    } else if (is_test_all) {
        rt_kprintf("==== All Test Start: ====\n");
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[1]init test start:\n");
        fal_show_part_table();
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[2]spif ewr test start:\n");
        ret = spif_ewr_test(size, offset);
        rt_kprintf("[2]spif ewr test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[3]spif boundary ewr test start:\n");
        ret = spif_ewr_boundary_test();
        rt_kprintf("[3]spif boundary ewr test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[4]spif multi-length data ewr test start:\n");
        ret = spif_multi_length_ewr_test();
        rt_kprintf("[4]spif multi-length data ewr test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[5]spif multi-offset ewr test start:\n");
        ret = spif_multi_offset_ewr_test();
        rt_kprintf("[5]spif multi-offset ewr test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[6]spif full partition ewr test start:\n");
        ret = spif_full_partition_ewr_test();
        rt_kprintf("[6]spif full partition ewr test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[7]spif data pattern sensitivity test start\n");
        ret = spif_data_pattern_sensitivity_test();
        rt_kprintf("[7]spif data pattern sensitivity test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("[8]spif ewr stability test start\n");
        ret = spif_ewr_stability_test();
        rt_kprintf("[8]spif ewr stability test %s\n", ret ? "failed" : "passed");
        result |= ret;
        rt_kprintf("------------------------------------\n");
        rt_kprintf("==== All Test Finished ====\n");
    }

    return result;

Usage:
    rt_kprintf("Usage:\n");
    rt_kprintf("spinor flash test:\n");
    rt_kprintf("	-a: start all test\n");
    rt_kprintf("	-m: mount prim to /mnt/data (elm)\n");
    rt_kprintf(
        "	-l: set test size (decimal) in byte (only effective for basic ewr(erase/write/read) "
        "test)\n");
    rt_kprintf("	-o: set offset (only effective for basic ewr(erase/write/read) test)\n");
    return 1;
}
MSH_CMD_EXPORT(test_spif, Run FAL Flash test);
