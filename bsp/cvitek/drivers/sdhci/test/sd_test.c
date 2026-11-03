#include <dfs_fs.h>
#include <finsh.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <stdlib.h>

#define SD_TEST_FILE     "/mnt/sd/test_file.bin"
#define TEST_BLOCK_SIZE  512
#define TEST_BLOCK_COUNT 10

int random_range(int min, int max)
{
    static rt_uint32_t seed;

    if (seed == 0) {
        seed = rt_tick_get();
    }
    return (rand_r(&seed) % (max - min + 1)) + min;
}
static int sd_test_read_write(void)
{
    int         fd        = -1;
    rt_uint8_t *write_buf = RT_NULL;
    rt_uint8_t *read_buf  = RT_NULL;
    rt_size_t   written, readlen;
    int         i, result = 0;

    write_buf = rt_malloc(TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);
    read_buf  = rt_malloc(TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);

    if (!write_buf || !read_buf) {
        rt_kprintf("[FAIL] Memory allocation failed!\n");
        result = -1;
        goto exit;
    }

    for (i = 0; i < TEST_BLOCK_SIZE * TEST_BLOCK_COUNT; i++) {
        if (i % TEST_BLOCK_SIZE <= 0xff)
            write_buf[i] = i & 0xFF;
        else
            write_buf[i] = (0xff - i) & 0xFF;
    }

    fd = creat(SD_TEST_FILE, 0);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for writing\n");
        result = -1;
        goto exit;
    }

    written = write(fd, write_buf, TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);
    if (written != TEST_BLOCK_SIZE * TEST_BLOCK_COUNT) {
        rt_kprintf("[FAIL] Write failed! Written: %d, Expected: %d\n", written,
                   TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);
        result = -1;
        close(fd);
        goto exit;
    }
    rt_kprintf("file size: %d\n", lseek(fd, 0, SEEK_END));
    close(fd);

    fd = open(SD_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for reading\n");
        result = -1;
        goto exit;
    }

    readlen = read(fd, read_buf, TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);
    if (readlen != TEST_BLOCK_SIZE * TEST_BLOCK_COUNT) {
        rt_kprintf("[FAIL] Read failed! Read: %d, Expected: %d\n", readlen,
                   TEST_BLOCK_SIZE * TEST_BLOCK_COUNT);
        result = -1;
        close(fd);
        goto exit;
    }
    close(fd);

    for (i = 0; i < TEST_BLOCK_SIZE * TEST_BLOCK_COUNT; i++) {
        if (read_buf[i] != write_buf[i]) {
            rt_kprintf("[FAIL] Data mismatch at position %d: %02X vs %02X\n", i, read_buf[i],
                       write_buf[i]);
            result = -1;
            break;
        }
    }

    // delete test file
    unlink(SD_TEST_FILE);

exit:
    if (write_buf) rt_free(write_buf);
    if (read_buf) rt_free(read_buf);
    return result;
}

static int sd_test_performance(void)
{
    int         fd  = -1;
    rt_uint8_t *buf = RT_NULL;
    rt_tick_t   start, end;
    rt_size_t   size = 1024 * 1024;  // 1MB
    float       time_sec, speed;
    char        print_buf[128] = {0};
    int         result         = 0;

    buf = rt_malloc(size);
    if (!buf) {
        rt_kprintf("[FAIL] Memory allocation failed!\n");
        result = -1;
        goto exit;
    }
    for (int i = 0; i < size; i++) {
        buf[i] = i & 0xFF;
    }
    fd = creat(SD_TEST_FILE, 0);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for writing\n");
        result = -1;
        goto exit;
    }

    start             = rt_tick_get();
    rt_size_t written = write(fd, buf, size);

    end = rt_tick_get();
    close(fd);

    if (written != size) {
        rt_kprintf("[FAIL] Write performance test failed\n");
        result = -1;
        goto exit;
    }

    time_sec = (end - start) * 1.0 / RT_TICK_PER_SECOND;
    speed    = size / (time_sec * 1024 * 1024);  // MB/s

    snprintf(print_buf, sizeof(print_buf), "Write speed: %.2f MB/s", speed);
    rt_kprintf("%s\n", print_buf);

    fd = open(SD_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for reading\n");
        result = -1;
        goto exit;
    }

    start             = rt_tick_get();
    rt_size_t readlen = read(fd, buf, size);

    end = rt_tick_get();
    close(fd);

    if (readlen != size) {
        rt_kprintf("[FAIL] Read performance test failed\n");
        result = -1;
        goto exit;
    }

    time_sec = (end - start) * 1.0 / RT_TICK_PER_SECOND;
    speed    = size / (time_sec * 1024 * 1024);  // MB/s
    memset(print_buf, 0, sizeof(print_buf));
    snprintf(print_buf, sizeof(print_buf), "Read speed: %.2f MB/s", speed);
    rt_kprintf("%s\n", print_buf);

exit:
    if (buf) rt_free(buf);
    unlink(SD_TEST_FILE);
    return result;
}

#define SD_DIR       "/mnt/sd"
#define TEST_DIR     SD_DIR "/test_dir"
#define TEST_FILE    "/test_file.txt"
#define RENAMED_DIR  SD_DIR "/renamed_dir"
#define RENAMED_FILE "/renamed_file.txt"

// //1. Execute shell command and check return value
static int execute_command(const char *cmd)
{
    int ret = -1;

    rt_kprintf("\n> %s\n", cmd);  // print the command

    // Execute shell command and check return value
    ret = msh_exec(cmd, rt_strlen(cmd));
    if (ret != 0) {
        rt_kprintf("[ERROR] Command failed: %s (ret=%d)\n", cmd, ret);
        return -1;
    }

    return 0;
}

static void sd_file_system_test(void)
{
    char cmd[128] = {0};
    // //0. check if sdcard is mounted
    // if (access("/sdcard", F_OK) != 0)
    // {
    //	 rt_kprintf("[ERROR] /sdcard not mounted! Run 'mount sd0 /sdcard' first.\n");
    //	 return;
    // }
    // rt_kprintf("[1] /sdcard mounted OK\n");

    // 1. Create a test directory
    rt_kprintf("\n(1) Directory created: %s\n", TEST_DIR);
    rt_snprintf(cmd, sizeof(cmd), "mkdir %s", TEST_DIR);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", SD_DIR);
    if (execute_command(cmd) != 0) goto cleanup;

    // 2. Create a test file and write content
    rt_kprintf("\n(2) File created: %s\n", TEST_DIR TEST_FILE);
    rt_snprintf(cmd, sizeof(cmd), "echo \"!!!SD file system test!!!\" %s", TEST_DIR TEST_FILE);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", TEST_DIR);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "cat %s", TEST_DIR TEST_FILE);
    if (execute_command(cmd) != 0) goto cleanup;

    // 3. Rename the test directory
    rt_kprintf("\n(3) Directory renamed: %s -> %s\n", TEST_DIR, RENAMED_DIR);
    rt_snprintf(cmd, sizeof(cmd), "mv %s %s", TEST_DIR, RENAMED_DIR);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", SD_DIR);
    if (execute_command(cmd) != 0) goto cleanup;

    // 4. Rename the test file
    rt_kprintf("\n(4) File renamed: %s -> %s\n", RENAMED_DIR TEST_FILE, RENAMED_DIR RENAMED_FILE);
    rt_snprintf(cmd, sizeof(cmd), "mv %s %s", RENAMED_DIR TEST_FILE, RENAMED_DIR RENAMED_FILE);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", RENAMED_DIR);
    if (execute_command(cmd) != 0) goto cleanup;

    // 5. Verify the file content
    rt_kprintf("\n(5) File content:\n");
    rt_snprintf(cmd, sizeof(cmd), "cat %s", RENAMED_DIR RENAMED_FILE);
    if (execute_command(cmd) != 0) goto cleanup;

    // 6. Delete the test file
    rt_kprintf("\n(6) File deleted: %s\n", RENAMED_DIR RENAMED_FILE);
    rt_snprintf(cmd, sizeof(cmd), "rm %s", RENAMED_DIR RENAMED_FILE);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", RENAMED_DIR);
    if (execute_command(cmd) != 0) goto cleanup;

    // 7. Delete the test directory
    rt_kprintf("\n(7) Directory deleted: %s\n", RENAMED_DIR);
    rt_snprintf(cmd, sizeof(cmd), "rm -r %s", RENAMED_DIR);
    if (execute_command(cmd) != 0) goto cleanup;
    rt_snprintf(cmd, sizeof(cmd), "ls %s", SD_DIR);
    if (execute_command(cmd) != 0) goto cleanup;

    return;

cleanup:
    rt_kprintf("[TEST FAILED]\n");
    rt_snprintf(cmd, sizeof(cmd), "rm -r %s", RENAMED_DIR);
    execute_command(cmd);
    rt_snprintf(cmd, sizeof(cmd), "rm -r %s", TEST_DIR);
    execute_command(cmd);
}

#define FILE_NUM    5
#define TEST_CYCLES 20
#define BUF_SIZE    128
struct thread_param {
    int   id;
    char  path[32];  // file path
    char *w_buf;
    char *r_buf;
};

static rt_mutex_t sd_mutex = RT_NULL;

static void thread_rw_entry(void *parameter)
{
    struct thread_param *param  = (struct thread_param *)parameter;
    int                  fd     = -1;
    int                  result = 0;
    rt_size_t            written, readlen;

    rt_kprintf("thread: %d, %s start\n", param->id, param->path);
    for (int i = 0; i < TEST_CYCLES; i++) {
        rt_memset(param->r_buf, 0, BUF_SIZE);
        rt_mutex_take(sd_mutex, RT_WAITING_FOREVER);
        fd = creat(param->path, 0);
        if (fd < 0) {
            rt_kprintf("[FAIL] thread: %d, Failed to open file for writing\n", param->id);
            result = -1;
            goto exit;
        }

        written = write(fd, param->w_buf, BUF_SIZE);
        if (written != BUF_SIZE) {
            rt_kprintf("[FAIL] thread: %d, Write failed! Written: %d, Expected: %d\n", param->id,
                       written, BUF_SIZE);
            result = -1;
            close(fd);
            fd = -1;
            goto exit;
        }

        close(fd);
        fd = -1;

        fd = open(param->path, O_RDONLY);

        if (fd < 0) {
            rt_kprintf("[FAIL] Failed to open file for reading\n");
            result = -1;
            goto exit;
        }

        readlen = read(fd, param->r_buf, BUF_SIZE);

        if (readlen != BUF_SIZE) {
            rt_kprintf("[FAIL] thread: %d, Read failed! Read: %d, Expected: %d\n", param->id,
                       readlen, BUF_SIZE);
            result = -1;
            close(fd);
            fd = -1;
            goto exit;
        }

        close(fd);
        fd = -1;
        rt_mutex_release(sd_mutex);

        for (int i = 0; i < BUF_SIZE; i++) {
            if (param->w_buf[i] != param->r_buf[i]) {
                rt_kprintf("[FAIL] thread: %d, Data mismatch at position %d: %02X vs %02X\n",
                           param->id, i, param->w_buf[i], param->r_buf[i]);
                result = -1;
                goto exit;
            }
        }
        rt_thread_mdelay(10);
    }

exit:
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
    unlink(param->path);

    rt_free(param->w_buf);
    rt_free(param->r_buf);
    rt_free(param);
    rt_kprintf("thread: %d test [%s], exit\n", param->id, result == 0 ? "passed" : "failed");
}

static int test_sd_concurrent_rw(void)
{
    rt_thread_t          tid[FILE_NUM];
    struct thread_param *param;
    int                  result = 0;

    sd_mutex = rt_mutex_create("sd_mutex", RT_IPC_FLAG_FIFO);
    if (sd_mutex == RT_NULL) {
        rt_kprintf("Failed to create mutex!\n");
        result = -1;
        goto exit;
    }
    // Create FILE_NUM threads
    for (int i = 0; i < FILE_NUM; i++) {
        char thread_name[16];

        param = rt_malloc(sizeof(struct thread_param));
        if (param == RT_NULL) {
            rt_kprintf("Thread %d malloc failed!\n", i);
            result = -1;
            goto exit;
        }
        param->id    = i;
        param->w_buf = rt_malloc(BUF_SIZE);
        param->r_buf = rt_malloc(BUF_SIZE);
        if (param->w_buf == RT_NULL || param->r_buf == RT_NULL) {
            rt_kprintf("Thread %d malloc failed!\n", i);
            rt_free(param->w_buf);
            rt_free(param->r_buf);
            rt_free(param);
            result = -1;
            goto exit;
        }
        for (int j = 0; j < BUF_SIZE; j++) {
            param->w_buf[j] = (j + i * 10) & 0xff;
        }
        rt_snprintf(param->path, sizeof(param->path), "/mnt/sd/test%d.txt", i);
        rt_snprintf(thread_name, sizeof(thread_name), "sd_thread%d", i);
        tid[i] = rt_thread_create(thread_name, thread_rw_entry, param, 8192,
                                  RT_THREAD_PRIORITY_MAX / 2, 20);
        rt_kprintf("thread: %d, create\n", i);
    }

    for (int i = 0; i < FILE_NUM; i++) {
        if (tid[i] != RT_NULL) {
            rt_thread_startup(tid[i]);
        } else {
            rt_kprintf("Thread %d create failed!\n", i);
            rt_free(param->w_buf);
            rt_free(param->r_buf);
            rt_free(param);
            result = -1;
            goto exit;
        }
    }

    for (int i = 0; i < FILE_NUM; i++) {
        if (tid[i] != RT_NULL) {
            while (rt_thread_find(tid[i]->parent.name) != RT_NULL) rt_thread_mdelay(100);
        }
    }

exit:
    if (sd_mutex != RT_NULL) {
        rt_mutex_delete(sd_mutex);
        sd_mutex = RT_NULL;
    }
    return result;
}

static int sd_random_access_test(void)
{
    int         fd        = -1;
    rt_uint8_t *write_buf = RT_NULL;
    rt_uint8_t *read_buf  = RT_NULL;
    rt_size_t   written, readlen;
    int         result = 0;
    int         file_size, random_offset, random_size;
    int         test_cycles   = 5;
    int         set_file_size = TEST_BLOCK_SIZE * TEST_BLOCK_COUNT * 2;

    // Create a 10KB file
    write_buf = rt_malloc(set_file_size);
    read_buf  = rt_malloc(set_file_size);

    if (!write_buf || !read_buf) {
        rt_kprintf("[FAIL] Memory allocation failed!\n");
        goto exit;
    }

    fd = creat(SD_TEST_FILE, 0);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for writing\n");
        result = -1;
        goto exit;
    }

    written = write(fd, write_buf, set_file_size);
    if (written != set_file_size) {
        rt_kprintf("[FAIL] Write failed! Written: %d, Expected: %d\n", written, set_file_size);
        result = -1;
        close(fd);
        goto exit;
    }
    // Get file size
    file_size = lseek(fd, 0, SEEK_END);
    rt_kprintf("File size: %d\n", file_size);
    close(fd);

    // Random access test
    for (int i = 0; i < test_cycles; i++) {
        fd = creat(SD_TEST_FILE, 0);
        if (fd < 0) {
            rt_kprintf("[FAIL] Failed to open file for reading\n");
            result = -1;
            goto exit;
        }

        // Random access
        random_offset = random_range(0, file_size - 1);
        random_size   = random_range(0, file_size - random_offset);
        rt_kprintf("Random access test [%d], offset: %d, size: %d ", i + 1, random_offset,
                   random_size);
        // Fill write buff
        for (int j = 0; j < random_size; j++) {
            write_buf[j] = (j) & 0xff;
        }
        lseek(fd, random_offset, SEEK_SET);
        written = write(fd, write_buf, random_size);
        if (written != random_size) {
            rt_kprintf("[FAIL] Write failed! Written: %d, Expected: %d\n", written, random_size);
            result = -1;
            close(fd);
            goto exit;
        }
        close(fd);

        fd = open(SD_TEST_FILE, O_RDONLY);
        if (fd < 0) {
            rt_kprintf("[FAIL] Failed to open file for reading\n");
            result = -1;
            goto exit;
        }
        lseek(fd, random_offset, SEEK_SET);
        readlen = read(fd, read_buf, random_size);
        if (readlen != random_size) {
            rt_kprintf("[FAIL] Read failed! Read: %d, Expected: %d\n", readlen, random_size);
            result = -1;
            close(fd);
            goto exit;
        }
        close(fd);

        for (int k = 0; k < random_size; k++) {
            if (read_buf[k] != write_buf[k]) {
                rt_kprintf("[FAIL] Data mismatch at position %d: %02X vs %02X\n", k, read_buf[k],
                           write_buf[k]);
                result = -1;
                break;
            }
        }
        rt_kprintf("[passed]\n");
    }
    // delete test file
    unlink(SD_TEST_FILE);

exit:
    if (write_buf) rt_free(write_buf);
    if (read_buf) rt_free(read_buf);
    return result;
}

static int sd_stress_test(int cycles)
{
    int         fd        = -1;
    rt_uint8_t *write_buf = RT_NULL;
    rt_uint8_t *read_buf  = RT_NULL;
    rt_size_t   written, readlen;
    rt_tick_t   start, end;
    float       time_sec, speed;
    int         i, result = 0;
    int         size_max       = 1024 * 1024 * 3;
    char        print_buf[128] = {0};

    for (i = 0; i < cycles; i++) {
        int size = random_range(0, size_max);

        rt_kprintf("----------------------------------\n");
        rt_kprintf("Stree test (%d) start: test size: %d\n", i, size);
        write_buf = rt_malloc(size);
        read_buf  = rt_malloc(size);

        if (!write_buf || !read_buf) {
            rt_kprintf("[FAIL] Memory allocation failed!\n");
            result = -1;
            goto exit;
        }

        for (int k = 0; k < size; k++) {
            write_buf[k] = random_range(0, 0xff);
        }

        fd = creat(SD_TEST_FILE, 0);
        if (fd < 0) {
            rt_kprintf("[FAIL] Failed to open file for writing\n");
            result = -1;
            goto exit;
        }
        start   = rt_tick_get();
        written = write(fd, write_buf, size);
        end     = rt_tick_get();

        if (written != size) {
            rt_kprintf("[FAIL] Write failed! Written: %d, Expected: %d\n", written, size);
            result = -1;
            close(fd);
            goto exit;
        }
        close(fd);
        time_sec = (end - start) * 1.0 / RT_TICK_PER_SECOND;
        speed    = size / (time_sec * 1024 * 1024);  // MB/s

        memset(print_buf, 0, sizeof(print_buf));
        snprintf(print_buf, sizeof(print_buf), "Write speed: %.2f MB/s", speed);
        rt_kprintf("%s\n", print_buf);

        fd = open(SD_TEST_FILE, O_RDONLY);
        if (fd < 0) {
            rt_kprintf("[FAIL] Failed to open file for reading\n");
            result = -1;
            goto exit;
        }

        start   = rt_tick_get();
        readlen = read(fd, read_buf, size);
        end     = rt_tick_get();

        if (readlen != size) {
            rt_kprintf("[FAIL] Read failed! Read: %d, Expected: %d\n", readlen, size);
            result = -1;
            close(fd);
            goto exit;
        }
        close(fd);
        fd       = -1;
        time_sec = (end - start) * 1.0 / RT_TICK_PER_SECOND;
        speed    = size / (time_sec * 1024 * 1024);  // MB/s
        memset(print_buf, 0, sizeof(print_buf));
        snprintf(print_buf, sizeof(print_buf), "Read speed: %.2f MB/s", speed);
        rt_kprintf("%s\n", print_buf);

        for (int j = 0; j < size; j++) {
            if (read_buf[j] != write_buf[j]) {
                rt_kprintf("[FAIL] Data mismatch at position %d: %02X vs %02X\n", j, read_buf[j],
                           write_buf[j]);
                result = -1;
                break;
            }
        }
        rt_free(write_buf);
        rt_free(read_buf);
        write_buf = RT_NULL;
        read_buf  = RT_NULL;
        // delete test file
        unlink(SD_TEST_FILE);
    }
exit:
    if (i != cycles) {
        rt_kprintf("[FAIL] sd stress test failed, current cycle: %d\n", i);
        result = -1;
    }
    if (write_buf) rt_free(write_buf);
    if (read_buf) rt_free(read_buf);
    return result;
}

static int sd_abnormal_operation_test(void)
{
    int   result            = 0;
    int   fd                = -1;
    char  cmd[128]          = {0};
    char *non_existent_file = "/mnt/sd/non_existent_file.txt";
    char *readonly_file     = "/mnt/sd/readonly.txt";

    // 1. Test opening a non-existent file
    rt_kprintf("\n(1) opening a non-existent file: %s\n", non_existent_file);
    rt_snprintf(cmd, sizeof(cmd), "cat %s", non_existent_file);
    execute_command(cmd);

    // TODO: for elm, read-only seems not work
    //  2. Test writing to a read-only file system
    rt_kprintf("\n(2) opening a read-only file: %s\n", readonly_file);
    fd = creat(readonly_file, 0444);
    if (fd < 0) {
        rt_kprintf("[FAIL] Failed to open file for writing to read-only file system\n");
        result = -1;
        close(fd);
    }
    close(fd);
    rt_snprintf(cmd, sizeof(cmd), "echo \"test\"  %s", readonly_file);
    execute_command(cmd);
    rt_snprintf(cmd, sizeof(cmd), "cat %s", readonly_file);
    execute_command(cmd);

    // 3. Test writing to a full disk
    // TODO:

    // delete readonly file
    unlink(readonly_file);
    return result;
}

int sd_card_test(int argc, char *argv[])
{
    uint8_t has_stress_test = 0;
    int     stress_cycles   = 20;
    int     result          = 0;
    int     ret;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        if (strcmp(a, "-s") == 0) {
            has_stress_test = 1;
            continue;
        }

        if (strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                rt_kprintf("Error: -c(stress test cycles) requires a value: test cycles\n");
                goto Usage;
            }
            char *end = NULL;
            int   tmp = strtol(argv[++i], &end, 10);

            if (*end != '\0' || tmp < 0 || tmp > 65536) {
                rt_kprintf("Error: -c(stress test cycles) requires a value: 1~65535\n");
                goto Usage;
            }
            stress_cycles = tmp;
            continue;
        }

        rt_kprintf("Error: unknown option: %s\n", a);
        goto Usage;
    }
    // rt_kprintf("Unmounting SD card...\n");
    // if (dfs_unmount("/")) {
    //	 rt_kprintf("Failed to unmount SD card!");
    //	 return;
    // }
    // rt_kprintf("SD card Unmounted successfully");

    // rt_kprintf("Mounting SD card...\n");
    // if (dfs_mount("sd0", "/", "elm", 0, 0) != 0) {
    //	 rt_kprintf("Failed to mount SD card!");
    //	 return;
    // }

    // rt_kprintf("SD card mounted successfully");
    rt_kprintf("[1]sd_test_read_write start:\n");
    ret = sd_test_read_write();
    rt_kprintf("[1]sd_test_read_write [%s]\n", ret == 0 ? "pass" : "fail");
    result |= ret;
    rt_kprintf("----------------------------------\n");
    rt_kprintf("[2]sd performance test start:\n");
    ret = sd_test_performance();
    rt_kprintf("[2]sd performance test [%s]\n", ret == 0 ? "pass" : "fail");
    result |= ret;
    rt_kprintf("----------------------------------\n");
    rt_kprintf("[3]sd_file_system_test start:\n");
    rt_kprintf("*** Please manually determine whether the test passed. ***\n");
    // For default: elm
    sd_file_system_test();
    rt_kprintf("[3]sd_file_system_test end\n");
    rt_kprintf("----------------------------------\n");
    rt_kprintf("[4]sd random access test start:\n");
    ret = sd_random_access_test();
    rt_kprintf("[4]sd random access test [%s]\n", ret == 0 ? "pass" : "fail");
    result |= ret;
    rt_kprintf("----------------------------------\n");
    rt_kprintf("[5]sd concurrent rw test start:\n");
    ret = test_sd_concurrent_rw();
    rt_kprintf("[5]sd concurrent rw test [%s]\n", ret == 0 ? "pass" : "fail");
    result |= ret;
    rt_kprintf("----------------------------------\n");
    rt_kprintf("[6]sd abnormal operation test start:\n");
    rt_kprintf("*** Please manually determine whether the test passed. ***\n");
    sd_abnormal_operation_test();
    rt_kprintf("[6]sd abnormal operation test end\n");

    if (has_stress_test) {
        rt_kprintf("----------------------------------\n");
        rt_kprintf("[7]sd stress test start:\n");
        ret = sd_stress_test(stress_cycles);
        rt_kprintf("[7]sd stress test [%s]\n", ret == 0 ? "pass" : "fail");
        result |= ret;
        rt_kprintf("----------------------------------\n");
    }

    return result;

Usage:
    rt_kprintf("Usage:\n");
    rt_kprintf("sd_card_test [-s] [-c <cycles>]\n");
    rt_kprintf("	-s: enable stress test\n");
    rt_kprintf("	-c: stress test cycles, default: 20 (only valid when -s enabled)\n");
    rt_kprintf("Example:\n");
    rt_kprintf("	sd_card_test -s -c 100\n");
    rt_kprintf(
        "	which means next test includes normal test and stress test, and stress test will "
        "continue for 100 rounds.\n");
    return 1;
}

MSH_CMD_EXPORT(sd_card_test, Test SD card);
