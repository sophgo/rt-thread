/* cvi_vb_compat.h — VB API compatibility wrapper for RT-Thread cvi_mpi */
#pragma once

#include "cvi_comm_vb.h"
#include "cvi_vb.h"

/* RT-Thread cvi_mpi doesn't have CVI_VB_IsInited, provide stub */
static inline int CVI_VB_IsInited(void) {
    return 0;  /* 始终返回未初始化，让初始化逻辑完整执行 */
}

/* RT-Thread cvi_mpi doesn't have VB_POOL_CONFIG_EX_S, define it here */

#define VB_MAX_USER_BLK 3

typedef struct _VB_USER_BLK_S {
    uint64_t au64PhyAddr[VB_MAX_USER_BLK];
} VB_USER_BLK_S;

typedef struct _VB_POOL_CONFIG_EX_S {
    uint32_t u32BlkCnt;
    VB_USER_BLK_S astUserBlk[VB_MAX_COMM_POOLS];
} VB_POOL_CONFIG_EX_S;

/* Wrapper for CVI_VB_CreateExPool — use CVI_VB_CreatePool instead */
static inline VB_POOL CVI_VB_CreateExPool(VB_POOL_CONFIG_EX_S *pstVbPoolCfg) {
    /* For now, just create a simple pool with the first block config */
    VB_POOL_CONFIG_S cfg;
    cfg.u32BlkSize = 0; /* Will be set by caller */
    cfg.u32BlkCnt = pstVbPoolCfg->u32BlkCnt;
    cfg.enRemapMode = VB_REMAP_MODE_NONE;
    sprintf(cfg.acName, "ex_pool");

    /* Note: This is a simplified implementation.
     * The actual VB_POOL_CONFIG_EX_S supports multiple user blocks,
     * but cvi_mpi only supports simple pool config.
     * This wrapper works for basic cases but may not support all features.
     */
    return CVI_VB_CreatePool(&cfg);
}
