#ifndef __RGN_DEFINES_H__
#define __RGN_DEFINES_H__

#ifdef __cplusplus
	extern "C" {
#endif

#include "cvi_type.h"
#include "rtos_types.h"
#include <cvi_defines.h>
#include <vip_spinlock.h>
#include <pthread.h>

enum sclr_gop {
	SCL_GOP_SCD,
	SCL_GOP_SCV1,
	SCL_GOP_SCV2,
	SCL_GOP_SCV3,
	SCL_GOP_DISP,
	SCL_GOP_MAX,
};

struct sclr_size {
	u16 w;
	u16 h;
};

struct cvi_rect {
	__u32 left;
	__u32 top;
	__u32 width;
	__u32 height;
};

/**
 * struct cvi_rgn - RGN IP abstraction
 */
struct cvi_rgn_dev {
	spinlock_t		lock;
	spinlock_t		rdy_lock;
	pthread_mutex_t		mutex;
	bool			bind_fb;
};

#ifdef __cplusplus
}
#endif

#endif /* __RGN_DEFINES_H__ */
