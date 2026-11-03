#ifndef __CVI_TPU_CHIP_CV181X_H__
#define __CVI_TPU_CHIP_CV181X_H__

#include <stdint.h>

/*
 * CVITEK TPU per-chip constants: cv181x (TIU lane count, etc.).
 * Selected at build time by the tpu module SConscript
 * (chip_type -> libraries/tpu/chip/<chip>/); common/ code consumes these.
 */

uint32_t cvi_tpu_tiu_lane_count(void);

#endif /* __CVI_TPU_CHIP_CV181X_H__ */
