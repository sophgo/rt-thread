/* realpath_stub.h — newlib 缺失 POSIX 函数的 stub
 *
 * RT-Thread 无符号链接、无 /proc，提供空实现让编译通过。
 */
#pragma once

#include <limits.h>
#include <string.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

/* realpath：解析绝对路径。RT-Thread 无符号链接，直接复制 */
static inline char *realpath(const char *path, char *resolved) {
    if (!path || !resolved) return nullptr;
    size_t len = 0;
    while (path[len] && len < PATH_MAX - 1) {
        resolved[len] = path[len];
        len++;
    }
    resolved[len] = '\0';
    return resolved;
}

/* readlink：读取符号链接。RT-Thread 无 /proc，返回 -1 */
static inline ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    return -1;
}
