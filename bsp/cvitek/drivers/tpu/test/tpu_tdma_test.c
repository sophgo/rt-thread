// #include "aos/kernel.h"
#include <pthread.h>
#include <rtdbg.h>
#include <rtthread.h>
#include <semaphore.h>
#include <stdlib.h>

#include "drv_tpu.h"
#include "hal_barrier.h"
#include "hal_tpu.h"
#define TEST_NUM 20

int cvi_tpu_tdma_test_multi_times(void)
{
    struct cvi_tpu_device *ndev = NULL;

    ndev = cvi_tpu_open();
    if (ndev == NULL) {
        rt_kprintf("open tpu device failed!\r\n");
        return -1;
    }

    rt_kprintf("tdmaBaseAddr = 0x%x , tiuBaseAddr = 0x%x\r\n", ndev->tdma_paddr, ndev->tiu_paddr);

    char                    *paddr_src[TEST_NUM];
    char                    *paddr_dst[TEST_NUM];
    struct cvi_tdma_copy_arg pioCfg[TEST_NUM];
    struct cvi_cache_op_arg  opCfg;
    char                    *p, *q;
    for (uint32_t pioCfgIndex = 0; pioCfgIndex < TEST_NUM; pioCfgIndex++) {
        pioCfg[pioCfgIndex].enable_2d  = 0;
        pioCfg[pioCfgIndex].seq_no     = pioCfgIndex;
        pioCfg[pioCfgIndex].leng_bytes = 1024;
        paddr_src[pioCfgIndex]         = (char *)rt_malloc(pioCfg[pioCfgIndex].leng_bytes + 0x3f);
        paddr_dst[pioCfgIndex]         = (char *)rt_malloc(pioCfg[pioCfgIndex].leng_bytes + 0x3f);
        if (paddr_dst[pioCfgIndex] == NULL || paddr_src[pioCfgIndex] == NULL) {
            rt_kprintf("rt_malloc src/dst failed!\r\n");
            return -1;
        }
        pioCfg[pioCfgIndex].paddr_dst = (uintptr_t)paddr_dst[pioCfgIndex];
        pioCfg[pioCfgIndex].paddr_src = (uintptr_t)paddr_src[pioCfgIndex];
        // 64 byte align
        pioCfg[pioCfgIndex].paddr_dst = (pioCfg[pioCfgIndex].paddr_dst + 0x3f) & (~0x3f);
        pioCfg[pioCfgIndex].paddr_src = (pioCfg[pioCfgIndex].paddr_src + 0x3f) & (~0x3f);
        p                             = (char *)pioCfg[pioCfgIndex].paddr_src;
        for (uint32_t tmpIndex = 0; tmpIndex < pioCfg[pioCfgIndex].leng_bytes; tmpIndex++) {
            p[tmpIndex] = 'A' + pioCfgIndex;
        }

        // tpu cache flush
        opCfg.paddr = pioCfg[pioCfgIndex].paddr_src;
        opCfg.size  = pioCfg[pioCfgIndex].leng_bytes;
        cvi_tpu_ioctl(ndev, CVITPU_DMABUF_FLUSH, (unsigned long)&opCfg);

        opCfg.paddr = pioCfg[pioCfgIndex].paddr_dst;
        opCfg.size  = pioCfg[pioCfgIndex].leng_bytes;
        cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);

        // tpu tdma process
        rt_kprintf("platform_run_pio()\r\n");
        cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&(pioCfg[pioCfgIndex]));
    }
    rt_kprintf("CVITPU_WAIT_PIO\r\n");

    // wait for pio done
    int                      result  = 0;
    struct cvi_tdma_wait_arg waitCfg = {.seq_no = 1, .ret = 0};
    for (uint32_t pioCfgIndex = 0; pioCfgIndex < TEST_NUM; pioCfgIndex++) {
        waitCfg.seq_no = pioCfgIndex;
        waitCfg.ret    = 0;
        rt_kprintf("CVITPU_WAIT_PIO\r\n");
        cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);
        // tpu release
        // tpu dma invalidate
        opCfg.paddr = pioCfg[pioCfgIndex].paddr_dst;
        opCfg.size  = pioCfg[pioCfgIndex].leng_bytes;

        rt_kprintf("CVITPU_DMABUF_INVLD\r\n");
        cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);
        rt_kprintf("CVITPU_DMABUF_INVLD end\r\n");
        // printf dst data
        p = (char *)pioCfg[pioCfgIndex].paddr_src;
        q = (char *)pioCfg[pioCfgIndex].paddr_dst;

        for (uint32_t tmpIndex = 0; tmpIndex < pioCfg[pioCfgIndex].leng_bytes; tmpIndex++) {
            if (p[tmpIndex] != q[tmpIndex]) {
                rt_kprintf("[%s %d] [%d\t%d]:%d->%d error\r\n", __FUNCTION__, __LINE__, pioCfgIndex,
                           tmpIndex, p[tmpIndex], q[tmpIndex]);
                result = 1;
            } else {
                rt_kprintf("[%s %d] [%d\t%d]:%d->%d success\r\n", __FUNCTION__, __LINE__,
                           pioCfgIndex, tmpIndex, p[tmpIndex], q[tmpIndex]);
            }
        }
        rt_kprintf("\r\n");
    }
    if (result)
        rt_kprintf("test error\r\n");
    else
        rt_kprintf("test success\r\n");
    for (uint32_t pioCfgIndex = 0; pioCfgIndex < TEST_NUM; pioCfgIndex++) {
        if (NULL != paddr_src[pioCfgIndex]) rt_free(paddr_src[pioCfgIndex]);
        if (NULL != paddr_dst[pioCfgIndex]) rt_free(paddr_dst[pioCfgIndex]);
    }
    cvi_tpu_close();

    return 0;
}

int cvi_tpu_tdma_test_once(void)
{
    struct cvi_tpu_device *ndev = NULL;

    ndev = cvi_tpu_open();
    if (ndev == NULL) {
        rt_kprintf("open tpu device failed!\r\n");
        return -1;
    }
    rt_kprintf("tdmaBaseAddr = 0x%lx , tiuBaseAddr = 0x%lx\r\n", ndev->tdma_paddr, ndev->tiu_paddr);

    // set pio cfg
    struct cvi_tdma_copy_arg pioCfg;
    pioCfg.enable_2d  = 0;
    pioCfg.seq_no     = 1;
    pioCfg.leng_bytes = 1024;
    char *paddr_src   = (char *)rt_malloc(pioCfg.leng_bytes + 0x3f);
    char *paddr_dst   = (char *)rt_malloc(pioCfg.leng_bytes + 0x3f);
    rt_kprintf("[%s %d] paddr_src=0x%x  paddr_dst=0x%x\r\n", __FUNCTION__, __LINE__, paddr_src,
               paddr_dst);

    if (paddr_dst == NULL || paddr_src == NULL) {
        rt_kprintf("rt_malloc src/dst failed!\r\n");
        return -1;
    }
    pioCfg.paddr_dst = (uintptr_t)paddr_dst;
    pioCfg.paddr_src = (uintptr_t)paddr_src;
    // 64 byte align
    pioCfg.paddr_dst = (pioCfg.paddr_dst + 0x3f) & (~0x3f);
    pioCfg.paddr_src = (pioCfg.paddr_src + 0x3f) & (~0x3f);
    rt_kprintf("[%s %d] pioCfg.paddr_src=0x%x  pioCfg.paddr_dst=0x%x\r\n", __FUNCTION__, __LINE__,
               pioCfg.paddr_src, pioCfg.paddr_dst);
    char *p = (char *)pioCfg.paddr_src;
    for (uint32_t tmpIndex = 0; tmpIndex < pioCfg.leng_bytes; tmpIndex++) {
        p[tmpIndex] = tmpIndex % 200;
    }

    // tpu cache flush
    struct cvi_cache_op_arg opCfg;
    opCfg.paddr = pioCfg.paddr_src;
    opCfg.size  = pioCfg.leng_bytes;
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_FLUSH, (unsigned long)&opCfg);
    opCfg.paddr = pioCfg.paddr_dst;
    opCfg.size  = pioCfg.leng_bytes;
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);

    // tpu tdma process
    rt_kprintf("CVITPU_SUBMIT_PIO\r\n");
    cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&pioCfg);
    // wait for pio done
    struct cvi_tdma_wait_arg waitCfg = {.seq_no = 1, .ret = 0};
    rt_kprintf("CVITPU_WAIT_PIO\r\n");
    cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);
    // tpu release
    // tpu dma invalidate
    opCfg.paddr = pioCfg.paddr_dst;
    opCfg.size  = pioCfg.leng_bytes;
    rt_kprintf("CVITPU_DMABUF_INVLD\r\n");
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);
    // printf dst data
    char *q    = (char *)pioCfg.paddr_src;
    p          = (char *)pioCfg.paddr_dst;
    int result = 0;
    for (uint32_t tmpIndex = 0; tmpIndex < pioCfg.leng_bytes; tmpIndex++) {
        if (q[tmpIndex] != p[tmpIndex]) result = 1;
        rt_kprintf("[%d]:%d->%d\t", tmpIndex, q[tmpIndex], p[tmpIndex]);
        if (tmpIndex % 5 == 0) rt_kprintf("\r\n");
    }
    if (result)
        rt_kprintf("\r\ntest error\r\n");
    else
        rt_kprintf("\r\ntest success\r\n");
    if (NULL != paddr_src) rt_free(paddr_src);
    if (NULL != paddr_dst) rt_free(paddr_dst);
    cvi_tpu_close();
    return 0;
}

int cvi_tpu_tdma_test_2d(void)
{
    struct cvi_tpu_device *ndev = NULL;

    ndev = cvi_tpu_open();
    if (ndev == NULL) {
        rt_kprintf("open tpu device failed!\r\n");
        return -1;
    }
    rt_kprintf("tdmaBaseAddr = 0x%lx, tiuBaseAddr = 0x%lx\r\n", ndev->tdma_paddr, ndev->tiu_paddr);

    uint32_t h = 16;
    uint32_t w_bytes = 32;
    uint32_t stride_bytes_src = 48;
    uint32_t stride_bytes_dst = 64;

    uint32_t src_size = h * stride_bytes_src;
    uint32_t dst_size = h * stride_bytes_dst;
    char *paddr_src   = (char *)rt_malloc(src_size + 0x3f);
    char *paddr_dst   = (char *)rt_malloc(dst_size + 0x3f);

    if (paddr_src == NULL || paddr_dst == NULL) {
        rt_kprintf("rt_malloc src/dst failed!\r\n");
        if (paddr_src) rt_free(paddr_src);
        if (paddr_dst) rt_free(paddr_dst);
        cvi_tpu_close();
        return -1;
    }

    struct cvi_tdma_copy_arg pioCfg;
    pioCfg.paddr_src = (uintptr_t)paddr_src;
    pioCfg.paddr_dst = (uintptr_t)paddr_dst;
    pioCfg.paddr_src = (pioCfg.paddr_src + 0x3f) & (~0x3f);
    pioCfg.paddr_dst = (pioCfg.paddr_dst + 0x3f) & (~0x3f);
    // Enable 2D transfer
    pioCfg.enable_2d = 1;
    pioCfg.seq_no = 3;
    pioCfg.h = h;
    pioCfg.w_bytes = w_bytes;
    pioCfg.stride_bytes_src = stride_bytes_src;
    pioCfg.stride_bytes_dst = stride_bytes_dst;

    char *p_src_aligned = (char *)pioCfg.paddr_src;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w_bytes; x++) {
            p_src_aligned[y * stride_bytes_src + x] = (y * 16 + x) % 256;
        }
    }

    struct cvi_cache_op_arg opCfg;
    opCfg.paddr = pioCfg.paddr_src;
    opCfg.size = src_size;
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_FLUSH, (unsigned long)&opCfg);

    opCfg.paddr = pioCfg.paddr_dst;
    opCfg.size = dst_size;
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);

    rt_kprintf("CVITPU_SUBMIT_PIO (2D)\r\n");
    cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&pioCfg);

    struct cvi_tdma_wait_arg waitCfg = {.seq_no = 3, .ret = 0};
    rt_kprintf("CVITPU_WAIT_PIO (2D)\r\n");
    cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);

    opCfg.paddr = pioCfg.paddr_dst;
    opCfg.size = dst_size;
    cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);

    int result = 0;
    char *p_dst_aligned = (char *)pioCfg.paddr_dst;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w_bytes; x++) {
            if (p_src_aligned[y * stride_bytes_src + x] != p_dst_aligned[y * stride_bytes_dst + x]) {
                rt_kprintf("2D check error at [h=%d, w=%d]: src(%d) != dst(%d)\r\n", y, x,
                           p_src_aligned[y * stride_bytes_src + x], p_dst_aligned[y * stride_bytes_dst + x]);
                result = 1;
                goto check_fail;
            }
        }
    }

check_fail:
    if (result)
        rt_kprintf("\r\n2D DMA test ERROR!\r\n");
    else
        rt_kprintf("\r\n2D DMA test PASSED!\r\n");

    if (NULL != paddr_src) rt_free(paddr_src);
    if (NULL != paddr_dst) rt_free(paddr_dst);
    cvi_tpu_close();
    return result;
}

int cvi_tpu_tdma_test_performance(void)
{
    int PERF_TEST_ITERATIONS = 10000;
    struct cvi_tpu_device *ndev = NULL;

    ndev = cvi_tpu_open();
    if (ndev == NULL) {
        rt_kprintf("open tpu device failed!\r\n");
        return -1;
    }

    rt_kprintf("--- TPU DMA Performance Test (Iterations: %d) ---\r\n", PERF_TEST_ITERATIONS);

    uint32_t test_sizes[] = {1 * 1024, 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        uint32_t current_size = test_sizes[i];
        char *paddr_src = (char *)rt_malloc(current_size + 0x3f);
        char *paddr_dst = (char *)rt_malloc(current_size + 0x3f);

        if (paddr_dst == NULL || paddr_src == NULL) {
            rt_kprintf("rt_malloc for size %d failed!\r\n", current_size);
            if(paddr_src) rt_free(paddr_src);
            if(paddr_dst) rt_free(paddr_dst);
            continue;
        }

        // Setup PIO
        struct cvi_tdma_copy_arg pioCfg;
        pioCfg.enable_2d  = 0;
        pioCfg.leng_bytes = current_size;
        pioCfg.paddr_dst = ((uintptr_t)paddr_dst + 0x3f) & (~0x3f);
        pioCfg.paddr_src = ((uintptr_t)paddr_src + 0x3f) & (~0x3f);

        // Cache operations (do once before loop)
        struct cvi_cache_op_arg opCfg;
        opCfg.paddr = pioCfg.paddr_src;
        opCfg.size  = pioCfg.leng_bytes;
        cvi_tpu_ioctl(ndev, CVITPU_DMABUF_FLUSH, (unsigned long)&opCfg);
        opCfg.paddr = pioCfg.paddr_dst;
        opCfg.size  = pioCfg.leng_bytes;
        cvi_tpu_ioctl(ndev, CVITPU_DMABUF_INVLD, (unsigned long)&opCfg);

        // Start timer and run DMA for multiple iterations
        rt_tick_t start_tick = rt_tick_get();

        for (int j = 0; j < PERF_TEST_ITERATIONS; j++) {
            pioCfg.seq_no = 100 + i * PERF_TEST_ITERATIONS + j;
            cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&pioCfg);
            struct cvi_tdma_wait_arg waitCfg = {.seq_no = pioCfg.seq_no, .ret = 0};
            cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);
        }

        rt_tick_t end_tick = rt_tick_get();

        // Calculate and print performance
        rt_tick_t duration_tick = end_tick - start_tick;
        if (duration_tick == 0) duration_tick = 1; // Avoid division by zero


        long long total_duration_us = ((long long)duration_tick * 1000000) / RT_TICK_PER_SECOND;
        long long avg_duration_us = total_duration_us / PERF_TEST_ITERATIONS;
        long long total_kbytes_transferred = ((long long)current_size * PERF_TEST_ITERATIONS) / 1024;
        long long bandwidth_kbs = 0;
        if (total_duration_us > 0) {
            bandwidth_kbs = (total_kbytes_transferred * 1000000) / total_duration_us;
        }

        rt_kprintf("Size: %6d KB, Avg Time: %4lu us, Bandwidth: %6lu KB/s (%lu MB/s)\r\n",
                   current_size / 1024,
                   (uint32_t)avg_duration_us,
                   (uint32_t)bandwidth_kbs,
                   (uint32_t)bandwidth_kbs / 1024);

        rt_free(paddr_src);
        rt_free(paddr_dst);
    }

    rt_kprintf("--- Performance Test End ---\r\n");
    cvi_tpu_close();
    return 0;
}

int cvi_tpu_error_handling_test(int sub_case)
{
    struct cvi_tpu_device *ndev = NULL;
    int ret = 0;
    int test_result = -1; // Default to fail

    rt_kprintf("--- TPU Error Handling and Boundary Test ---\r\n");

    ndev = cvi_tpu_open();
    if (ndev == NULL) {
        rt_kprintf("open tpu device failed!\r\n");
        return -1;
    }

    switch (sub_case) {
    case 1: {
        // --- Test 1: Null pointer argument ---
        rt_kprintf("\r\n[Test] Submitting with NULL parameter...\r\n");
        ret = cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)NULL);
        if (ret != 0) {
            rt_kprintf("  SUCCESS: Driver correctly returned error for NULL argument (ret = %d).\r\n", ret);
            test_result = 0;
        } else {
            rt_kprintf("  FAILED: Driver did not return an error for NULL argument.\r\n");
        }
        break;
    }
    case 2: {
        // --- Test 2: Invalid memory address ---
        rt_kprintf("\r\n[Test] Submitting with invalid source address...\r\n");
        char *valid_dst = (char *)rt_malloc(1024 + 0x3f);
        if (valid_dst) {
            struct cvi_tdma_copy_arg pioCfg;
            pioCfg.enable_2d  = 0;
            pioCfg.seq_no     = 500;
            pioCfg.leng_bytes = 1024;
            // Use invalid memory address
            pioCfg.paddr_src = (uintptr_t)0x1;
            pioCfg.paddr_dst = ((uintptr_t)valid_dst + 0x3f) & (~0x3f);

            cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&pioCfg);

            struct cvi_tdma_wait_arg waitCfg = {.seq_no = 500, .ret = 0};
            cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);

            rt_free(valid_dst);
        } else {
            rt_kprintf("  SKIPPED: Could not allocate memory for test.\r\n");
        }
        break;
    }
    case 3: {
        // --- Test 3: Oversized data length ---
        rt_kprintf("\r\n[Test] Submitting with oversized data length...\r\n");
        char *valid_src_large = (char *)rt_malloc(1024 + 0x3f);
        char *valid_dst_large = (char *)rt_malloc(1024 + 0x3f);
        if (valid_src_large && valid_dst_large) {
            struct cvi_tdma_copy_arg pioCfg;
            pioCfg.enable_2d  = 0;
            pioCfg.seq_no     = 501;
            // Oversized data length
            pioCfg.leng_bytes = 0xFFFFFFFF;
            pioCfg.paddr_src = ((uintptr_t)valid_src_large + 0x3f) & (~0x3f);
            pioCfg.paddr_dst = ((uintptr_t)valid_dst_large + 0x3f) & (~0x3f);

            ret = cvi_tpu_ioctl(ndev, CVITPU_SUBMIT_PIO, (unsigned long)&pioCfg);

            struct cvi_tdma_wait_arg waitCfg = {.seq_no = 501, .ret = 0};
            cvi_tpu_ioctl(ndev, CVITPU_WAIT_PIO, (unsigned long)&waitCfg);

            if (ret != 0 || waitCfg.ret != 0) {
                rt_kprintf("  SUCCESS: Driver/HW correctly reported error for oversized length \
                                (ioctl_ret=%d, wait_ret=%d).\r\n", ret, waitCfg.ret);
                test_result = 0;
            } else {
                rt_kprintf("  FAILED: Driver/HW did not report an error for oversized length.\r\n");
            }
            rt_free(valid_src_large);
            rt_free(valid_dst_large);
        } else {
            rt_kprintf("  SKIPPED: Could not allocate memory for test.\r\n");
            if (valid_src_large) rt_free(valid_src_large);
            if (valid_dst_large) rt_free(valid_dst_large);
        }
        break;
    }
    default:
        rt_kprintf("Invalid sub-case number for error handling test.\r\n");
        break;
    }

    cvi_tpu_close();
    return test_result;
}

int test_tpu(int32_t argc, char **argv)
{
    int ret;

    if (argc < 2) {
        rt_kprintf("para error, usage: cvi_tpu_test <case_num> [sub_case_num]\r\n");
        rt_kprintf("case_num 1: 1D dma once test\r\n");
        rt_kprintf("case_num 2: 1D dma multi times test\r\n");
        rt_kprintf("case_num 3: 2D dma test\r\n");
        rt_kprintf("case_num 4: performance test\r\n");
        rt_kprintf("case_num 5: error handling test. Usage: cvi_tpu_test 5 <sub_case>\r\n");
        rt_kprintf("           sub_case 1: Null pointer test\r\n");
        rt_kprintf("           sub_case 2: Invalid address test\r\n");
        rt_kprintf("           sub_case 3: Oversized data length test\r\n");
        return -1;
    }

    int test_case = atoi(argv[1]);
    cvi_tpu_init();
    if (1 == test_case) {
        ret = cvi_tpu_tdma_test_once();
    } else if (2 == test_case) {
        ret = cvi_tpu_tdma_test_multi_times();
    } else if (3 == test_case) {
        ret = cvi_tpu_tdma_test_2d();
    } else if (4 == test_case) {
        ret = cvi_tpu_tdma_test_performance();
    } else if (5 == test_case) {
        if (argc < 3) {
            cvi_tpu_close();
            rt_kprintf("Missing sub-case for error handling test. Usage: cvi_tpu_test 5 <1|2|3>\r\n");
            return -1;
        }
        int sub_case = atoi(argv[2]);
        ret = cvi_tpu_error_handling_test(sub_case);
    } else {
        cvi_tpu_close();
        rt_kprintf("para error: invalid case num\r\n");
        return -1;
    }
    return ret;
}

MSH_CMD_EXPORT(test_tpu, tpu tdma);
