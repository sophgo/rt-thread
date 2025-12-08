/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: afalgo.c
 * Description:
 *
 */

#define AF_LIB_VER    (1)  // U8
#define AF_LIB_SUBVER (1)  // U8

#include "afalgo.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>  //for gettimeofday()
#include <time.h>
#include <unistd.h>  //for usleep()

#include "cvi_comm_3a.h"
#include "cvi_isp.h"
#include "cvi_mw_base.h"
#include "isp_debug.h"
#include "isp_main.h"
void AF_GetAlgoVer(CVI_U16 *pVer, CVI_U16 *pSubVer)
{
    *pVer    = AF_LIB_VER;
    *pSubVer = AF_LIB_SUBVER;
}
