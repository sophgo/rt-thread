#ifndef __CVI_DMA_H__
#define __CVI_DMA_H__

/* DW_DMA register layout, descriptor format and engine-level channel
 * operations are provided by the IP library header. This adapter header is
 * kept for the hal_dma_* UAPI implementation and its consumers. The path is
 * written relative to the libraries root (which drivers/ adds to CPPPATH
 * unconditionally), NOT as a bare name: this header is pulled in by board
 * code, spi and i2s even when the drivers.dma group is inactive (e.g.
 * cv18xx_boot without BSP_USING_DMA), and only the libraries root is
 * guaranteed to be on the include path then. */
#include "dma/dw_dma.h"

#endif
