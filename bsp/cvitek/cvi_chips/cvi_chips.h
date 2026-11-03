#ifndef __CVI_CHIPS_H__
#define __CVI_CHIPS_H__
#include <rtconfig.h>

#if defined(__CV181X__)
#include "soc_cv181x.h"
#elif defined(__CV180X__)
#include "soc_cv180x.h"
#elif defined(__CV184X__)
#include "soc_cv184x.h"
#else
#error "Please select the chip type"
#endif


#endif /* __CVI_CHIPS_H__ */