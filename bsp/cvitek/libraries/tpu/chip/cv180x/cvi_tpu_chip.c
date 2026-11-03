/*
 * CVITEK TPU per-chip constants: cv180x (TIU lane count, etc.).
 * Selected at build time by the tpu module SConscript
 * (chip_type -> libraries/tpu/chip/<chip>/); common/ code consumes these.
 */
#include "cvi_tpu_chip.h"

uint32_t cvi_tpu_tiu_lane_count(void)
{
    return 2;
}
