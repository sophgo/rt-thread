/* CV180x/CV181x single-system boot time records. */
#ifndef CVI_BOOT_TIME_H
#define CVI_BOOT_TIME_H

#include <rtconfig.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DUMP_RECORD_TIME
/* Call once at the application main entry; timestamps are in milliseconds. */
void cvi_boot_time_record(void);
/* Print the saved snapshot after console initialization. */
void cvi_boot_time_dump(void);
#else
static inline void cvi_boot_time_record(void) {}
static inline void cvi_boot_time_dump(void) {}
#endif

#ifdef __cplusplus
}
#endif
#endif
