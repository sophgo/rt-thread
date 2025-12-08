
#include "awb_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvi_awb_comm.h"
#include "cvi_comm_3a.h"

#if defined(__CV181X__) || defined(__CV180X__)
#include "isp_mgr_buf.h"
#else
CVI_U32 u32AWBParamUpdateFlag[AWB_SENSOR_NUM];
#endif

#ifdef ARCH_RTOS_CV181X
#include "malloc.h"
#endif

static CVI_S32 awb_malloc_cnt;

ISP_WB_Q_INFO_S            *pstAwb_Q_Info[AWB_SENSOR_NUM];
ISP_WB_ATTR_S              *pstAwbMpiAttr[AWB_SENSOR_NUM];
ISP_AWB_ATTR_EX_S          *pstAwbMpiAttrEx[AWB_SENSOR_NUM];
ISP_AWB_Calibration_Gain_S *pstWbCalibration[AWB_SENSOR_NUM];

#ifndef ARCH_RTOS_CV181X  // TODO@CV181X
void *AWB_Malloc(size_t nsize)
{
    void *ptr = NULL;

    ptr = malloc(nsize);
    if (ptr != NULL) awb_malloc_cnt++;
    return ptr;
}
#else
void *AWB_Malloc(size_t nsize)
{
    void *ptr = NULL;

    ptr = pvPortMalloc(nsize);
    if (ptr != NULL) awb_malloc_cnt++;
    return ptr;
}
#endif

#ifndef ARCH_RTOS_CV181X  // TODO@CV181X
void AWB_Free(void *ptr)
{
    if (ptr != NULL) {
        awb_malloc_cnt--;
        free(ptr);
    }
}
#else
void AWB_Free(void *ptr)
{
    if (ptr != NULL) {
        awb_malloc_cnt--;
        vPortFree(ptr);
    }
}
#endif

void AWB_CheckMemFree(void)
{
    if (awb_malloc_cnt != 0) {
        printf("error: malloc/free is not match %d\n", awb_malloc_cnt);
    }
}

void awb_buf_init(CVI_U8 sID)
{
    CVI_S32 ret = CVI_SUCCESS;

#if defined(__CV181X__) || defined(__CV180X__)
    struct isp_3a_shared_buffer *p = CVI_NULL;

    isp_mgr_buf_get_3a_addr(sID, (CVI_VOID **)&p);

    pstAwb_Q_Info[sID]    = &p->stWB_Q_Info;
    pstAwbMpiAttr[sID]    = &p->stWBAttr;
    pstAwbMpiAttrEx[sID]  = &p->stAWBAttrEx;
    pstWbCalibration[sID] = &p->stWBCalib;
#else
    if (pstAwb_Q_Info[sID] == CVI_NULL) {
        pstAwb_Q_Info[sID] = (ISP_WB_Q_INFO_S *)AWB_Malloc(sizeof(ISP_WB_Q_INFO_S));
        if (pstAwb_Q_Info[sID] == CVI_NULL) {
            ret = CVI_FAILURE;
        }
    }

    if (pstAwbMpiAttr[sID] == CVI_NULL) {
        pstAwbMpiAttr[sID] = (ISP_WB_ATTR_S *)AWB_Malloc(sizeof(ISP_WB_ATTR_S));
        if (pstAwbMpiAttr[sID] == CVI_NULL) {
            ret = CVI_FAILURE;
        }
    }

    if (pstAwbMpiAttrEx[sID] == CVI_NULL) {
        pstAwbMpiAttrEx[sID] = (ISP_AWB_ATTR_EX_S *)AWB_Malloc(sizeof(ISP_AWB_ATTR_EX_S));
        if (pstAwbMpiAttrEx[sID] == CVI_NULL) {
            ret = CVI_FAILURE;
        }
    }

    if (pstWbCalibration[sID] == CVI_NULL) {
        pstWbCalibration[sID] =
            (ISP_AWB_Calibration_Gain_S *)AWB_Malloc(sizeof(ISP_AWB_Calibration_Gain_S));
        if (pstWbCalibration[sID] == CVI_NULL) {
            ret = CVI_FAILURE;
        }
    }
#endif

    if (ret != CVI_SUCCESS) {
        printf("error, awb buf init fail\n");
    }
}

void awb_buf_deinit(CVI_U8 sID)
{
#if defined(__CV181X__) || defined(__CV180X__)
    pstAwb_Q_Info[sID]    = CVI_NULL;
    pstAwbMpiAttr[sID]    = CVI_NULL;
    pstAwbMpiAttrEx[sID]  = CVI_NULL;
    pstWbCalibration[sID] = CVI_NULL;
#else
    if (pstAwb_Q_Info[sID] != CVI_NULL) {
        AWB_Free(pstAwb_Q_Info[sID]);
        pstAwb_Q_Info[sID] = CVI_NULL;
    }

    if (pstAwbMpiAttr[sID] != CVI_NULL) {
        AWB_Free(pstAwbMpiAttr[sID]);
        pstAwbMpiAttr[sID] = CVI_NULL;
    }

    if (pstAwbMpiAttrEx[sID] != CVI_NULL) {
        AWB_Free(pstAwbMpiAttrEx[sID]);
        pstAwbMpiAttrEx[sID] = CVI_NULL;
    }

    if (pstWbCalibration[sID] != CVI_NULL) {
        AWB_Free(pstWbCalibration[sID]);
        pstWbCalibration[sID] = CVI_NULL;
    }
#endif
}

void AWB_SetParamUpdateFlag(CVI_U8 sID, AWB_PARAMETER_UPDATE flag)
{
#if defined(__CV181X__) || defined(__CV180X__)
    struct isp_3a_shared_buffer *p = CVI_NULL;

    isp_mgr_buf_get_3a_addr(sID, (CVI_VOID **)&p);
    p->u32AWBParamUpdateFlag |= (0x01 << flag);
#else
    u32AWBParamUpdateFlag[sID] |= (0x01 << flag);
#endif
}

void AWB_CheckParamUpdateFlag(CVI_U8 sID)
{
    CVI_U32 flag = 0;

#if defined(__CV181X__) || defined(__CV180X__)
    struct isp_3a_shared_buffer *p = CVI_NULL;

    isp_mgr_buf_get_3a_addr(sID, (CVI_VOID **)&p);
    flag = p->u32AWBParamUpdateFlag;
    if (flag != 0x00) {
        p->u32AWBParamUpdateFlag = 0x00;
    }
#else
    flag = u32AWBParamUpdateFlag[sID];
    if (flag != 0x00) {
        u32AWBParamUpdateFlag[sID] = 0x00;
    }
#endif

    if (flag != 0x00) {
        if (flag & (0x01 << AWB_ATTR_UPDATE)) {
            isNeedUpdateAttr[sID] = 1;
        }

        if (flag & (0x01 << AWB_ATTR_EX_UPDATE)) {
            isNeedUpdateAttrEx[sID] = 1;
        }

        if (flag & (0x01 << AWB_CALIB_UPDATE)) {
            isNeedUpdateCalib[sID] = 1;
        }
    }
}

#ifdef ARCH_RTOS_CV181X  // TODO: mason.zou
#include "isp_mgr_buf.h"
void AWB_RtosBufInit(CVI_U8 sID)
{
    struct isp_3a_shared_buffer *p = CVI_NULL;

    isp_mgr_buf_get_3a_addr(sID, (CVI_VOID **)&p);

    pstAwb_Q_Info[sID]    = &p->stWB_Q_Info;
    pstAwbMpiAttr[sID]    = &p->stWBAttr;
    pstAwbMpiAttrEx[sID]  = &p->stAWBAttrEx;
    pstWbCalibration[sID] = &p->stWBCalib;
}
#endif
