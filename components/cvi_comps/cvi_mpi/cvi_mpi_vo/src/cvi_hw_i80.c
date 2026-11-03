#include "cvi_hw_i80.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <vo_uapi.h>

#include "cvi_debug.h"
#include "cvi_mw_base.h"
#include "vo_ioctl.h"

CVI_S32 CVI_HWI80_Init(VO_DEV VoDev, const HW_I80_CFG_S *pstHwI80Attr)
{
    CVI_S32                  fd = -1;
    struct vo_hwi80_attr_cfg cfg;

    cfg.VoDev = VoDev;

    memcpy(&cfg.pstHwI80Attr, pstHwI80Attr, sizeof(HW_I80_CFG_S));

    fd = get_vo_fd();

    if (vo_sdk_hwi80_init(fd, &cfg) != CVI_SUCCESS) {
        CVI_TRACE_VO(CVI_DBG_ERR, "VoDev(%d) HwI80 Init fail\n", VoDev);
        return CVI_FAILURE;
    }

    return CVI_SUCCESS;
}
