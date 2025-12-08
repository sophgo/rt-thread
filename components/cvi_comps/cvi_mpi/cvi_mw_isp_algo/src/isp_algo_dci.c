/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: isp_algo_dci.c
 * Description:
 *
 */

#include "isp_algo_dci.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "isp_algo_debug.h"
#include "isp_algo_utility.h"
#include "limits.h"

#define DCI_OUT_DATA_DIMENSION (10)
#define DCI_OUT_DATA_MAX_VALUE ((1 << DCI_OUT_DATA_DIMENSION) - 1)
#define DCI_BINS_NUM           (256)
#define DCI_STR_BASE           9

static int8_t    wbuf_alloc_cnt;
static uint32_t *pHistb;
static uint32_t *linear_tone;

static int  DCI_API(uint32_t *pHist, const int nbins, uint32_t img_width, uint32_t img_height,
                    uint32_t ContrastGain, uint16_t dci_strength, unsigned int *pMapHist,
                    uint16_t DciGamma, uint8_t DciOffset, uint8_t bw_strecth_black_th,
                    uint8_t bw_strecth_white_th, uint16_t bw_strecth_black_str,
                    uint16_t bw_strecth_white_str, uint8_t ToleranceY, uint16_t DciGainMax,
                    uint8_t Method);
static int  DCI_Blending_API(uint16_t *pMapHist, uint32_t *pMapHist_prev, uint16_t *pMapHist_output,
                             uint16_t bins_num, uint16_t weight_prev);
static int  u32GetLinearTargetY(uint32_t *hist, int nbins, uint32_t base);
static void u32ClipHist(unsigned int *pHist, unsigned int nbins, unsigned int ClipLimit);
static void u32MapHist(uint32_t *pHist, uint32_t *dst, uint32_t min_val, uint32_t max_val,
                       uint32_t dci_strength, uint32_t nbins, uint32_t nPixels);
static uint32_t u32CalcAvgY(uint32_t *tone, uint32_t *hist, int nbins);
static void     BWStretch(unsigned int *pHist, unsigned int nbins, uint32_t bw_strecth_black_th,
                          uint32_t bw_strecth_white_th, uint32_t bw_strecth_black_str,
                          uint32_t bw_strecth_white_str);

CVI_S32 isp_algo_dci_main(struct dci_param_in *dci_param_in, struct dci_param_out *dci_param_out)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32 ret = CVI_SUCCESS;

    if (dci_param_in->bUpdateCurve) {
        uint32_t pHist[DCI_BINS_NUM]    = {0};
        uint32_t pMapHist[DCI_BINS_NUM] = {0};

        for (uint32_t i = 0; i < DCI_BINS_NUM; i++) {
            pHist[i] = ISP_PTR_CAST_U16(dci_param_in->pHist)[i];
        }

        DCI_API(pHist, dci_param_in->dci_bins_num, dci_param_in->img_width,
                dci_param_in->img_height, dci_param_in->ContrastGain, dci_param_in->DciStrength,
                pMapHist, dci_param_in->DciGamma, dci_param_in->DciOffset, dci_param_in->BlcThr,
                dci_param_in->WhtThr, dci_param_in->BlcCtrl, dci_param_in->WhtCtrl,
                dci_param_in->ToleranceY, dci_param_in->DciGainMax, dci_param_in->Method);

        for (uint32_t i = 0; i < DCI_BINS_NUM; i++)
            ISP_PTR_CAST_U16(dci_param_in->pCntCurve)[i] = pMapHist[i];
    }

    DCI_Blending_API(
        ISP_PTR_CAST_U16(dci_param_in->pCntCurve), ISP_PTR_CAST_U32(dci_param_in->pPreDCILut),
        ISP_PTR_CAST_U16(dci_param_out->map_lut), dci_param_in->dci_bins_num, dci_param_in->Speed);

    return ret;
}

CVI_S32 isp_algo_dci_init(void)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32 ret = CVI_SUCCESS;

    DCI_wbuf_alloc(DCI_BINS_NUM);

    return ret;
}

CVI_S32 isp_algo_dci_uninit(void)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32 ret = CVI_SUCCESS;

    DCI_wbuf_free();

    return ret;
}

/**
 * @brief [Entry] DCI
 *
 * @param[out] pMapHist       Mapping histogram
 * @param[in]  pHist          Histogram of image from driver
 * @param[in]  img_width      Image width
 * @param[in]  img_height     Image height
 * @param[in]  ContrastGain   Strength of DCI
 */
static int DCI_API(uint32_t *pHist, const int nbins, uint32_t img_width, uint32_t img_height,
                   uint32_t ContrastGain, uint16_t dci_strength, unsigned int *pMapHist,
                   uint16_t DciGamma, uint8_t DciOffset, uint8_t bw_strecth_black_th,
                   uint8_t bw_strecth_white_th, uint16_t bw_strecth_black_str,
                   uint16_t bw_strecth_white_str, uint8_t ToleranceY, uint16_t DciGainMax,
                   uint8_t Method)
{
    const int min_val = 0;
    const int max_val = 255;

    uint32_t avgY, targetY, heY;
    uint32_t dci_str, step_size;
    int32_t  direction = 1;
    uint32_t nPixels   = 0;

    uint32_t targetYBase = (Method > 0) ? 255 : 1023;
    // Method 0: 10bit domain
    // Method 1: 8bit domain

    avgY = u32GetLinearTargetY(pHist, nbins, targetYBase);

    if (avgY > 128) direction = -1;

    memcpy(pHistb, pHist, sizeof(uint32_t) * nbins);

    targetY = ((avgY * (256 - ContrastGain)) >> 8) + ((128 * ContrastGain) >> 8);

    // printf("avg(%d), target(%d)\n", avgY, targetY);

    dci_str   = (1 << DCI_STR_BASE);
    step_size = (1 << (DCI_STR_BASE - 1));

    for (int i = 0; i < (DCI_STR_BASE - 1); i++) {
        memcpy(pHist, pHistb, sizeof(uint32_t) * nbins);

        uint32_t ClipLimit = (dci_str / 64.0) * ((img_width * img_height) / 256.0);

        // Clip histogram
        u32ClipHist(pHist, nbins, ClipLimit);

        nPixels = 0;

        for (int n = 0; n < nbins; n++) nPixels += pHist[n];

        // Mapping histogram
        u32MapHist(pHist, pMapHist, min_val, max_val, dci_strength, nbins, nPixels);

        heY = u32CalcAvgY(pMapHist, pHistb, nbins);

        uint32_t val = MAX2(targetY, heY) - MIN2(targetY, heY);

        if (val <= ToleranceY)
            break;
        else if (heY > targetY)
            dci_str -= step_size * direction;
        else
            dci_str += step_size * direction;
        step_size >>= 1;

        if (ClipLimit > 0xFFFF) break;

        // printf("dci_str(%d), step_size(%d), AvgY after HE is %d\n", dci_str, step_size, heY);
    }

    // u32PWLTone(pMapHist, min_val, max_val, nbins, 4);
    BWStretch(pHist, nbins, bw_strecth_black_th, bw_strecth_white_th, bw_strecth_black_str,
              bw_strecth_white_str);

    // Slope Constraint
    // int SlopeMax = 16;		// 16 = 1x gain
    uint32_t ClipLimitMax = ((img_width * img_height) / 256.0) * DciGainMax / 16;

    u32ClipHist(pHist, nbins, ClipLimitMax);

    nPixels = 0;
    for (int n = 0; n < nbins; n++) nPixels += pHist[n];

    u32MapHist(pHist, pMapHist, min_val, DCI_OUT_DATA_MAX_VALUE, dci_strength, nbins, nPixels);

    if ((DciOffset != 0) && (DciGamma != 100)) {
        float fDciGamma = (float)DciGamma / 100.0;

        // Inverse Gamma
        for (int n = 0; n < nbins; n++) {
            float input = (float)((pMapHist[n] + DciOffset) / (float)DCI_OUT_DATA_MAX_VALUE);

            pMapHist[n] = (unsigned int)(DCI_OUT_DATA_MAX_VALUE * powf(input, fDciGamma));
            pMapHist[n] = MIN(pMapHist[n], DCI_OUT_DATA_MAX_VALUE);
        }
    }

    return 0;
}

/**
 * @brief [Entry] DCI Mapping Histogram Blending
 *
 * @param[out] pMapHist_output Output Mapping histogram
 * @param[in]  pMapHist        Current Mapping histogram
 * @param[in]  pMapHist_prev   Previous Mapping histogram
 * @param[in]  weight_prev     Weight for previous mapping histogram
 */

#define DCI_IIR_COEF_MAX_500

static int DCI_Blending_API(uint16_t *pMapHist, uint32_t *pMapHist_prev, uint16_t *pMapHist_output,
                            uint16_t bins_num, uint16_t weight_prev)
{
#define DCI_BLENDING_SHIFT_ROUND_OFFSET (512)
#define DCI_BLENDING_SHIFT_BITS         (10)
#define DCI_BLENDING_ORIG_BIN_MAX       (1023)
#define DCI_BLENDING_SHIFT_BIN_MAX      (DCI_BLENDING_ORIG_BIN_MAX << DCI_BLENDING_SHIFT_BITS)

    const uint32_t wt_cur = 1;
    uint32_t       pre_weight, weight_base;

    // if average frame = 0. let
    pre_weight  = (weight_prev < wt_cur) ? 0 : weight_prev;
    weight_base = pre_weight + wt_cur;
    if (pre_weight == 0) {
        for (int n = 0; n < bins_num; n++) {
            pMapHist_output[n] = MIN(pMapHist[n], DCI_BLENDING_ORIG_BIN_MAX);
            pMapHist_prev[n]   = (uint32_t)pMapHist_output[n] << DCI_BLENDING_SHIFT_BITS;
        }
    } else {
#ifdef DCI_IIR_COEF_MAX_500
#define uintprt uint32_t
#else
#define uintprt uint64_t
#endif  //
        for (int n = 0; n < bins_num; n++) {
            uintprt val1, val2, val;

            val1             = ((uintprt)pMapHist[n] << DCI_BLENDING_SHIFT_BITS) * wt_cur;
            val2             = pMapHist_prev[n] * pre_weight;
            val              = (val1 + val2) / weight_base;
            pMapHist_prev[n] = MIN((uint32_t)val, DCI_BLENDING_SHIFT_BIN_MAX);
            pMapHist_output[n] =
                (pMapHist_prev[n] + DCI_BLENDING_SHIFT_ROUND_OFFSET) >> DCI_BLENDING_SHIFT_BITS;
            pMapHist_output[n] = MIN(pMapHist_output[n], DCI_BLENDING_ORIG_BIN_MAX);
        }
#undef uintprt
    }
    return 0;
}

CVI_S32 DCI_wbuf_alloc(uint32_t num)
{
    if (wbuf_alloc_cnt == 0) {
        pHistb      = (uint32_t *)malloc(num * sizeof(uint32_t));
        linear_tone = (uint32_t *)malloc(num * sizeof(uint32_t));

        if ((pHistb == NULL) || (linear_tone == NULL)) {
            return CVI_FAILURE;
        }
    }
    wbuf_alloc_cnt++;

    return CVI_SUCCESS;
}

CVI_S32 DCI_wbuf_free(void)
{
    wbuf_alloc_cnt--;
    if (wbuf_alloc_cnt < 0) {
        ISP_ALGO_LOG_ERR("alloc & free unpaired (%d)\n", wbuf_alloc_cnt);
    } else if (wbuf_alloc_cnt == 0) {
        if (pHistb) free(pHistb);
        if (linear_tone) free(linear_tone);
    }

    return CVI_SUCCESS;
}

static uint32_t u32CalcAvgY(uint32_t *tone, uint32_t *hist, int nbins)
{
    int      i;
    uint64_t sum         = 0;
    uint32_t total_pixel = 0, avgY;

    for (i = 0; i < nbins; i++) {
        sum += tone[i] * hist[i];
        total_pixel += hist[i];
    }

    avgY = sum / (total_pixel + 1);

    return avgY;
}

static int u32GetLinearTargetY(uint32_t *hist, int nbins, uint32_t base)
{
    for (int n = 0; n < nbins; n++) {
        linear_tone[n] = (n * base) / nbins;
    }
    return u32CalcAvgY(linear_tone, hist, nbins);
}

void u32PWLTone(uint32_t *tone, uint32_t min_val, uint32_t max_val, uint32_t nbins, int seg_num)
{
    int      prev_n, next_n;
    uint32_t prev_y, next_y;
    int      i;
    uint32_t next_target_y;

    prev_n        = 0;
    prev_y        = tone[0];
    next_target_y = min_val + (max_val - min_val) / seg_num;
    for (uint32_t n = 1; n < nbins; n++) {
        if (tone[n] >= next_target_y || n == (nbins - 1)) {
            next_n = n;
            next_y = tone[n];
            for (i = prev_n; i < next_n; i++)
                tone[i] = ((i - prev_n) * next_y + (next_n - i) * prev_y) / (next_n - prev_n);
            prev_n = next_n;
            prev_y = next_y;
            next_target_y += (max_val - min_val) / seg_num;
        }
    }
}

/**
 * @brief Clip histogram using @param ClipLimit
 */
static void u32ClipHist(unsigned int *pHist, unsigned int nbins, unsigned int ClipLimit)
{
    uint32_t nExcess, nExcessPerBin;
    int32_t  ExcessBin;

    nExcess = 0;
    for (uint32_t i = 0; i < nbins; i++) {
        ExcessBin = pHist[i] - ClipLimit;
        if (ExcessBin > 0) {
            nExcess += ExcessBin;
        }
    }

    nExcessPerBin = nExcess / nbins;

    for (uint32_t i = 0; i < nbins; i++) {
        if (pHist[i] >= ClipLimit) {
            pHist[i] = ClipLimit + nExcessPerBin;
        } else {
            pHist[i] += nExcessPerBin;
        }
    }
}

/* @brief Black & white stretching */
static void BWStretch(unsigned int *pHist, unsigned int nbins, uint32_t bw_strecth_black_th,
                      uint32_t bw_strecth_white_th, uint32_t bw_strecth_black_str,
                      uint32_t bw_strecth_white_str)
{
    unsigned int *ptr    = NULL;
    uint32_t      n      = 0;
    unsigned int  tmp32  = 0;
    unsigned int  BinMax = UINT_MAX;

    float bw_black_str = (float)bw_strecth_black_str / 256.0;
    float bw_white_str = (float)bw_strecth_white_str / 256.0;

    ptr = pHist;
    while (n < nbins) {
        if (n < bw_strecth_black_th) {
            tmp32 = (*ptr);
            tmp32 = (unsigned int)((float)tmp32 * bw_black_str + 0.5);
            tmp32 = (tmp32 > BinMax) ? BinMax : tmp32;
            *ptr  = tmp32;
        } else if (n > bw_strecth_white_th) {
            tmp32 = (*ptr);
            tmp32 = (unsigned int)((float)tmp32 * bw_white_str + 0.5);
            tmp32 = (tmp32 > BinMax) ? BinMax : tmp32;
            *ptr  = tmp32;
        }
        ptr++;
        n++;
    }
}

/**
 * @brief MapHist generated by CDF
 */
static void u32MapHist(uint32_t *pHist, uint32_t *dst, uint32_t min_val, uint32_t max_val,
                       uint32_t dci_strength, uint32_t nbins, uint32_t nPixels)
{
    uint32_t sum_val       = 0;
    uint32_t new_max_val   = min_val + ((dci_strength * (max_val - min_val)) >> 8);
    uint32_t dynamic_range = new_max_val - min_val;

    if (nPixels == 0) {
        return;
    }

    nPixels = nPixels - pHist[0];
    dst[0]  = 0;

    for (uint32_t i = 1; i < nbins; i++) {
        sum_val += pHist[i];
        dst[i] = (uint32_t)(min_val + ((sum_val * dynamic_range + (nPixels >> 1)) / (nPixels + 1)));
        if (dst[i] > new_max_val) dst[i] = new_max_val;
    }

    // Linear Hist. Set the minimum slope as (256-strengh)/256
    for (uint32_t i = 0; i < nbins; i++) {
        dst[i] += ((max_val - min_val) * i * (256 - dci_strength)) / (256 * 256);
    }
}
