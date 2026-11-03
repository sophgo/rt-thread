/*
 * mklfsimg.c — build a littlefs image from a directory tree.
 *
 * Packs the data/ dir next to the project partition XML
 * (customization/<project>/data/) into data_lfs.img at build time
 * (POST_ACTION in rtconfig.py) so the DATA partition ships with factory
 * files (algo models / configs). Links against the vendor lfs.c in-tree;
 * geometry values copied from dfs_lfs.c _lfs_load_config().
 *
 * Usage: mklfsimg <src_dir> <out.img> <partition_size_bytes> [block_size]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include "lfs.h"

/* geometry defaults, same numbers dfs_lfs.c uses */
#define GEOM_READ_SIZE    256
#define GEOM_PROG_SIZE    256
#define GEOM_CACHE_SIZE   GEOM_PROG_SIZE
#define GEOM_BLOCK_CYCLES (-1)
#define GEOM_BLOCK_SIZE   4096
#define GEOM_LOOKAHEAD_MAX 128

static FILE *g_img;
static lfs_size_t g_img_size;

/* flash callbacks over the image file */
static int img_read(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    if (size == 0)
        return 0;
    long pos = (long)block * (long)c->block_size + (long)off;
    if (pos + (long)size > (long)g_img_size)
        return LFS_ERR_INVAL;
    if (fseek(g_img, pos, SEEK_SET) != 0)
        return LFS_ERR_IO;
    if (fread(buffer, 1, size, g_img) != size)
        return LFS_ERR_IO;
    return 0;
}

static int img_prog(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    long pos = (long)block * (long)c->block_size + (long)off;
    if (pos + (long)size > (long)g_img_size)
        return LFS_ERR_INVAL;
    if (fseek(g_img, pos, SEEK_SET) != 0)
        return LFS_ERR_IO;
    if (fwrite(buffer, 1, size, g_img) != size)
        return LFS_ERR_IO;
    return 0;
}

static int img_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    long pos = (long)block * (long)c->block_size;
    if (pos + (long)c->block_size > (long)g_img_size)
        return LFS_ERR_INVAL;
    if (fseek(g_img, pos, SEEK_SET) != 0)
        return LFS_ERR_IO;
    static unsigned char ff[4096];
    memset(ff, 0xff, sizeof(ff));
    lfs_size_t left = c->block_size;
    while (left > sizeof(ff)) {
        if (fwrite(ff, 1, sizeof(ff), g_img) != sizeof(ff))
            return LFS_ERR_IO;
        left -= sizeof(ff);
    }
    if (fwrite(ff, 1, left, g_img) != left)
        return LFS_ERR_IO;
    return 0;
}

static int img_sync(const struct lfs_config *c)
{
    (void)c;
    if (fflush(g_img) != 0)
        return LFS_ERR_IO;
    return 0;
}

/* directory walker */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static lfs_t g_lfs;

static int copy_file(const char *src_abs, const char *lfs_path)
{
    FILE *f = fopen(src_abs, "rb");
    if (!f) {
        fprintf(stderr, "mklfsimg: open %s: %s\n", src_abs, strerror(errno));
        return -1;
    }
    lfs_file_t lf;
    int rc = lfs_file_open(&g_lfs, &lf, lfs_path,
                           LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (rc < 0) {
        fprintf(stderr, "mklfsimg: lfs open %s: %d\n", lfs_path, rc);
        fclose(f);
        return -1;
    }
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        rc = lfs_file_write(&g_lfs, &lf, buf, n);
        if (rc < 0 || (size_t)rc != n) {
            fprintf(stderr, "mklfsimg: lfs write %s: %d\n", lfs_path, rc);
            lfs_file_close(&g_lfs, &lf);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    rc = lfs_file_close(&g_lfs, &lf);
    if (rc < 0) {
        fprintf(stderr, "mklfsimg: lfs close %s: %d\n", lfs_path, rc);
        return -1;
    }
    printf("  + %s\n", lfs_path);
    return 0;
}

static int walk_dir(const char *src_abs, const char *lfs_path)
{
    DIR *d = opendir(src_abs);
    if (!d) {
        fprintf(stderr, "mklfsimg: opendir %s: %s\n", src_abs, strerror(errno));
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")
            || de->d_name[0] == '.')
            continue;
        char child_abs[PATH_MAX], child_lfs[PATH_MAX];
        snprintf(child_abs, sizeof(child_abs), "%s/%s", src_abs, de->d_name);
        snprintf(child_lfs, sizeof(child_lfs), "%s%s",
                 strcmp(lfs_path, "/") ? lfs_path : "", de->d_name);
        struct stat st;
        if (stat(child_abs, &st) != 0) {
            fprintf(stderr, "mklfsimg: stat %s: %s\n", child_abs, strerror(errno));
            closedir(d);
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            int rc = lfs_mkdir(&g_lfs, child_lfs);
            if (rc < 0 && rc != LFS_ERR_EXIST) {
                fprintf(stderr, "mklfsimg: lfs_mkdir %s: %d\n", child_lfs, rc);
                closedir(d);
                return -1;
            }
            printf("  d %s\n", child_lfs);
            if (walk_dir(child_abs, child_lfs) != 0) {
                closedir(d);
                return -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copy_file(child_abs, child_lfs) != 0) {
                closedir(d);
                return -1;
            }
        }
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc > 5) {
        fprintf(stderr,
                "Usage: %s <src_dir> <out.img> <partition_size_bytes> [block_size]\n",
                argv[0]);
        return 1;
    }
    const char *src_dir = argv[1];
    const char *out_img = argv[2];
    lfs_size_t part_size = (lfs_size_t)strtoul(argv[3], NULL, 0);
    lfs_size_t block_size = (argc == 5)
                            ? (lfs_size_t)strtoul(argv[4], NULL, 0)
                            : GEOM_BLOCK_SIZE;
    if (block_size == 0 || part_size % block_size != 0) {
        fprintf(stderr, "mklfsimg: partition size must be block-size aligned\n");
        return 1;
    }
    lfs_size_t block_count = part_size / block_size;
    lfs_size_t lookahead = 32 * ((block_count + 31) / 32);
    if (lookahead > GEOM_LOOKAHEAD_MAX)
        lookahead = GEOM_LOOKAHEAD_MAX;

    /* image file pre-filled with 0xff (erased flash); wb+: littlefs
     * reads back what it wrote */
    g_img = fopen(out_img, "wb+");
    if (!g_img) {
        fprintf(stderr, "mklfsimg: create %s: %s\n", out_img, strerror(errno));
        return 1;
    }
    unsigned char ff[4096];
    memset(ff, 0xff, sizeof(ff));
    for (lfs_size_t i = 0; i < part_size; i += sizeof(ff)) {
        lfs_size_t chunk = (part_size - i < sizeof(ff)) ? (part_size - i)
                                                        : sizeof(ff);
        if (fwrite(ff, 1, chunk, g_img) != chunk) {
            fprintf(stderr, "mklfsimg: init %s failed\n", out_img);
            fclose(g_img);
            return 1;
        }
    }
    fflush(g_img);
    g_img_size = part_size;

    struct lfs_config cfg = {0};
    cfg.context = NULL;
    cfg.read_size = GEOM_READ_SIZE;
    cfg.prog_size = GEOM_PROG_SIZE;
    cfg.block_size = block_size;
    cfg.block_count = block_count;
    cfg.cache_size = GEOM_CACHE_SIZE;
    cfg.lookahead_size = lookahead;
    cfg.block_cycles = GEOM_BLOCK_CYCLES;
    cfg.read = img_read;
    cfg.prog = img_prog;
    cfg.erase = img_erase;
    cfg.sync = img_sync;

    int rc = lfs_format(&g_lfs, &cfg);
    if (rc < 0) {
        fprintf(stderr, "mklfsimg: lfs_format: %d\n", rc);
        fclose(g_img);
        return 1;
    }
    rc = lfs_mount(&g_lfs, &cfg);
    if (rc < 0) {
        fprintf(stderr, "mklfsimg: lfs_mount: %d\n", rc);
        fclose(g_img);
        return 1;
    }
    printf("mklfsimg: littlefs %ux%u bytes, packing %s -> %s\n",
           (unsigned)block_count, (unsigned)block_size, src_dir, out_img);
    if (walk_dir(src_dir, "/") != 0) {
        lfs_unmount(&g_lfs);
        fclose(g_img);
        return 1;
    }
    lfs_unmount(&g_lfs);
    if (fflush(g_img) != 0 || fclose(g_img) != 0) {
        fprintf(stderr, "mklfsimg: close %s failed\n", out_img);
        return 1;
    }
    printf("mklfsimg: done\n");
    return 0;
}
