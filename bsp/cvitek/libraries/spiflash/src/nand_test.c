/*
 * nand_test.c - SPI NAND 上板测试命令（系统层）。u-boot 风格单入口 `nand <sub> ...`。
 * 全部 #if defined(BOOT_SPINAND) 门控；破坏性子命令(write/erase/markbad/torture/
 * speed/stress)要求显式地址、无默认，避免误擦 boot/PRIM 分区。
 * 只用已验证的驱动原语（cvi_spi_nand_read_page/write_page/erase_block/read_bbm/
 * write_bbm），驱动核心不在本文件。
 */
#if defined(BOOT_SPINAND)

#include <rtthread.h>
#include <stdlib.h>
#include <string.h>
#include "cvitek_spi_nand.h"

/* 一键自测默认块：取颗粒高地址空闲块，避开 boot0/boot/PRIM 分区。
 * ⚠️ 破坏性：nand test 会擦写该块。命令行可覆盖。 */
#define NAND_TEST_DEFAULT_BLOCK_FROM_TOP  4   /* block_total - 4 */

/* torture 图案序列（u-boot 语义）。 */
static const uint8_t nand_torture_patterns[] = { 0x00, 0xff, 0x55, 0xaa };

static void nand_print_usage(void)
{
    /* 逐行打印：单次 rt_kprintf 塞过长多行串会被串口 FIFO 截断丢尾。 */
    rt_kprintf("nand info                              - print geometry/id\n");
    rt_kprintf("nand read  <addr> <off> <size>         - read flash[off,off+size) to memory addr\n");
    rt_kprintf("nand dump  <page> [oob]                - hexdump page, oob=append spare\n");
    rt_kprintf("nand write <addr> <off> <size>         - write memory addr to flash (off/size page-aligned, DESTRUCTIVE)\n");
    rt_kprintf("nand erase <block>                     - erase one block            (DESTRUCTIVE)\n");
    rt_kprintf("nand bad                               - scan all blocks, list bad\n");
    rt_kprintf("nand markbad <block>                   - mark block bad             (UNSAFE)\n");
    rt_kprintf("nand torture <block>                   - multi-pattern torture one block (DESTRUCTIVE)\n");
    rt_kprintf("nand speed <start_block> <nblocks>     - seq write/read throughput + verify (DESTRUCTIVE)\n");
    rt_kprintf("nand stress -r <block> <rounds>        - repeat erase/write/read/verify one block (DESTRUCTIVE)\n");
    rt_kprintf("nand stress -s <start> <nblocks> [rounds] - sweep block range, verify each (DESTRUCTIVE)\n");
    rt_kprintf("nand test [block]                      - one-shot self test (DESTRUCTIVE)\n");
}

/* 十六进制 16 字节/行打印。 */
static void nand_hexdump(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        if ((i & 0x0f) == 0) rt_kprintf("%04x:", i);
        rt_kprintf(" %02x", buf[i]);
        if ((i & 0x0f) == 0x0f) rt_kprintf("\n");
    }
    if (len & 0x0f) rt_kprintf("\n");
}

/* ---------------- 基础子命令 ---------------- */

static int do_nand_info(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    rt_kprintf("name %s id %02x %02x %02x\n", info->name, info->id[0], info->id[1], info->id[2]);
    rt_kprintf("page %u oob %u block %u pages/block %u blocks %u\n",
               info->page_size, info->oob_size, info->block_size,
               info->pages_per_block, info->block_total);
    return 0;
}

/* u-boot 语义：nand read <addr> <off> <size> —— 把 flash [off,off+size) 读到内存 addr。
 * off/size 可任意（非页对齐、跨页均可）：按页读到临时 buf，再拷子区间到 addr。 */
static int do_nand_read(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t off, size, page, page_off, remaining, n, uncorr = 0;
    uint32_t total_pages = info->block_total * info->pages_per_block;
    uint8_t *dst, *dst0, *tmp;
    int ecc;

    if (argc < 5) { rt_kprintf("usage: nand read <addr> <off> <size>\n"); return -1; }
    dst0 = (uint8_t *)(uintptr_t)strtoul(argv[2], NULL, 0);
    dst  = dst0;
    off  = strtoul(argv[3], NULL, 0);
    size = strtoul(argv[4], NULL, 0);
    if (size == 0) { rt_kprintf("size must be > 0\n"); return -1; }

    /* 末字节所在页必须在片内。 */
    if ((off + size - 1) / info->page_size >= total_pages) {
        rt_kprintf("out of range: off 0x%x size %u exceeds flash\n", off, size);
        return -1;
    }

    tmp = rt_malloc(info->page_size);
    if (!tmp) return -1;

    page     = off / info->page_size;
    page_off = off % info->page_size;
    remaining = size;
    while (remaining > 0) {
        ecc = cvi_spi_nand_read_page(page, tmp, info->page_size);
        if (ecc == CVI_SPI_NAND_ECC_UNCORR) uncorr++;
        n = info->page_size - page_off;
        if (n > remaining) n = remaining;
        rt_memcpy(dst, tmp + page_off, n);
        dst += n;
        remaining -= n;
        page++;
        page_off = 0;
    }
    rt_kprintf("nand read: off 0x%x size %u -> %p, ecc uncorr pages %u\n",
               off, size, (void *)dst0, uncorr);
    rt_free(tmp);
    return 0;
}

static int do_nand_dump(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t page, total;
    uint8_t *buf;
    int with_oob, ecc;

    if (argc < 3) { rt_kprintf("usage: nand dump <page> [oob]\n"); return -1; }
    page = strtoul(argv[2], NULL, 0);
    with_oob = (argc >= 4 && strcmp(argv[3], "oob") == 0);
    total = with_oob ? (info->page_size + info->oob_size) : info->page_size;

    buf = rt_malloc(total);
    if (!buf) return -1;
    ecc = cvi_spi_nand_read_page(page, buf, total);
    rt_kprintf("page %u ecc=%d, %u data bytes:\n", page, ecc, info->page_size);
    nand_hexdump(buf, info->page_size);
    if (with_oob) {
        rt_kprintf("-- OOB (%u bytes) --\n", info->oob_size);
        nand_hexdump(buf + info->page_size, info->oob_size);
    }
    rt_free(buf);
    return 0;
}

/* u-boot 语义：nand write <addr> <off> <size> —— 把内存 addr 写到 flash off。
 * NAND 无子页编程：要求 off/size 页对齐，逐页 program（不 erase、不 malloc，同 u-boot）。
 * 目标页需事先 nand erase。 */
static int do_nand_write(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t off, size, page, npages, i;
    uint32_t total_pages = info->block_total * info->pages_per_block;
    const uint8_t *src;

    if (argc < 5) {
        rt_kprintf("usage: nand write <addr> <off> <size>  (off/size page-aligned, DESTRUCTIVE)\n");
        return -1;
    }
    src  = (const uint8_t *)(uintptr_t)strtoul(argv[2], NULL, 0);
    off  = strtoul(argv[3], NULL, 0);
    size = strtoul(argv[4], NULL, 0);
    if (size == 0) { rt_kprintf("size must be > 0\n"); return -1; }

    if ((off % info->page_size) != 0 || (size % info->page_size) != 0) {
        rt_kprintf("off/size must be page-aligned (page_size=%u)\n", info->page_size);
        return -1;
    }
    page   = off / info->page_size;
    npages = size / info->page_size;
    if (page + npages > total_pages) {
        rt_kprintf("out of range: off 0x%x size %u exceeds flash\n", off, size);
        return -1;
    }

    for (i = 0; i < npages; i++) {
        if (cvi_spi_nand_write_page(page + i, src + i * info->page_size, info->page_size) != 0) {
            rt_kprintf("write page %u FAIL\n", page + i);
            return -1;
        }
    }
    rt_kprintf("nand write: %p -> off 0x%x size %u OK\n", (void *)src, off, size);
    return 0;
}

static int do_nand_erase(int argc, char **argv)
{
    uint32_t block;

    if (argc < 3) { rt_kprintf("usage: nand erase <block>  (DESTRUCTIVE)\n"); return -1; }
    block = strtoul(argv[2], NULL, 0);
    rt_kprintf("erase block %u: %s\n", block,
               (cvi_spi_nand_erase_block(block) == 0) ? "OK" : "FAIL");
    return 0;
}

static int do_nand_bad(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t b, nbad = 0;
    uint8_t marker;

    for (b = 0; b < info->block_total; b++) {
        marker = 0xff;
        if (cvi_spi_nand_read_bbm(b, &marker) != 0) {
            rt_kprintf("block %u: read bbm fail\n", b);
            continue;
        }
        if (marker != 0xff) {
            rt_kprintf("block %u bad (marker=%02x)\n", b, marker);
            nbad++;
        }
    }
    rt_kprintf("total bad: %u / %u\n", nbad, info->block_total);
    return 0;
}

static int do_nand_markbad(int argc, char **argv)
{
    uint32_t block;

    if (argc < 3) { rt_kprintf("usage: nand markbad <block>  (UNSAFE)\n"); return -1; }
    block = strtoul(argv[2], NULL, 0);
    rt_kprintf("mark block %u bad: %s\n", block,
               (cvi_spi_nand_write_bbm(block, 0x00) == 0) ? "OK" : "FAIL");
    return 0;
}

/* ---------------- torture：单块多图案反复擦写 ---------------- */

static int do_nand_torture(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t block, first_page, p, i, pat;
    uint8_t *wbuf, *rbuf;
    int ret = 0, ecc;

    if (argc < 3) { rt_kprintf("usage: nand torture <block>  (DESTRUCTIVE)\n"); return -1; }
    block = strtoul(argv[2], NULL, 0);
    first_page = block * info->pages_per_block;

    wbuf = rt_malloc(info->page_size);
    rbuf = rt_malloc(info->page_size);
    if (!wbuf || !rbuf) { rt_free(wbuf); rt_free(rbuf); return -1; }

    for (pat = 0; pat < sizeof(nand_torture_patterns); pat++) {
        uint8_t v = nand_torture_patterns[pat];
        if (cvi_spi_nand_erase_block(block) != 0) {
            rt_kprintf("block %u torture FAIL: erase at pattern %02x\n", block, v);
            ret = -1; goto out;
        }
        for (i = 0; i < info->page_size; i++) wbuf[i] = v;
        for (p = 0; p < info->pages_per_block; p++) {
            if (cvi_spi_nand_write_page(first_page + p, wbuf, info->page_size) != 0) {
                rt_kprintf("block %u torture FAIL at pattern %02x page %u (write) "
                           "-> suggest 'nand markbad %u' (NOT auto-marked)\n",
                           block, v, first_page + p, block);
                ret = -1; goto out;
            }
            ecc = cvi_spi_nand_read_page(first_page + p, rbuf, info->page_size);
            if (ecc == CVI_SPI_NAND_ECC_UNCORR || rt_memcmp(wbuf, rbuf, info->page_size) != 0) {
                rt_kprintf("block %u torture FAIL at pattern %02x page %u (verify ecc=%d) "
                           "-> suggest 'nand markbad %u' (NOT auto-marked)\n",
                           block, v, first_page + p, ecc, block);
                ret = -1; goto out;
            }
        }
        rt_kprintf("block %u pattern %02x PASS\n", block, v);
    }
    rt_kprintf("block %u torture PASS (good)\n", block);

out:
    cvi_spi_nand_erase_block(block);   /* 收尾擦回 */
    rt_free(wbuf); rt_free(rbuf);
    return ret;
}

/* ---------------- speed：多块连续吞吐 + verify ---------------- */

static void nand_print_rate(const char *tag, uint32_t bytes, uint32_t ms)
{
    if (ms == 0) {
        rt_kprintf("%s: %u KiB in <1ms (too fast, increase nblocks)\n", tag, bytes >> 10);
        return;
    }
    /* bytes/ms == KiB/s*... ；KiB/ms ≈ MB/s。用整数算避免浮点。 */
    uint32_t kib = bytes >> 10;
    uint32_t mbps_i = kib / ms;              /* MB/s 整数部分 */
    uint32_t mbps_f = ((kib % ms) * 1000) / ms; /* 三位小数 */
    rt_kprintf("%s: %u KiB in %u ms => %u.%03u MB/s\n", tag, kib, ms, mbps_i, mbps_f);
}

static int do_nand_speed(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t start, nblocks, b, p, i, first_page;
    uint32_t total_bytes;
    uint8_t *wbuf, *rbuf;
    rt_tick_t t0;
    int ecc, ret = 0;

    if (argc < 4) { rt_kprintf("usage: nand speed <start_block> <nblocks>  (DESTRUCTIVE)\n"); return -1; }
    start = strtoul(argv[2], NULL, 0);
    nblocks = strtoul(argv[3], NULL, 0);
    if (nblocks == 0) { rt_kprintf("nblocks must be > 0\n"); return -1; }

    wbuf = rt_malloc(info->page_size);
    rbuf = rt_malloc(info->page_size);
    if (!wbuf || !rbuf) { rt_free(wbuf); rt_free(rbuf); return -1; }
    for (i = 0; i < info->page_size; i++) wbuf[i] = (uint8_t)(i ^ 0x5a);

    total_bytes = nblocks * info->pages_per_block * info->page_size;

    /* 1. 逐块 erase */
    for (b = 0; b < nblocks; b++) {
        if (cvi_spi_nand_erase_block(start + b) != 0) {
            rt_kprintf("erase block %u FAIL\n", start + b); ret = -1; goto out;
        }
    }

    /* 2. 顺序写 + 计时 */
    t0 = rt_tick_get();
    for (b = 0; b < nblocks; b++) {
        first_page = (start + b) * info->pages_per_block;
        for (p = 0; p < info->pages_per_block; p++) {
            if (cvi_spi_nand_write_page(first_page + p, wbuf, info->page_size) != 0) {
                rt_kprintf("write block %u page %u FAIL\n", start + b, first_page + p);
                ret = -1; goto out;
            }
        }
    }
    nand_print_rate("write", total_bytes, (uint32_t)(rt_tick_get() - t0));

    /* 3. 顺序读 + verify + 计时 */
    t0 = rt_tick_get();
    for (b = 0; b < nblocks; b++) {
        first_page = (start + b) * info->pages_per_block;
        for (p = 0; p < info->pages_per_block; p++) {
            ecc = cvi_spi_nand_read_page(first_page + p, rbuf, info->page_size);
            if (ecc == CVI_SPI_NAND_ECC_UNCORR || rt_memcmp(wbuf, rbuf, info->page_size) != 0) {
                rt_kprintf("verify block %u page %u FAIL (ecc=%d)\n", start + b, first_page + p, ecc);
                ret = -1; goto out;
            }
        }
    }
    nand_print_rate("read ", total_bytes, (uint32_t)(rt_tick_get() - t0));
    rt_kprintf("speed [%u,%u): verify PASS\n", start, start + nblocks);

out:
    rt_free(wbuf); rt_free(rbuf);
    return ret;
}

/* ---------------- stress：-r 同块反复 / -s 区间遍历 ---------------- */

/* 对单块跑一轮：erase -> 写种子图案 -> 读 memcmp + 查 ECC。返回 0 成功，<0 失败。
 * 图案字节 = page_idx + seed + i，seed 每轮/每块不同以避开固定图案假阳性。 */
static int nand_stress_one_block(uint32_t block, uint32_t seed, uint8_t *wbuf, uint8_t *rbuf)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t first_page = block * info->pages_per_block;
    uint32_t p, i;
    int ecc;

    if (cvi_spi_nand_erase_block(block) != 0) return -1;
    for (p = 0; p < info->pages_per_block; p++) {
        for (i = 0; i < info->page_size; i++)
            wbuf[i] = (uint8_t)(p + seed + i);
        if (cvi_spi_nand_write_page(first_page + p, wbuf, info->page_size) != 0) {
            rt_kprintf("block %u page %u FAIL (write)\n", block, first_page + p);
            return -2;
        }
        ecc = cvi_spi_nand_read_page(first_page + p, rbuf, info->page_size);
        if (ecc == CVI_SPI_NAND_ECC_UNCORR || rt_memcmp(wbuf, rbuf, info->page_size) != 0) {
            rt_kprintf("block %u page %u FAIL (verify ecc=%d)\n", block, first_page + p, ecc);
            return -3;
        }
    }
    return 0;
}

static int do_nand_stress(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint8_t *wbuf, *rbuf;
    int ret = 0;

    if (argc < 3) {
        rt_kprintf("usage: nand stress -r <block> <rounds>\n"
                   "       nand stress -s <start> <nblocks> [rounds]  (DESTRUCTIVE)\n");
        return -1;
    }

    wbuf = rt_malloc(info->page_size);
    rbuf = rt_malloc(info->page_size);
    if (!wbuf || !rbuf) { rt_free(wbuf); rt_free(rbuf); return -1; }

    if (strcmp(argv[2], "-r") == 0) {
        uint32_t block, rounds, r;
        if (argc < 5) { rt_kprintf("usage: nand stress -r <block> <rounds>\n"); ret = -1; goto out; }
        block = strtoul(argv[3], NULL, 0);
        rounds = strtoul(argv[4], NULL, 0);
        if (rounds == 0) rounds = 0xffffffff;   /* 近似无限，靠失败/Ctrl-C 停 */
        rt_kprintf("stress -r block %u rounds %u ...\n", block, rounds);
        for (r = 0; r < rounds; r++) {
            if (nand_stress_one_block(block, r * 31 + 1, wbuf, rbuf) != 0) {
                rt_kprintf("stress -r block %u FAIL at round %u\n", block, r);
                ret = -1; goto out;
            }
            if ((r & 0x1f) == 0x1f) rt_kprintf("  round %u ok\n", r + 1);
        }
        cvi_spi_nand_erase_block(block);
        rt_kprintf("stress -r block %u: %u rounds PASS\n", block, rounds);
    } else if (strcmp(argv[2], "-s") == 0) {
        uint32_t start, nblocks, rounds, r, b;
        if (argc < 5) { rt_kprintf("usage: nand stress -s <start> <nblocks> [rounds]\n"); ret = -1; goto out; }
        start = strtoul(argv[3], NULL, 0);
        nblocks = strtoul(argv[4], NULL, 0);
        rounds = (argc >= 6) ? strtoul(argv[5], NULL, 0) : 1;
        if (nblocks == 0 || rounds == 0) { rt_kprintf("nblocks/rounds must be > 0\n"); ret = -1; goto out; }
        rt_kprintf("stress -s [%u,%u) rounds %u ...\n", start, start + nblocks, rounds);
        for (r = 0; r < rounds; r++) {
            for (b = 0; b < nblocks; b++) {
                if (nand_stress_one_block(start + b, r * 31 + (start + b) * 7 + 1, wbuf, rbuf) != 0) {
                    rt_kprintf("stress -s FAIL at round %u block %u\n", r, start + b);
                    ret = -1; goto out;
                }
            }
            rt_kprintf("  sweep round %u/%u ok\n", r + 1, rounds);
        }
        for (b = 0; b < nblocks; b++) cvi_spi_nand_erase_block(start + b);
        rt_kprintf("stress -s [%u,%u) x%u: PASS\n", start, start + nblocks, rounds);
    } else {
        rt_kprintf("unknown stress mode '%s' (use -r or -s)\n", argv[2]);
        ret = -1;
    }

out:
    rt_free(wbuf); rt_free(rbuf);
    return ret;
}

/* ---------------- test：一键自测 ---------------- */

static int do_nand_test(int argc, char **argv)
{
    const struct cvi_spi_nand_info *info = cvi_spi_nand_get_info();
    uint32_t block, first_page, last_page, i;
    uint8_t *wbuf, *rbuf, marker = 0xff;
    int fail = 0, ecc;

    block = (argc >= 3) ? strtoul(argv[2], NULL, 0)
                        : (info->block_total - NAND_TEST_DEFAULT_BLOCK_FROM_TOP);
    first_page = block * info->pages_per_block;
    last_page  = first_page + info->pages_per_block - 1;
    rt_kprintf("== NAND SELFTEST block %u (pages %u..%u) ==\n", block, first_page, last_page);

    /* 1. 坏块检查 */
    cvi_spi_nand_read_bbm(block, &marker);
    if (marker != 0xff) { rt_kprintf("block %u is BAD (bbm=%02x), abort\n", block, marker); return -1; }
    rt_kprintf("[1] check_block: good PASS\n");

    wbuf = rt_malloc(info->page_size);
    rbuf = rt_malloc(info->page_size);
    if (!wbuf || !rbuf) { rt_free(wbuf); rt_free(rbuf); return -1; }

    /* 2. erase -> 读回全 0xFF（抽查首页） */
    if (cvi_spi_nand_erase_block(block) != 0) { rt_kprintf("[2] erase FAIL\n"); fail = 1; goto out; }
    cvi_spi_nand_read_page(first_page, rbuf, info->page_size);
    for (i = 0; i < info->page_size; i++) if (rbuf[i] != 0xff) { fail = 1; break; }
    rt_kprintf("[2] erase->0xFF: %s\n", fail ? "FAIL" : "PASS");
    if (fail) goto out;

    /* 3. write pattern -> read -> memcmp（首页 + 末页各一次，验 row/plane） */
    for (i = 0; i < info->page_size; i++) wbuf[i] = (uint8_t)(i ^ 0x5a);
    uint32_t pages[2] = { first_page, last_page };
    for (int p = 0; p < 2; p++) {
        if (cvi_spi_nand_write_page(pages[p], wbuf, info->page_size) != 0) {
            rt_kprintf("[3] write page %u FAIL\n", pages[p]); fail = 1; goto out;
        }
        ecc = cvi_spi_nand_read_page(pages[p], rbuf, info->page_size);
        if (ecc == CVI_SPI_NAND_ECC_UNCORR) { rt_kprintf("[3] ecc UNCORR page %u\n", pages[p]); fail = 1; goto out; }
        if (rt_memcmp(wbuf, rbuf, info->page_size) != 0) {
            rt_kprintf("[3] memcmp page %u FAIL\n", pages[p]); fail = 1; goto out;
        }
        rt_kprintf("[3] write+read+memcmp page %u: PASS (ecc=%d)\n", pages[p], ecc);
    }

    /* 4. 收尾擦回，不残留 */
    cvi_spi_nand_erase_block(block);
    rt_kprintf("[4] cleanup erase: done\n");

out:
    rt_free(wbuf); rt_free(rbuf);
    rt_kprintf("== NAND SELFTEST %s ==\n", fail ? "FAIL" : "PASS");
    return fail ? -1 : 0;
}

/* ---------------- 单入口分发器 ---------------- */

static int nand_cmd(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) { nand_print_usage(); return 0; }

    if (strcmp(argv[1], "info")    == 0) return do_nand_info(argc, argv);
    if (strcmp(argv[1], "read")    == 0) return do_nand_read(argc, argv);
    if (strcmp(argv[1], "dump")    == 0) return do_nand_dump(argc, argv);
    if (strcmp(argv[1], "write")   == 0) return do_nand_write(argc, argv);
    if (strcmp(argv[1], "erase")   == 0) return do_nand_erase(argc, argv);
    if (strcmp(argv[1], "bad")     == 0) return do_nand_bad(argc, argv);
    if (strcmp(argv[1], "markbad") == 0) return do_nand_markbad(argc, argv);
    if (strcmp(argv[1], "torture") == 0) return do_nand_torture(argc, argv);
    if (strcmp(argv[1], "speed")   == 0) return do_nand_speed(argc, argv);
    if (strcmp(argv[1], "stress")  == 0) return do_nand_stress(argc, argv);
    if (strcmp(argv[1], "test")    == 0) return do_nand_test(argc, argv);

    rt_kprintf("unknown subcommand '%s'\n", argv[1]);
    nand_print_usage();
    return -1;
}
MSH_CMD_EXPORT_ALIAS(nand_cmd, nand, spi nand test: nand <sub> (help for list));

#endif /* BOOT_SPINAND */
