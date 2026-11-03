/* dlfcn.h — RT-Thread/newlib stub
 *
 * 原始 SDK 在 Linux glibc 上用 dladdr() 获取 .so 路径，
 * RT-Thread 没有动态链接，提供空实现让编译通过。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *dli_fname;  /* 共享对象路径 */
    void       *dli_fbase;  /* 加载基址 */
    const char *dli_sname;  /* 符号名 */
    void       *dli_saddr;  /* 符号地址 */
} Dl_info;

/* RT-Thread 无动态链接，始终返回 0（失败） */
static inline int dladdr(const void *addr, Dl_info *info) {
    (void)addr;
    if (info) {
        info->dli_fname = "";
        info->dli_fbase = 0;
        info->dli_sname = 0;
        info->dli_saddr = 0;
    }
    return 0;
}

static inline void *dlopen(const char *filename, int flags) {
    (void)filename; (void)flags;
    return (void *)0;
}

static inline void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return (void *)0;
}

static inline int dlclose(void *handle) {
    (void)handle;
    return 0;
}

static inline const char *dlerror(void) {
    return "dlfcn not supported on RT-Thread";
}

#define RTLD_LAZY   0x00001
#define RTLD_NOW    0x00002
#define RTLD_GLOBAL 0x00100
#define RTLD_LOCAL  0

#ifdef __cplusplus
}
#endif
