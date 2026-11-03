#ifndef _CVI_RC_H_
#define _CVI_RC_H_

//#include <printf.h>
#include <stdio.h>
#include <stdlib.h>
#include <vdi_osal.h>

#define CVI_DBG_MSG_ENABLE

// #define CVI_RC_MASK_ERR   0x1
// #define CVI_RC_MASK_WARN  0x2
// #define CVI_RC_MASK_INFO  0x4
// #define CVI_RC_MASK_FLOW  0x8
// #define CVI_RC_MASK_DBG   0x10
// #define CVI_RC_MASK_IF    0x20
// #define CVI_RC_MASK_LOCK  0x40
// #define CVI_RC_MASK_RC    0x80
// #define CVI_RC_MASK_CVRC  0x100
// #define CVI_RC_MASK_FLOAT 0x200
// #define CVI_RC_MASK_MEM   0x400
// #define CVI_RC_MASK_TRACE 0x1000
// #define CVI_RC_MASK_CURR  (0x3)

#define PRINTF printf

#ifdef CVI_DBG_MSG_ENABLE
#define CVI_RC_PRNT(msg, ...) PRINTF(msg, ##__VA_ARGS__)

#define CVI_RC_ERR CVI_VC_CVRCK
#define CVI_RC_WARN CVI_VC_CVRCK
#define CVI_RC_INFO CVI_VC_CVRCK
#define CVI_RC_FLOW CVI_VC_CVRCK
#define CVI_RC_DBG CVI_VC_CVRCK
#define CVI_RC_MEM CVI_VC_CVRCK
#define CVI_RC_IF CVI_VC_CVRCK
#define CVI_RC_LOCK CVI_VC_CVRCK
#define CVI_RC_RC  CVI_VC_CVRCK
#define CVI_RC_CVRC  CVI_VC_CVRCK
#define CVI_RC_FLOAT
#define CVI_RC_TRACE CVI_VC_CVRCK
#else

#define CVI_RC_ERR(msg, ...)
#define CVI_RC_WARN(msg, ...)
#define CVI_RC_INFO(msg, ...)
#define CVI_RC_FLOW(msg, ...)
#define CVI_RC_DBG(msg, ...)
#define CVI_RC_MEM(msg, ...)
#define CVI_RC_IF(msg, ...)
#define CVI_RC_LOCK(msg, ...)
#define CVI_RC_RC(msg, ...)
#define CVI_RC_CVRC(msg, ...)
#define CVI_RC_FLOAT(msg, ...)
#define CVI_RC_TRACE(msg, ...)
#endif

#endif //#ifndef _CVI_RC_H_
