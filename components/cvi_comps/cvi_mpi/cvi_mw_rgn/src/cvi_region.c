#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/queue.h>
#include <pthread.h>
#include <stdatomic.h>
#include <inttypes.h>

#include <fcntl.h>		/* low-level i/o */
#include <unistd.h>

#include "cvi_base.h"
#include "cvi_mw_base.h"
#include "cvi_sys.h"
#include "cvi_vpss.h"
#include "cvi_region.h"
#include "rgn_ioctl.h"
#include "osdc.h"

struct rgn_canvas {
	STAILQ_ENTRY(rgn_canvas) stailq;
	RGN_HANDLE Handle;
	CVI_U64 u64PhyAddr;
	CVI_U8 *pu8VirtAddr;
	CVI_U32 u32Size;
};

struct cvi_rgn_bitmap {
	CVI_VOID *pBitmapVAddr;
	CVI_U32 u32BitmapSize;
};

static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t canvas_q_lock = PTHREAD_MUTEX_INITIALIZER;
STAILQ_HEAD(rgn_canvas_q, rgn_canvas) canvas_q;

static void rgn_init(void)
{
	STAILQ_INIT(&canvas_q);
}

/**************************************************************************
 *   RGN basic APIs.
 **************************************************************************/
extern struct vdev dev_rgn;

CVI_S32 rgn_dev_open(CVI_VOID)
{
	if (dev_rgn.state != VDEV_STATE_OPEN) {
		// rgn
		strncpy(dev_rgn.name, "RGN", sizeof(dev_rgn.name));
		rgn_open();
		dev_rgn.state = VDEV_STATE_OPEN;
	}

	return CVI_SUCCESS;
}

CVI_S32 get_rgn_fd(CVI_VOID)
{
	if (dev_rgn.fd <= 0)
		rgn_dev_open();

	return dev_rgn.fd;
}

CVI_S32 rgn_dev_close(CVI_VOID)
{
	if (dev_rgn.state != VDEV_STATE_CLOSED) {
		rgn_close();
		dev_rgn.state = VDEV_STATE_CLOSED;
	}

	return CVI_SUCCESS;
}

/**************************************************************************
 *   Public APIs.
 **************************************************************************/
CVI_S32 CVI_RGN_Create(RGN_HANDLE Handle, const RGN_ATTR_S *pstRegion)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstRegion);
	rgn_dev_open();

	// Driver control
	fd = get_rgn_fd();
	if (rgn_create(fd, Handle, pstRegion) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Create RGN fail.\n");
		return CVI_FAILURE;
	}

	pthread_mutex_lock(&canvas_q_lock);
	pthread_once(&once, rgn_init);
	pthread_mutex_unlock(&canvas_q_lock);

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_Destroy(RGN_HANDLE Handle)
{
	CVI_S32 fd = -1;

	// Driver control
	fd = get_rgn_fd();
	if (rgn_destroy(fd, Handle) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Destroy RGN fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_GetAttr(RGN_HANDLE Handle, RGN_ATTR_S *pstRegion)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstRegion);

	// Driver control
	fd = get_rgn_fd();
	if (rgn_get_attr(fd, Handle, pstRegion) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Get RGN attributes fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_SetAttr(RGN_HANDLE Handle, const RGN_ATTR_S *pstRegion)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstRegion);
	// Driver control
	fd = get_rgn_fd();
	if (rgn_set_attr(fd, Handle, pstRegion) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Set RGN attributes fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_SetBitMap(RGN_HANDLE Handle, const BITMAP_S *pstBitmap)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstBitmap);
	// Driver control
	fd = get_rgn_fd();
	if (rgn_set_bit_map(fd, Handle, pstBitmap) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Set RGN Bitmap fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_AttachToChn(RGN_HANDLE Handle, const MMF_CHN_S *pstChn, const RGN_CHN_ATTR_S *pstChnAttr)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChn);
	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChnAttr);

#ifdef __CV180X__
	if (pstChn->enModId == CVI_ID_VO) {
		CVI_TRACE_RGN(CVI_DBG_ERR, "No vo device, cannot attach to vo!\n");
		return CVI_ERR_RGN_ILLEGAL_PARAM;
	}
#endif

	// Driver control
	fd = get_rgn_fd();
	if (rgn_attach_to_chn(fd, Handle, pstChn, pstChnAttr) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Attach RGN to channel fail.\n");
		return CVI_FAILURE;
	}
	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_DetachFromChn(RGN_HANDLE Handle, const MMF_CHN_S *pstChn)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChn);

#ifdef __CV180X__
	if (pstChn->enModId == CVI_ID_VO) {
		CVI_TRACE_RGN(CVI_DBG_ERR, "No vo device, cannot detach from vo!\n");
		return CVI_ERR_RGN_ILLEGAL_PARAM;
	}
#endif

	// Driver control
	fd = get_rgn_fd();
	if (rgn_detach_from_chn(fd, Handle, pstChn) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Detach RGN from channel fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_SetDisplayAttr(RGN_HANDLE Handle, const MMF_CHN_S *pstChn, const RGN_CHN_ATTR_S *pstChnAttr)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChn);
	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChnAttr);

	// Driver control
	fd = get_rgn_fd();
	if (rgn_set_display_attr(fd, Handle, pstChn, pstChnAttr) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Set display RGN attributes fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_GetDisplayAttr(RGN_HANDLE Handle, const MMF_CHN_S *pstChn, RGN_CHN_ATTR_S *pstChnAttr)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChn);
	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChnAttr);

	// Driver control
	fd = get_rgn_fd();
	if (rgn_get_display_attr(fd, Handle, pstChn, pstChnAttr) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Get display RGN attributes fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_GetCanvasInfo(RGN_HANDLE Handle, RGN_CANVAS_INFO_S *pstCanvasInfo)
{
	CVI_S32 s32Ret;
	CVI_S32 fd = -1;
	struct rgn_canvas *canvas;
	struct cvi_rgn_ctx ctx;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstCanvasInfo);

	// Driver control
	fd = get_rgn_fd();

	s32Ret = cvi_rgn_get_ctx(fd, Handle, &ctx);
	if (s32Ret != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_ERR, "Get context failed!\n");
		return CVI_FAILURE;
	}

	if (rgn_get_canvas_info(fd, Handle, pstCanvasInfo) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Get RGN canvas information fail.\n");
		return CVI_FAILURE;
	}

	canvas = calloc(sizeof(struct rgn_canvas), 1);
	if (!canvas) {
		CVI_TRACE_RGN(CVI_DBG_ERR, "calloc failed.\n");
		return CVI_ERR_RGN_NOMEM;
	}

	pthread_mutex_lock(&canvas_q_lock);
	canvas->u64PhyAddr = pstCanvasInfo->u64PhyAddr;
	canvas->u32Size = ctx.ion_len;
	pstCanvasInfo->pu8VirtAddr = (CVI_U8 *)pstCanvasInfo->u64PhyAddr;
	canvas->pu8VirtAddr = pstCanvasInfo->pu8VirtAddr;
	if (pstCanvasInfo->pu8VirtAddr == NULL) {
		free(canvas);
		CVI_TRACE_RGN(CVI_DBG_INFO, "CVI_SYS_Mmap NG.\n");
		return CVI_FAILURE;
	}
	canvas->Handle = Handle;
	pstCanvasInfo->pstCanvasCmprAttr = (RGN_CANVAS_CMPR_ATTR_S *)pstCanvasInfo->pu8VirtAddr;
	pstCanvasInfo->pstObjAttr = (RGN_CMPR_OBJ_ATTR_S *)(pstCanvasInfo->pu8VirtAddr +
			sizeof(RGN_CANVAS_CMPR_ATTR_S));
	STAILQ_INSERT_TAIL(&canvas_q, canvas, stailq);

	pthread_mutex_unlock(&canvas_q_lock);

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_UpdateCanvas(RGN_HANDLE Handle)
{

	CVI_S32 s32Ret;
	CVI_S32 fd = -1;
	struct rgn_canvas *canvas;
	struct cvi_rgn_ctx ctx;
	struct rgn_canvas *rgnCanvasMap = NULL;
	CVI_U32 u32Bpp, i, j = 0;

	// Driver control
	fd = get_rgn_fd();

	s32Ret = cvi_rgn_get_ctx(fd, Handle, &ctx);

	if (!STAILQ_EMPTY(&canvas_q)) {
		STAILQ_FOREACH(rgnCanvasMap, &canvas_q, stailq) {
			if (rgnCanvasMap->Handle == Handle) {
				break;
			}
		}
	}

	if (ctx.stCanvasInfo[ctx.canvas_idx].bCompressed) {
		RGN_CANVAS_CMPR_ATTR_S *pstCanvasCmprAttr = NULL;
		RGN_CMPR_OBJ_ATTR_S *pstObjAttr = NULL;
		CVI_S32 status;
		OSDC_Canvas_Attr_S osdc_canvas;
		OSDC_DRAW_OBJ_S *obj_vec;
		CVI_U32 bs_size;
		struct cvi_rgn_bitmap *pstBitmaps;

		//cmpr canvas info is stored in ion which needs to be flush before reading
		CVI_SYS_IonFlushCache(rgnCanvasMap->u64PhyAddr, rgnCanvasMap->pu8VirtAddr, rgnCanvasMap->u32Size);
		pstCanvasCmprAttr = (RGN_CANVAS_CMPR_ATTR_S *)rgnCanvasMap->pu8VirtAddr;
		pstObjAttr = (RGN_CMPR_OBJ_ATTR_S *)(rgnCanvasMap->pu8VirtAddr +
			sizeof(RGN_CANVAS_CMPR_ATTR_S));

		for (i = 0; i < pstCanvasCmprAttr->u32ObjNum; ++i) {
			if (pstObjAttr[i].enObjType == RGN_CMPR_LINE) {
				CVI_TRACE_RGN(CVI_DBG_DEBUG, "start(%d %d) end(%d %d) Thick(%d) Color(0x%x)\n",
					pstObjAttr[i].stLine.stPointStart.s32X,
					pstObjAttr[i].stLine.stPointStart.s32Y,
					pstObjAttr[i].stLine.stPointEnd.s32X,
					pstObjAttr[i].stLine.stPointEnd.s32Y,
					pstObjAttr[i].stLine.u32Thick,
					pstObjAttr[i].stLine.u32Color);
			} else if (pstObjAttr[i].enObjType == RGN_CMPR_RECT) {
				CVI_TRACE_RGN(CVI_DBG_DEBUG,
					"xywh(%d %d %d %d) Thick(%d) Color(0x%x) is_fill(%d)\n",
					pstObjAttr[i].stRgnRect.stRect.s32X,
					pstObjAttr[i].stRgnRect.stRect.s32Y,
					pstObjAttr[i].stRgnRect.stRect.u32Width,
					pstObjAttr[i].stRgnRect.stRect.u32Height,
					pstObjAttr[i].stRgnRect.u32Thick,
					pstObjAttr[i].stRgnRect.u32Color,
					pstObjAttr[i].stRgnRect.u32IsFill);
			} else if (pstObjAttr[i].enObjType == RGN_CMPR_BIT_MAP) {
				CVI_TRACE_RGN(CVI_DBG_DEBUG, "xywh(%d %d %d %d) u32BitmapPAddr(%x)\n",
					pstObjAttr[i].stBitmap.stRect.s32X,
					pstObjAttr[i].stBitmap.stRect.s32Y,
					pstObjAttr[i].stBitmap.stRect.u32Width,
					pstObjAttr[i].stBitmap.stRect.u32Height,
					pstObjAttr[i].stBitmap.u32BitmapPAddr);
			}
		}

		osdc_canvas.width = ctx.stRegion.unAttr.stOverlay.stSize.u32Width;
		osdc_canvas.height = ctx.stRegion.unAttr.stOverlay.stSize.u32Height;
		osdc_canvas.bg_color_code = ctx.stRegion.unAttr.stOverlay.u32BgColor;
		switch (ctx.stRegion.unAttr.stOverlay.enPixelFormat) {
		case PIXEL_FORMAT_ARGB_8888:
			osdc_canvas.format = OSD_ARGB8888;
			u32Bpp = 4;
			break;

		case PIXEL_FORMAT_ARGB_4444:
			osdc_canvas.format = OSD_ARGB4444;
			u32Bpp = 2;
			break;

		case PIXEL_FORMAT_ARGB_1555:
			osdc_canvas.format = OSD_ARGB1555;
			u32Bpp = 2;
			break;

		case PIXEL_FORMAT_8BIT_MODE:
			osdc_canvas.format = OSD_LUT8;
			u32Bpp = 1;
			break;

		default:
			osdc_canvas.format = OSD_ARGB1555;
			u32Bpp = 2;
			break;
		}

		obj_vec = calloc(sizeof(OSDC_DRAW_OBJ_S) * pstCanvasCmprAttr->u32ObjNum, 1);
		if (!obj_vec) {
			CVI_TRACE_RGN(CVI_DBG_ERR, "calloc size (%zu) failed!\n",
							sizeof(OSDC_DRAW_OBJ_S) * pstCanvasCmprAttr->u32ObjNum);
			return CVI_ERR_RGN_NOBUF;
		}
		pstBitmaps = (struct cvi_rgn_bitmap *)calloc(pstCanvasCmprAttr->u32ObjNum,
						sizeof(struct cvi_rgn_bitmap));
		if (!pstBitmaps) {
			CVI_TRACE_RGN(CVI_DBG_ERR, "calloc size (%zu) failed!\n",
							pstCanvasCmprAttr->u32ObjNum * sizeof(struct cvi_rgn_bitmap));
			free(obj_vec);
			return CVI_ERR_RGN_NOBUF;
		}

		for (i = 0; i < pstCanvasCmprAttr->u32ObjNum; ++i) {
			if (pstObjAttr[i].enObjType == RGN_CMPR_LINE) {
				OSDC_SetLineObjAttr(&osdc_canvas, &obj_vec[i],
				pstObjAttr[i].stLine.u32Color,
					pstObjAttr[i].stLine.stPointStart.s32X,
					pstObjAttr[i].stLine.stPointStart.s32Y,
					pstObjAttr[i].stLine.stPointEnd.s32X,
					pstObjAttr[i].stLine.stPointEnd.s32Y,
					pstObjAttr[i].stLine.u32Thick);
			} else if (pstObjAttr[i].enObjType == RGN_CMPR_RECT) {
				OSDC_SetRectObjAttr(&osdc_canvas, &obj_vec[i],
					pstObjAttr[i].stRgnRect.u32Color,
					pstObjAttr[i].stRgnRect.stRect.s32X,
					pstObjAttr[i].stRgnRect.stRect.s32Y,
					pstObjAttr[i].stRgnRect.stRect.u32Width,
					pstObjAttr[i].stRgnRect.stRect.u32Height,
					pstObjAttr[i].stRgnRect.u32IsFill,
					pstObjAttr[i].stRgnRect.u32Thick);
			} else if (pstObjAttr[i].enObjType == RGN_CMPR_BIT_MAP) {
				pstBitmaps[j].u32BitmapSize = pstObjAttr[i].stBitmap.stRect.u32Width *
								pstObjAttr[i].stBitmap.stRect.u32Height * u32Bpp;
				pstBitmaps[j].pBitmapVAddr = (CVI_VOID *)(uintptr_t)pstObjAttr[i].stBitmap.u32BitmapPAddr;

				OSDC_SetBitmapObjAttr(&osdc_canvas, &obj_vec[i],
					pstBitmaps[j++].pBitmapVAddr,
					pstObjAttr[i].stBitmap.stRect.s32X,
					pstObjAttr[i].stBitmap.stRect.s32Y,
					pstObjAttr[i].stBitmap.stRect.u32Width,
					pstObjAttr[i].stBitmap.stRect.u32Height,
					false);
			}
		}

		status = OSDC_DrawCmprCanvas(&osdc_canvas, &obj_vec[0], pstCanvasCmprAttr->u32ObjNum,
			rgnCanvasMap->pu8VirtAddr, ctx.ion_len,  &bs_size);
		if (status != 1) {
			CVI_TRACE_RGN(CVI_DBG_ERR, "Region(%d) needs ion size(%d), current size(%d)!\n",
				Handle, bs_size, ctx.ion_len);
			OSDC_DrawCmprCanvas(&osdc_canvas, &obj_vec[0], 0,
				rgnCanvasMap->pu8VirtAddr, ctx.ion_len,  &bs_size);
		}
		// if (j) {
		// 	for (i = 0; i < j; i++) {
		// 		CVI_SYS_Munmap(pstBitmaps[i].pBitmapVAddr, pstBitmaps[i].u32BitmapSize);
		// 	}
		// }
		free(pstBitmaps);
		// store bitstream size in bit[32:63], after C906L gets it,
		// it should be restored to image width and height
		*((unsigned int *)rgnCanvasMap->pu8VirtAddr + 1) = bs_size;
		free(obj_vec);
	}

	CVI_SYS_IonFlushCache(rgnCanvasMap->u64PhyAddr, rgnCanvasMap->pu8VirtAddr, rgnCanvasMap->u32Size);

	if (rgn_update_canvas(fd, Handle) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Update RGN canvas fail.\n");
		return CVI_FAILURE;
	}

	pthread_mutex_lock(&canvas_q_lock);
	if (!STAILQ_EMPTY(&canvas_q)) {
		STAILQ_FOREACH(canvas, &canvas_q, stailq) {
			if (canvas->Handle == Handle) {
				STAILQ_REMOVE(&canvas_q, canvas, rgn_canvas, stailq);
				free(canvas);
				break;
			}
		}
	}
	pthread_mutex_unlock(&canvas_q_lock);

	return s32Ret;
}

/* CVI_RGN_Invert_Color - invert color per luma statistics of video content
 *   Chns' pixel-format should be YUV.
 *   RGN's pixel-format should be ARGB1555.
 *
 * @param Handle: RGN Handle
 * @param pstChn: the chn which rgn attached
 * @param pu32Color: rgn's content
 */
CVI_S32 CVI_RGN_Invert_Color(RGN_HANDLE Handle, MMF_CHN_S *pstChn, CVI_U32 *pu32Color)
{
	CVI_S32 fd = -1;

	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pstChn);
	MOD_CHECK_NULL_PTR(CVI_ID_RGN, pu32Color);

	// not supported yet
	return CVI_ERR_RGN_NOT_SUPPORT;

	// Driver control
	fd = get_rgn_fd();
	if (rgn_invert_color(fd, Handle, pstChn, (void *)pu32Color) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Invert RGN color fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

CVI_S32 CVI_RGN_SetChnPalette(RGN_HANDLE Handle, const MMF_CHN_S *pstChn, RGN_PALETTE_S *pstPalette)
{
	struct vdev *d;

	// Driver control
	d = get_dev_info(VDEV_TYPE_RGN, 0);
	if (!IS_VDEV_OPEN(d->state)) {
		CVI_TRACE_RGN(CVI_DBG_ERR, "rgn state(%d) incorrect.", d->state);
		return CVI_ERR_RGN_SYS_NOTREADY;
	}

	if (rgn_set_chn_palette(d->fd, Handle, pstChn, pstPalette) != CVI_SUCCESS) {
		CVI_TRACE_RGN(CVI_DBG_INFO, "Invert RGN color fail.\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}
