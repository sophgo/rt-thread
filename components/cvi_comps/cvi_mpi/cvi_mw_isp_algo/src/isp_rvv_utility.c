/*
 *
 * File Name: isp_rvv_utility.c
 * Description:
 *
 */

#ifdef __riscv_vector
#include "isp_rvv_utility.h"

#include <riscv_vector.h>

void *rvv_memcpy(void *restrict destination, const void *restrict source, size_t n)
{
    unsigned char       *dst = destination;
    const unsigned char *src = source;
    // copy data byte by byte
    for (size_t vl; n > 0; n -= vl, src += vl, dst += vl) {
        vl                 = vsetvl_e8m8(n);
        vuint8m8_t vec_src = vle8_v_u8m8(src, vl);

        vse8_v_u8m8(dst, vec_src, vl);
    }
    return destination;
}

void *rvv_memset(void *destination, int ch, size_t n)
{
    unsigned char *dst    = destination;
    size_t         vlmax  = vsetvlmax_e8m8();
    vuint8m8_t     vec_ch = vmv_v_x_u8m8((unsigned char)ch, vlmax);
    // set data byte by byte
    for (size_t vl; n > 0; n -= vl, dst += vl) {
        vl = vsetvl_e8m8(n);
        vse8_v_u8m8(dst, vec_ch, vl);
    }
    return destination;
}
#endif  // __riscv_vector
