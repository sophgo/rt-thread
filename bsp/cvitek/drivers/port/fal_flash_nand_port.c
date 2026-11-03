/*
 * fal_flash_nand_port.c - FAL over SPI NAND（byte-offset ↔ page/block 转换）。
 *
 * FAL ops 是字节偏移语义；底层 nand0 是 rt_mtd_nand_device（page/block）。
 * 本 port 把 FAL 的 read/write/erase(byte offset,size) 翻译成 MTD 的
 * read_page/write_page/erase_block：
 *   - read：任意 off/size，按页读到临时 buf，再拷子区间（支持非页对齐/跨页）。
 *   - write：要求 off/size 页对齐，逐页 write_page（NAND 无子页编程）。
 *   - erase：要求 off/size 块对齐，逐块 erase_block。
 * 仅用 data 区，不碰 OOB。最小目标：FAL init 不报错 + 分区读写擦可用，不挂 fs。
 * 与 NOR 的 fal_flash_sfud_port.c 文件级门控互斥（BOOT_SPINAND）。
 */
#if defined(BOOT_SPINAND)

#include <rtthread.h>
#include <drivers/mtd_nand.h>
#include <fal.h>

#ifndef FAL_USING_NAND_FLASH_DEV_NAME
#define FAL_USING_NAND_FLASH_DEV_NAME "nand0"
#endif

static int init(void);
static int read(long offset, uint8_t *buf, size_t size);
static int write(long offset, const uint8_t *buf, size_t size);
static int erase(long offset, size_t size);

static struct rt_mtd_nand_device *nand_mtd;
static uint32_t nand_page_size;   /* 字节 */
static uint32_t nand_block_size;  /* 字节 = page_size * pages_per_block */

struct fal_flash_dev nand_flash0 = {
    .name       = FAL_USING_NAND_FLASH_DEV_NAME,
    .addr       = 0,
    .ops        = {init, read, write, erase},
    .write_gran = 0,   /* init 里按 page_size 填（bit） */
};

static int init(void)
{
    rt_device_t dev = rt_device_find("nand0");
    if (dev == RT_NULL)
        return -1;

    nand_mtd = (struct rt_mtd_nand_device *)dev;
    nand_page_size  = nand_mtd->page_size;
    nand_block_size = (uint32_t)nand_mtd->page_size * nand_mtd->pages_per_block;

    nand_flash0.blk_size   = nand_block_size;                 /* erase 最小粒度 = 块 */
    nand_flash0.len        = (size_t)nand_mtd->block_total * nand_block_size;
    nand_flash0.write_gran = nand_page_size * 8;              /* bit */

    return 0;
}

/* byte off 任意：按页读到临时 buf，拷子区间到 buf。
 * FAL 读不校验 ECC（分区表/元数据需尽力返回），忽略 read_page 返回值继续拷。 */
static int read(long offset, uint8_t *buf, size_t size)
{
    uint32_t page, page_off, n;
    size_t remaining = size;
    uint8_t *dst = buf, *tmp;

    if (nand_mtd == RT_NULL)
        return -1;

    tmp = rt_malloc(nand_page_size);
    if (tmp == RT_NULL)
        return -1;

    page     = (uint32_t)(offset / nand_page_size);
    page_off = (uint32_t)(offset % nand_page_size);

    while (remaining > 0) {
        (void)nand_mtd->ops->read_page(nand_mtd, page, tmp, nand_page_size, RT_NULL, 0);
        n = nand_page_size - page_off;
        if (n > remaining)
            n = (uint32_t)remaining;
        rt_memcpy(dst, tmp + page_off, n);
        dst += n; remaining -= n; page++; page_off = 0;
    }

    rt_free(tmp);
    return (int)size;
}

/* off/size 须页对齐，逐页 program（NAND 无子页编程）。 */
static int write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t page, npages, i;

    if (nand_mtd == RT_NULL)
        return -1;
    if ((offset % nand_page_size) != 0 || (size % nand_page_size) != 0)
        return -1;

    page   = (uint32_t)(offset / nand_page_size);
    npages = (uint32_t)(size / nand_page_size);

    for (i = 0; i < npages; i++) {
        if (nand_mtd->ops->write_page(nand_mtd, page + i,
                                      buf + (size_t)i * nand_page_size,
                                      nand_page_size, RT_NULL, 0) != RT_EOK)
            return -1;
    }
    return (int)size;
}

/* off/size 须块对齐，逐块 erase。 */
static int erase(long offset, size_t size)
{
    uint32_t block, nblocks, i;

    if (nand_mtd == RT_NULL)
        return -1;
    if ((offset % nand_block_size) != 0 || (size % nand_block_size) != 0)
        return -1;

    block   = (uint32_t)(offset / nand_block_size);
    nblocks = (uint32_t)(size / nand_block_size);

    for (i = 0; i < nblocks; i++) {
        if (nand_mtd->ops->erase_block(nand_mtd, block + i) != RT_EOK)
            return -1;
    }
    return (int)size;
}

#endif /* BOOT_SPINAND */
