/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: cvi_awb.c
 * Description:
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #include "cvi_awb_comm.h"
#include "awbalgo.h"
#include "cvi_awb.h"
#include "cvi_comm_3a.h"
#include "cvi_comm_sns.h"
#include "cvi_isp.h"
#include "isp_debug.h"
#include "isp_ipc.h"
#include "isp_main.h"
#include "isp_proc.h"

CVI_S32 CVI_AWB_Register(VI_PIPE ViPipe, ALG_LIB_S *pstAwbLib)
{
    // ISP_LOG_DEBUG("+\n");
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstAwbLib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_S32            s32Ret = CVI_SUCCESS;
    ISP_AWB_REGISTER_S awbAlgo;

    awbAlgo.stAwbExpFunc.pfn_awb_init = awbInit;
    awbAlgo.stAwbExpFunc.pfn_awb_run  = awbRun;
    awbAlgo.stAwbExpFunc.pfn_awb_ctrl = awbCtrl;
    awbAlgo.stAwbExpFunc.pfn_awb_exit = awbExit;

    s32Ret = CVI_ISP_AWBLibRegCallBack(ViPipe, pstAwbLib, &awbAlgo);

#ifndef ARCH_RTOS_CV181X  // TODO@CV181X
    ISP_AWB_DEBUG_FUNC_S awbDebug;

    awbDebug.pfn_awb_dumplog        = AWB_DumpLog;
    awbDebug.pfn_awb_setdumplogpath = AWB_SetDumpLogPath;
    CVI_ISP_AWBLibRegInternalCallBack(ViPipe, &awbDebug);

    isp_proc_regAAAGetVerCb(ViPipe, ISP_AAA_TYPE_AWB, AWB_GetAlgoVer);
#endif

    return s32Ret;
}

CVI_S32 CVI_AWB_UnRegister(VI_PIPE ViPipe, ALG_LIB_S *pstAwbLib)
{
    // ISP_LOG_DEBUG("+\n");
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstAwbLib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_S32 s32Ret = CVI_SUCCESS;

    s32Ret = CVI_ISP_AWBLibUnRegCallBack(ViPipe, pstAwbLib);
#ifndef ARCH_RTOS_CV181X  // TODO@CV181X
    CVI_ISP_AWBLibUnRegInternalCallBack(ViPipe);
    isp_proc_unRegAAAGetVerCb(ViPipe, ISP_AAA_TYPE_AWB);
#endif
    return s32Ret;
}

CVI_S32 CVI_AWB_SensorRegCallBack(VI_PIPE ViPipe, ALG_LIB_S *pstAwbLib,
                                  ISP_SNS_ATTR_INFO_S   *pstSnsAttrInfo,
                                  AWB_SENSOR_REGISTER_S *pstRegister)
{
    // ISP_LOG_DEBUG("+\n");
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstAwbLib == CVI_NULL) {
        return CVI_FAILURE;
    }

    if (pstSnsAttrInfo == CVI_NULL) {
        return CVI_FAILURE;
    }

    if (pstRegister == CVI_NULL) {
        return CVI_FAILURE;
    }

    AWB_CTX_S *pstAwbCtx;

    pstAwbCtx = AWB_GET_CTX(ViPipe);
    memcpy((CVI_VOID *)(&(pstAwbCtx->stSnsAttrInfo)), (CVI_VOID *)pstSnsAttrInfo,
           sizeof(ISP_SNS_ATTR_INFO_S));
    pstAwbCtx->IspBindDev = ViPipe;
    memcpy(&(pstAwbCtx->stSnsRegister), pstRegister, sizeof(AWB_SENSOR_REGISTER_S));

    if (pstAwbCtx->stSnsRegister.stAwbExp.pfn_cmos_get_awb_default != CVI_NULL)
        pstAwbCtx->stSnsRegister.stAwbExp.pfn_cmos_get_awb_default(ViPipe, &pstAwbCtx->stSnsDft);
    pstAwbCtx->bSnsRegister = 1;

    return CVI_SUCCESS;
}

CVI_S32 CVI_AWB_SensorUnRegCallBack(VI_PIPE ViPipe, ALG_LIB_S *pstAwbLib, SENSOR_ID SensorId)
{
    ISP_LOG_NOTICE("+%d\n", SensorId);
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstAwbLib == CVI_NULL) {
        return CVI_FAILURE;
    }

    AWB_CTX_S *pstAwbCtx;
    /*TODO. Need check HI "s32handle" means.*/
    pstAwbCtx = AWB_GET_CTX(ViPipe);
    memset(&(pstAwbCtx->stSnsRegister), 0, sizeof(AWB_SENSOR_REGISTER_S));
    memset(&(pstAwbCtx->stSnsDft), 0, sizeof(AWB_SENSOR_DEFAULT_S));
    pstAwbCtx->bSnsRegister = 0;

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_SetWBAttr(VI_PIPE ViPipe, const ISP_WB_ATTR_S *pstWBAttr)
{
    // ISP_LOG_DEBUG("+\n");
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstWBAttr == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_CheckSensorID(ViPipe);

    AWB_SetAttr(sID, pstWBAttr);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_SetAWBAttrEx(VI_PIPE ViPipe, const ISP_AWB_ATTR_EX_S *pstAWBAttrEx)
{
    // ISP_LOG_DEBUG("+\n");
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstAWBAttrEx == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_CheckSensorID(ViPipe);

    AWB_SetAttrEx(sID, pstAWBAttrEx);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetWBAttr(VI_PIPE ViPipe, ISP_WB_ATTR_S *pstWBAttr)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWBAttr == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetAttr(sID, pstWBAttr);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetAWBAttrEx(VI_PIPE ViPipe, ISP_AWB_ATTR_EX_S *pstAWBAttrEx)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstAWBAttrEx == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetAttrEx(sID, pstAWBAttrEx);

    return CVI_SUCCESS;
}

CVI_S32 CVI_AWB_QueryInfo(VI_PIPE ViPipe, ISP_WB_Q_INFO_S *pstWB_Q_Info)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWB_Q_Info == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetQueryInfo(sID, pstWB_Q_Info);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_SetWBCalibration(VI_PIPE ViPipe, const ISP_AWB_Calibration_Gain_S *pstWBCalib)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWBCalib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_SetCalibration(sID, pstWBCalib);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetWBCalibration(VI_PIPE ViPipe, ISP_AWB_Calibration_Gain_S *pstWBCalib)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWBCalib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetCalibration(sID, pstWBCalib);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_SetWBCalibrationEx(VI_PIPE ViPipe, const ISP_AWB_Calibration_Gain_S_EX *pstWBCalib)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWBCalib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_SetCalibrationEx(sID, pstWBCalib);
    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetWBCalibrationEx(VI_PIPE ViPipe, ISP_AWB_Calibration_Gain_S_EX *pstWBCalib)
{
    // ISP_LOG_DEBUG("+\n");
    if (pstWBCalib == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetCalibrationEx(sID, pstWBCalib);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetAWBSnapLogBuf(VI_PIPE ViPipe, CVI_U8 *buf, CVI_U32 bufSize)
{
    // ISP_LOG_DEBUG("+\n");
    if (buf == CVI_NULL) {
        return CVI_FAILURE;
    }

    AWB_GetSnapLogBuf(ViPipe, buf, bufSize);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetAWBCurve(VI_PIPE ViPipe, ISP_WB_CURVE_S *pshWBCurve)
{
    // ISP_LOG_DEBUG("+\n");
    if (pshWBCurve == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetCurveWhiteZone(sID, pshWBCurve);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetAWBDbgBinBuf(VI_PIPE ViPipe, CVI_U8 *buf, CVI_U32 bufSize)
{
    // ISP_LOG_DEBUG("+\n");
    if (buf == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetDbgBinBuf(sID, buf, bufSize);

    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_GetAWBDbgBinSize(void)
{
    return AWB_GetDbgBinSize();
}

CVI_S32 CVI_ISP_CalGainByTemp(VI_PIPE ViPipe, const ISP_WB_ATTR_S *pstWBAttr, CVI_U16 u16ColorTemp,
                              CVI_S16 s16Shift, CVI_U16 *pu16AWBGain)
{
    if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
        ISP_LOG_ERR("ViPipe %d value error\n", ViPipe);
        return -ENODEV;
    }

    if (pstWBAttr == CVI_NULL) {
        return CVI_FAILURE;
    }

    CVI_U8 sID = AWB_CheckSensorID(ViPipe);

    AWB_GetRBGainByColorTemperature(sID, u16ColorTemp, s16Shift, pu16AWBGain);
    return CVI_SUCCESS;
}

CVI_S32 CVI_ISP_SetAWBLogPath(const char *szPath)
{
    CVI_BOOL result;

    result = AWB_SetDumpLogPath(szPath);

    return result;
}

CVI_S32 CVI_ISP_SetAWBLogName(const char *szName)
{
    CVI_BOOL result;

    result = AWB_SetDumpLogName(szName);

    return result;
}

CVI_S32 CVI_ISP_GetGrayWorldAwbInfo(VI_PIPE ViPipe, CVI_U16 *pRgain, CVI_U16 *pBgain)
{
    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_GetGrayWorldWB(sID, pRgain, pBgain);
    return CVI_SUCCESS;
}

void CVI_ISP_SetAwbSimMode(CVI_BOOL bMode)
{
    AWB_SetAwbSimMode(bMode);
}

CVI_BOOL CVI_ISP_GetAwbSimMode(void)
{
    return AWB_GetAwbSimMode();
}

CVI_BOOL CVI_ISP_GetAwbRunStatus(VI_PIPE ViPipe)
{
    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    return AWB_GetAwbRunStatus(sID);
}

void CVI_ISP_SetAwbRunStatus(VI_PIPE ViPipe, CVI_BOOL bState)
{
    CVI_U8 sID = AWB_ViPipe2sID(ViPipe);

    AWB_SetAwbRunStatus(sID, bState);
}
