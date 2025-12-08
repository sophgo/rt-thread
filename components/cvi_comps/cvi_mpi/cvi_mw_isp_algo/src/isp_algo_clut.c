/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2021. All rights reserved.
 *
 * File Name: isp_algo_clut.c
 * Description:
 *
 */

// #define SIMULATION 1

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !SIMULATION
#include <sys/time.h>

#include "isp_algo_clut.h"
#include "isp_algo_debug.h"
#include "isp_algo_utility.h"
#endif

#define DEBUG          0
#define DEBUG_PLATFORM 1
#define DEBUG_CONTOUR  0
#if SIMULATION
#define UNIT_TEST 1
#define PNG       1
#else
#define UNIT_TEST 0
#define PNG       0
#endif

#define COLOR_SPACE_HSV 0
#define COLOR_SPACE_YUV 1
#define COLOR_SPACE_LAB 2
#define COLOR_SPACE     COLOR_SPACE_YUV

#if DEBUG
#define debug printf
#else
#define debug(...)  // skip printf
#endif

#if SIMULATION
typedef uint32_t CLUT_DATA;
typedef uint32_t CLUT_ADDRESS;
#define UNUSED(...)
typedef uint32_t CVI_U32;
typedef int32_t  CVI_S32;
typedef uint16_t CVI_U16;
#define CVI_FAILURE -1

#if DEBUG
#define MAX_UPDATE_ITEM 8192  // never over limit
#else
#define MAX_UPDATE_ITEM 8192  // never over limit
// #define MAX_UPDATE_ITEM 256
#endif
#endif

#define LUT_LENGTH 7

typedef struct {
    uint16_t r;  // 0~1023
    uint16_t g;  // 0~1023
    uint16_t b;  // 0~1023
} RGB, SRGB;

typedef struct {
    float h;
    float s;
    float v;
} HSV;

#if SIMULATION
// uint32_t lut[256][2] in driver
typedef struct {
    CLUT_ADDRESS addr;
    CLUT_DATA    data;
} CLUT_UPDATE_ITEM;

typedef struct {
    uint32_t         length;
    CLUT_UPDATE_ITEM items[MAX_UPDATE_ITEM];
} CLUT_UPDATE_LIST;
#endif

typedef struct {
    // b, g, r
    CLUT_DATA clut[17][17][17];
} CLUT_TABLE;

#if SIMULATION
#define MAX2(a, b)              \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })
#define MIN2(a, b)              \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })
#define LIMIT_RANGE(v, min, max) ({ MIN2(MAX2((v), (min)), max); })
#define MAX3(a, b, c)            ({ MAX2(MAX2((a), (b)), (c)); })
#define MIN3(a, b, c)            ({ MIN2(MIN2((a), (b)), (c)); })
// #define ARRAY_LENGTH(array) (sizeof(array) / sizeof(array[0]))
#define ARRAY_LENGTH(array) ARRAY_SIZE(array)
#endif

//-----------------------------------------------------------------------------

typedef struct {
    uint16_t length;
    uint16_t in[LUT_LENGTH];
    uint16_t out[LUT_LENGTH];
} LUT_U16;

typedef struct {
    uint16_t length;
    float    in[LUT_LENGTH];
    float    out[LUT_LENGTH];
} LUT_FLOAT;

typedef LUT_FLOAT CLUT_SATURATION_LUT;

#if !SIMULATION
static const unsigned int ISO_LUT[ISP_AUTO_ISO_STRENGTH_NUM] = {
    100,   200,   400,    800,    1600,   3200,   6400,    12800,
    25600, 51200, 102400, 204800, 409600, 819200, 1638400, 3276800};

#define FILTER 0.1

struct isp_algo_clut_runtime {
    CVI_BOOL tableUpdated;
    CVI_BOOL isUpdated;

    CLUT_TABLE table;
    // CLUT_TABLE tableUpdate;
    CLUT_SATURATION_LUT lutManual;
    CLUT_SATURATION_LUT lutAuto[ISP_AUTO_ISO_STRENGTH_NUM];
    CLUT_UPDATE_LIST    candidateList;
    CLUT_UPDATE_LIST    updateListManual;
    CLUT_UPDATE_LIST    updateListAuto[ISP_AUTO_ISO_STRENGTH_NUM];
};
struct isp_algo_clut_runtime *algo_clut_runtime[VI_MAX_PIPE_NUM];

static struct isp_algo_clut_runtime **_get_algo_clut_runtime(VI_PIPE ViPipe);

static CVI_S32 isp_algo_clut_update_table(CVI_U32 ViPipe, const CVI_U16 *r, const CVI_U16 *g,
                                          const CVI_U16 *b);
static CVI_S32 isp_algo_clut_update_lut(CVI_U32                           ViPipe,
                                        const ISP_CLUT_SATURATION_ATTR_S *saturation_attr);
static CVI_S32 isp_algo_clut_update_iso_table(CVI_U32 ViPipe);
static CVI_S32 isp_algo_clut_interpolate(CVI_U32 ViPipe, struct clut_param_in *clut_param_in,
                                         struct clut_param_out *clut_param_out);
#endif

#if SIMULATION
int32_t LUT_U16_Mapping(const LUT_U16 *lut, uint16_t in, uint16_t *out)
{
    uint16_t length = lut->length;

    if (in <= lut->in[0]) {
        *out = lut->out[0];
        return 0;
    }

    if (in >= lut->in[length - 1]) {
        *out = lut->out[length - 1];
        return 0;
    }

    uint16_t i;

    for (i = 1; i < length && lut->in[i] < in; i++);

    uint16_t x0 = lut->in[i - 1];
    uint16_t x1 = lut->in[i];
    uint16_t y0 = lut->out[i - 1];
    uint16_t y1 = lut->out[i];

    *out = y0 + (in - x0) * (y1 - y0) / (x1 - x0);

    return 0;
}
#endif

static int32_t LUT_FLOAT_Mapping(const LUT_FLOAT *lut, float in, float *out)
{
    uint16_t length = lut->length;

    if (in <= lut->in[0]) {
        *out = lut->out[0];
        return 0;
    }

    if (in >= lut->in[length - 1]) {
        *out = lut->out[length - 1];
        return 0;
    }

    uint16_t i;

    for (i = 1; i < length && lut->in[i] < in; i++);

    float x0 = lut->in[i - 1];
    float x1 = lut->in[i];
    float y0 = lut->out[i - 1];
    float y1 = lut->out[i];

    *out = y0 + (in - x0) * (y1 - y0) / (x1 - x0);

    return 0;
}

#if SIMULATION
static int32_t LUT_FLOAT_GainMap(const LUT_FLOAT *lut, float in, float value, float *out)
{
    uint16_t length = lut->length;

    if (in <= lut->in[0]) {
        *out = value * lut->out[0];
        return 0;
    }

    if (in >= lut->in[length - 1]) {
        *out = value * lut->out[length - 1];
        return 0;
    }

    uint16_t i;

    for (i = 1; i < length && lut->in[i] < in; i++);

    float x0 = lut->in[i - 1];
    float x1 = lut->in[i];
    float y0 = lut->out[i - 1];
    float y1 = lut->out[i];

    *out = value * (y0 + (in - x0) * (y1 - y0) / (x1 - x0));

    return 0;
}
#endif

#if SIMULATION
static int32_t LUT_FLOAT_Print(const LUT_FLOAT *lut)
{
    printf("# LUT_FLOAT_Print\n");
    uint16_t length = lut->length;
    uint16_t i;

    for (i = 1; i < length; i++) {
        printf("lut[%d] : (%f, %f)\n", i, lut->in[i], lut->out[i]);
    }

    return 0;
}
#endif

#if UNIT_TEST
int32_t UT_LUT(void)
{
    // LUT_U16
    {
        LUT_U16 lut = {
            .length = 2,
            .in     = {100, 200},
            .out    = {200, 400},
        };

        uint16_t in[5] = {0, 100, 150, 200, 250};
        uint16_t out[5];
        uint16_t golden[5] = {200, 200, 300, 400, 400};

        for (uint16_t i = 0; i < ARRAY_LENGTH(in); i++) {
            LUT_U16_Mapping(&lut, in[i], &out[i]);
            debug("in=%d, out=%d, golden=%d\n", in[i], out[i], golden[i]);
            assert(out[i] == golden[i]);
        }
    }

    // LUT_FLOAT
    {
        LUT_FLOAT lut = {
            .length = 2,
            .in     = {0.100, 0.200},
            .out    = {0.200, 0.400},
        };

        float in[5] = {0.0, 0.100, 0.150, 0.200, 0.250};
        float out[5];
        float golden[5] = {0.200, 0.200, 0.300, 0.400, 0.400};

        for (uint16_t i = 0; i < ARRAY_LENGTH(in); i++) {
            LUT_FLOAT_Mapping(&lut, in[i], &out[i]);
            debug("in=%f, out=%f, golden=%f\n", in[i], out[i], golden[i]);
            assert(out[i] == golden[i]);
        }
    }
}
#endif

//-----------------------------------------------------------------------------
/**
 *  @see https://en.wikipedia.org/wiki/HSL_and_HSV
 */
static inline int32_t RGB_GetSaturation(const RGB *rgb, float *saturation)
{
    uint16_t max, min;
    uint16_t r = rgb->r;
    uint16_t g = rgb->g;
    uint16_t b = rgb->b;

    max = MAX3(r, g, b);
    min = MIN3(r, g, b);

    *saturation = (max == 0) ? 0 : 1.0 - (float)min / max;

    return 0;
}

static inline int32_t RGB_ConvertToHSV(const RGB *rgb, HSV *hsv)
{
    float max, min;
    float r = rgb->r / 1023.0f;
    float g = rgb->g / 1023.0f;
    float b = rgb->b / 1023.0f;

    max       = MAX3(r, g, b);
    min       = MIN3(r, g, b);
    float mmm = max - min;
    // float mpm = max + min;

    // H
    int16_t h = 0;

    if (max == min) {
        h = 0;
    } else if (max == r && g >= b) {
        h = 60.0f * (g - b) / mmm;
    } else if (max == r && g < b) {
        h = 60.0f * (g - b) / mmm + 360;
    } else if (max == g) {
        h = 60.0f * (b - r) / mmm + 120;
    } else if (max == b) {
        h = 60.0f * (r - g) / mmm + 240;
    }
    assert(h >= 0);
    assert(h <= 360);
    hsv->h = h;

    // S
    hsv->s = (max == 0) ? 0 : 1.0 - (float)min / max;

    // V
    hsv->v = max;

    return 0;
}

static inline int32_t RGB_ConvertToSRGB(const RGB *rgb, SRGB *srgb)
{
    // mask=rgb>0.0031308;
    // SRGB=mask.*(1.055.*rgb.^(1/2.4)-0.055)+(1-mask).*12.92.*rgb;

    float frgb[3] = {rgb->r / 1023.0f, rgb->g / 1023.0f, rgb->b / 1023.0f};
    float fsrgb[3];

    for (uint8_t i = 0; i < 3; i++) {
        fsrgb[i] =
            (frgb[i] > 0.0031308f) ? (1.055f * pow(frgb[i], 1 / 2.4) - 0.055f) : 12.92f * frgb[i];
    }
    srgb->r = fsrgb[0] * 1023.0f + 0.5f;
    srgb->g = fsrgb[1] * 1023.0f + 0.5f;
    srgb->b = fsrgb[2] * 1023.0f + 0.5f;

    return 0;
}

static inline int32_t SRGB_ConvertToRGB(const SRGB *srgb, RGB *rgb)
{
    // a = 0.055;
    // t = 0.04045;

    // rgb = zeros(size(SRGB));
    // rgb(SRGB<=t) = SRGB(SRGB<=t)/12.92;
    // rgb(SRGB> t) = ((SRGB(SRGB> t)+a)/(1+a)).^2.4;

    const float a = 0.055f;
    const float t = 0.04045f;

    float fsrgb[3] = {srgb->r / 1023.0f, srgb->g / 1023.0f, srgb->b / 1023.0f};
    float frgb[3];

    for (uint8_t i = 0; i < 3; i++) {
        frgb[i] = (fsrgb[i] <= t) ? (fsrgb[i] / 12.92f) : pow((fsrgb[i] + a) / (1.0f + a), 2.4f);
    }
    rgb->r = frgb[0] * 1023.0f + 0.5f;
    rgb->g = frgb[1] * 1023.0f + 0.5f;
    rgb->b = frgb[2] * 1023.0f + 0.5f;

    return 0;
}

#if DEBUG
static inline int32_t SRGB_Print(const SRGB *srgb)
{
    printf("srgb(\t%d\t%d\t%d\t)\n", srgb->r, srgb->g, srgb->b);

    return 0;
}
#endif

#if DEBUG
static inline int32_t RGB_Print(const RGB *rgb)
{
    printf("rgb(\t%d\t%d\t%d\t)\n", rgb->r, rgb->g, rgb->b);

    return 0;
}
#endif

/**
 * @see https://en.wikipedia.org/wiki/HSL_and_HSV
 */
static inline int32_t HSV_ConvertToRGB(const HSV *hsv, RGB *rgb)
{
    uint8_t hi = hsv->h / 60;
    float   v  = hsv->v;
    float   s  = hsv->s;
    float   f  = hsv->h / 60.0f - hi;
    float   p  = v * (1.0f - s);
    float   q  = v * (1.0f - f * s);
    float   t  = v * (1.0f - (1.0f - f) * s);
    float   r, g, b;

    switch (hi) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;  /// h=0~60
    case 1:
        r = q;
        g = v;
        b = p;
        break;  // h=60~120
    case 2:
        r = p;
        g = v;
        b = t;
        break;  // h=120~180
    case 3:
        r = p;
        g = q;
        b = v;
        break;  // h=180~240
    case 4:
        r = t;
        g = p;
        b = v;
        break;  // h=240~300
    case 5:
        r = v;
        g = p;
        b = q;
        break;  // h=300~360
    default:
        ISP_ALGO_LOG_DEBUG("unexpected case\n");
        break;  /// h=0~60
    }

    rgb->r = r * 1023.0f + 0.5f;
    rgb->g = g * 1023.0f + 0.5f;
    rgb->b = b * 1023.0f + 0.5f;

    return 0;
}

#if (COLOR_SPACE == COLOR_SPACE_HSV)
static int32_t RGB_GetSaturationInHSV(const RGB *rgb, float *saturation)
{
    HSV hsv;

    RGB_ConvertToHSV(rgb, &hsv);
    *saturation = hsv.s;

    return 0;
}
#endif

/**
 *	10 bit
 */
typedef struct {
    uint16_t y;
    uint16_t u;
    uint16_t v;
} YUV;

#if DEBUG
int32_t YUV_Print(const YUV *yuv)
{
    printf("yuv(%d,%d,%d)\n", yuv->y, yuv->u, yuv->v);

    return 0;
}
#endif

static inline int32_t YUV_LimitRange(YUV *yuv);
static int32_t        SRGB_ConvertToYUV(const SRGB *srgb, YUV *yuv)
{
    int R = srgb->r;
    int G = srgb->g;
    int B = srgb->b;

    // float mult= 1024.f;
    // BT.601
    int c11 = 306;   // int(fabs(0.299*mult) + 0.5);
    int c12 = 601;   // int(fabs(0.587*mult) + 0.5);
    int c13 = 117;   // int(fabs(0.114*mult) + 0.5);
    int c21 = -173;  // int(-fabs(-0.169*mult) - 0.5);
    int c22 = -339;  // int(-fabs(0.331*mult) - 0.5);
    int c23 = 512;   // int(fabs(0.500*mult) + 0.5);
    int c31 = 512;   // int(fabs(0.500*mult) + 0.5);
    int c32 = -429;  // int(-fabs(0.419*mult) - 0.5);
    int c33 = -83;   // int(-fabs(0.081*mult) - 0.5);

    // 10 bit RGB => 10 bit YUV
    int sign_Y  = c11 * R + c12 * G + c13 * B >= 0 ? 1 : -1;
    int sign_Cb = c21 * R + c22 * G + c23 * B >= 0 ? 1 : -1;
    int sign_Cr = c31 * R + c32 * G + c33 * B >= 0 ? 1 : -1;
    int Yvalue  = ((abs(c11 * R + c12 * G + c13 * B) + (1 << 9)) >> 10) * sign_Y;
    int Cbvalue = ((abs(c21 * R + c22 * G + c23 * B) + (1 << 9)) >> 10) * sign_Cb + 512;
    int Crvalue = ((abs(c31 * R + c32 * G + c33 * B) + (1 << 9)) >> 10) * sign_Cr + 512;

    // int Yout = LIMIT_RANGE( (int)(Yvalue+0), -512, 511);
    // int Cbout = LIMIT_RANGE( (int)( Cbvalue+0 ), -512, 511);
    // int Crout = LIMIT_RANGE( (int)( Crvalue+0 ), -512, 511);
    int Yout  = Yvalue;
    int Cbout = Cbvalue;
    int Crout = Crvalue;

    yuv->y = Yout;
    yuv->u = Cbout;
    yuv->v = Crout;

    YUV_LimitRange(yuv);

    return 0;
}

static int32_t RGB_ConvertToYUV(const RGB *rgb, YUV *yuv)
{
    SRGB srgb;

    RGB_ConvertToSRGB(rgb, &srgb);
    SRGB_ConvertToYUV(&srgb, yuv);

    return 0;
}

static inline int32_t YUV_ConvertToSRGB(const YUV *yuv, SRGB *srgb)
{
#if 1
    float Yvalue  = yuv->y;
    float Cbvalue = yuv->u - 512;
    float Crvalue = yuv->v - 512;

#if 1  // copy from c-model
    float Rvalue = Yvalue - 0.00125 * Cbvalue + 1.401921 * Crvalue;
    float Gvalue = Yvalue - 0.34426 * Cbvalue - 0.71398 * Crvalue;
    float Bvalue = Yvalue + 1.771641 * Cbvalue + 0.000965 * Crvalue;
#else  // copy from https://zh.wikipedia.org/zh-tw/YUV
    float Rvalue = Yvalue - 0.00093 * Cbvalue + 1.401687 * Crvalue;
    float Gvalue = Yvalue - 0.3437 * Cbvalue - 0.71417 * Crvalue;
    float Bvalue = Yvalue + 1.77216 * Cbvalue + 0.00099 * Crvalue;
#endif

    srgb->r = LIMIT_RANGE(Rvalue + 0.5f, 0, 1023);
    srgb->g = LIMIT_RANGE(Gvalue + 0.5f, 0, 1023);
    srgb->b = LIMIT_RANGE(Bvalue + 0.5f, 0, 1023);
#endif

    return 0;
}

/**
 *	10 bit YUV to 10 bit RGB
 */
static inline int32_t YUV_ConvertToRGB(const YUV *yuv, RGB *rgb)
{
    SRGB srgb;

    YUV_ConvertToSRGB(yuv, &srgb);
    SRGB_ConvertToRGB(&srgb, rgb);

    return 0;
}

static inline int32_t YUV_GetSaturation(const YUV *yuv, float *saturation)
{
    // float du = (yuv->u - 512.0f) / 512.0f;
    // float dv = (yuv->v - 512.0f) / 512.0f;
    float du = (yuv->u - 512.0f) / 1024.0f;
    float dv = (yuv->v - 512.0f) / 1024.0f;
    // printf("du=%f, dv=%f\n", du, dv);
    float s = sqrt(du * du + dv * dv);

    *saturation = LIMIT_RANGE(s, 0, 1.0f);
    return 0;
}

static inline int32_t YUV_LimitRange(YUV *yuv)
{
    yuv->y = LIMIT_RANGE(yuv->y, 0, 1023);
    yuv->u = LIMIT_RANGE(yuv->u, 0, 1023);
    yuv->v = LIMIT_RANGE(yuv->v, 0, 1023);

    return 0;
}

static inline int32_t YUV_SetSaturation(YUV *yuv, float saturation)
{
    yuv->u = (yuv->u - 512) * saturation + 512;
    yuv->v = (yuv->v - 512) * saturation + 512;
    YUV_LimitRange(yuv);

    return 0;
}

#if (COLOR_SPACE == COLOR_SPACE_YUV)
static int32_t RGB_GetSaturationInYUV(const RGB *rgb, float *saturation)
{
    YUV yuv;

    RGB_ConvertToYUV(rgb, &yuv);
    YUV_GetSaturation(&yuv, saturation);

    return 0;
}
#endif

#if SIMULATION
int32_t UT_YUV(void)
{
    SRGB srgbs[] = {
        // {0, 0, 0},
        // {512, 0, 0},
        // {0, 512, 0},
        // {0, 0, 512},
        // {512, 512, 512},
        // {1023, 0, 0},
        // {0, 1023, 0},
        // {0, 0, 1023},
        // {1023, 1023, 1023},
        {20 / 255.0 * 1024, 56 / 255.0 * 1024, 19 / 255.0 * 1024},
    };

    for (uint16_t i = 0; i < ARRAY_LENGTH(srgbs); i++) {
        SRGB srgb = srgbs[i];
        YUV  yuv;
        SRGB srgb2;

        SRGB_ConvertToYUV(&srgb, &yuv);
        YUV_ConvertToSRGB(&yuv, &srgb2);
        SRGB_Print(&srgb);
        YUV_Print(&yuv);
        SRGB_Print(&srgb2);
        assert(abs(srgb.r - srgb2.r) < 2);
        assert(abs(srgb.g - srgb2.g) < 2);
        assert(abs(srgb.b - srgb2.b) < 2);

        float saturation;

        YUV_GetSaturation(&yuv, &saturation);
        printf("saturation=%f\n", saturation);

        // half saturation
        YUV  yuv_sat = yuv;
        SRGB srgb_sat;

        YUV_SetSaturation(&yuv_sat, 0.5);
        YUV_ConvertToSRGB(&yuv_sat, &srgb_sat);
        YUV_Print(&yuv_sat);
        SRGB_Print(&srgb_sat);
        printf("\n");
    }

    return 0;
}
#endif

typedef struct {
    float x;
    float y;
    float z;
} XYZ;

#if DEBUG
static int32_t XYZ_Print(const XYZ *xyz)
{
    printf("xyz=(%f,%f,%f)\n", xyz->x, xyz->y, xyz->z);

    return 0;
}
#endif

static inline int32_t RGB_ConvertToXYZ(const RGB *rgb, XYZ *xyz)
{
    // TODO
    //  % D65 (Imatest)
    //  whitepoint = [0.950456 1.088754];
    //  MAT = [
    //  0.4124564  0.3575761  0.1804375
    //  0.2126729  0.7151522  0.0721750
    //  0.0193339  0.1191920  0.9503041];
    float M[] = {
        0.4124564, 0.3575761, 0.1804375, 0.2126729, 0.7151522,
        0.0721750, 0.0193339, 0.1191920, 0.9503041,
    };

    float r = rgb->r / 1023.0f;
    float g = rgb->g / 1023.0f;
    float b = rgb->b / 1023.0f;

    xyz->x = (M[0] * r) + (M[1] * g) + (M[2] * b);
    xyz->y = (M[3] * r) + (M[4] * g) + (M[5] * b);
    xyz->z = (M[6] * r) + (M[7] * g) + (M[8] * b);

    return 0;
}

static inline int32_t XYZ_ConvertToRGB(const XYZ *xyz, RGB *rgb)
{
    // % XYZ to RGB
    float M[] = {
        3.240479, -1.537150, -0.498535, -0.969256, 1.875992,
        0.041556, 0.055648,  -0.204043, 1.057311,
    };

    // rgb = max(min(MAT * [X; Y; Z], 1), 0);
    float X = xyz->x;
    float Y = xyz->y;
    float Z = xyz->z;

    float r, g, b;

    r = M[0] * X + M[1] * Y + M[2] * Z;
    g = M[3] * X + M[4] * Y + M[5] * Z;
    b = M[6] * X + M[7] * Y + M[8] * Z;
    // r = M[0] * X + M[3] * Y + M[6] * Z;
    // g = M[1] * X + M[4] * Y + M[7] * Z;
    // b = M[2] * X + M[5] * Y + M[8] * Z;
    r = LIMIT_RANGE(r, 0.0f, 1.0f);
    g = LIMIT_RANGE(g, 0.0f, 1.0f);
    b = LIMIT_RANGE(b, 0.0f, 1.0f);

    // rgb=rgb';
    rgb->r = r * 1023 + 0.5;
    rgb->g = g * 1023 + 0.5;
    rgb->b = b * 1023 + 0.5;

#if DEBUG
    printf("debug s\n");
    XYZ_Print(xyz);
    RGB_Print(rgb);
    printf("debug e\n\n");
#endif
    return 0;
}

typedef struct {
    float l;
    float a;
    float b;
} LAB;

#if DEBUG
static int32_t LAB_Print(const LAB *lab)
{
    printf("Lab=(%f,%f,%f)\n", lab->l, lab->a, lab->b);

    return 0;
}
#endif

static inline int32_t XYZ_ConvertToLAB(const XYZ *xyz, LAB *lab)
{
    // % Set a threshold
    const float T            = 0.008856f;
    const float whitepoint[] = {0.950456, 1.088754};

    // % Normalize for white point
    float X = xyz->x / whitepoint[0];
    float Y = xyz->y;
    float Z = xyz->z / whitepoint[1];

    // XT = X > T;
    // YT = Y > T;
    // ZT = Z > T;

    // Y3 = Y.^(1/3);
    float Y3 = pow(Y, 1 / 3.0f);

    // fX = XT .* X.^(1/3) + (~XT) .* (7.787 .* X + 16/116);
    float fX;

    if (X > T) {
        fX = pow(X, 1 / 3.0f);
    } else {
        fX = (7.787 * X + 16 / 1160.f);
    }

    // fY = YT .* Y3 + (~YT) .* (7.787 .* Y + 16/116);
    float fY;

    if (Y > T) {
        fY = Y3;
    } else {
        fY = 7.787 * Y + 16 / 116.0f;
    }

    // fZ = ZT .* Z.^(1/3) + (~ZT) .* (7.787 .* Z + 16/116);
    float fZ;

    if (Z > T) {
        fZ = pow(Z, 1 / 3.0f);
    } else {
        fZ = 7.787 * Z + 16 / 116.0f;
    }

    float L;

    if (Y > T) {
        L = 116 * Y3 - 16.0;
    } else {
        L = 903.3 * Y;
    }
    float a = 500 * (fX - fY);
    float b = 200 * (fY - fZ);

    // Lab = [L a b];
    lab->l = L;
    lab->a = a;
    lab->b = b;

    return 0;
}

static inline int32_t LAB_ConvertToXYZ(const LAB *lab, XYZ *xyz)
{
    // % Thresholds
    const float T1 = 0.008856;
    const float T2 = 0.206893;

    // % Compute Y
    // fY = ((L + 16) / 116) .^ 3;
    float fY = pow((lab->l + 16) / 116.0f, 3);
    // YT = fY > T1;
    bool YT = fY > T1;
    // fY = (~YT) .* (L / 903.3) + YT .* fY;
    if (YT) {
        fY = fY;
    } else {
        fY = lab->l / 903.3f;
    }
    // Y = fY;
    float Y = fY;

    // % Alter fY slightly for further calculations
    // fY = YT .* (fY .^ (1/3)) + (~YT) .* (7.787 .* fY + 16/116);
    if (YT) {
        fY = pow(fY, 1 / 3.0f);

    } else {
        fY = 7.787f * fY + 16 / 116.0f;
    }

    // % Compute X
    float fX = lab->a / 500 + fY;
    // XT = fX > T2;
    bool XT = fX > T2;
    // X = (XT .* (fX .^ 3) + (~XT) .* ((fX - 16/116) / 7.787));
    float X;

    if (XT) {
        X = pow(fX, 3);
    } else {
        X = (fX - 16.0f / 116.0f) / 7.787f;
    }

    // % Compute Z
    float fZ = fY - lab->b / 200.0f;
    // ZT = fZ > T2;
    bool ZT = fZ > T2;
    // Z = (ZT .* (fZ .^ 3) + (~ZT) .* ((fZ - 16/116) / 7.787));
    float Z;

    if (ZT) {
        Z = pow(fZ, 3);
    } else {
        Z = (fZ - 16.0f / 116.0f) / 7.787f;
    }

    // % Normalize for D65 white point
    X = X * 0.950456f;
    Z = Z * 1.088754f;

    xyz->x = X;
    xyz->y = Y;
    xyz->z = Z;

    return 0;
}

static inline int32_t LAB_GetSaturation(const LAB *lab, float *saturation)
{
    *saturation = sqrt(lab->a * lab->a + lab->b * lab->b);

    return 0;
}

static inline int32_t LAB_SetSaturation(LAB *lab, float saturation)
{
    lab->a *= saturation;
    lab->b *= saturation;

    return 0;
}

#if SIMULATION
static int32_t RGB_GetSaturationInLAB(const RGB *rgb, float *saturation)
{
    uint16_t r = rgb->r;
    uint16_t g = rgb->g;
    uint16_t b = rgb->b;
    XYZ      xyz;
    LAB      lab;
    float    sat;

    RGB_ConvertToXYZ(rgb, &xyz);
    XYZ_ConvertToLAB(&xyz, &lab);
    LAB_GetSaturation(&lab, &sat);
    sat /= 100.f;
    sat         = LIMIT_RANGE(sat, 0.0f, 1.0f);
    *saturation = sat;

#if 0
	// v lut
	float in[] = {0, 5, 10, 15, 20, 30};
	float out[] = {0.0, 0.0, 0.5, 1.0, 1.0, 1.0};
	uint16_t length = 6;
	LUT_FLOAT lut = {
		.in = in,
		.out = out,
		.length = length,
	};
	float sat2;

	LUT_FLOAT_GainMap(&lut, lab.l, sat, &sat2);
	*saturation = sat2;
#endif

    UNUSED(r);
    UNUSED(g);
    UNUSED(b);

    return 0;
}
#endif

#if UNIT_TEST
int32_t UT_LAB(void)
{
    /**
     *	@see octave's rgb2xyz() and xyz2lab()
     */
    if (1) {
        printf("# srgb => rgb => xyz => lab => xyz => rgb => srgb\n");

        // init
        SRGB srgb = {512, 512, 512};
        RGB  rgb;

        SRGB_ConvertToRGB(&srgb, &rgb);
        XYZ xyz;

        RGB_ConvertToXYZ(&rgb, &xyz);

        // xyz => lab
        LAB lab;

        XYZ_ConvertToLAB(&xyz, &lab);

        // lab => xyz
        XYZ xyz2;

        LAB_ConvertToXYZ(&lab, &xyz2);

        // xyz => rgb
        RGB rgb2;

        XYZ_ConvertToRGB(&xyz2, &rgb2);

        // rgb => xyz
        SRGB srgb2;

        RGB_ConvertToSRGB(&rgb2, &srgb2);
        // print
        SRGB_Print(&srgb);
        RGB_Print(&rgb);
        XYZ_Print(&xyz);
        LAB_Print(&lab);
        XYZ_Print(&xyz2);
        RGB_Print(&rgb2);
        SRGB_Print(&srgb2);
        assert(abs(srgb.r / srgb2.r - 1) < 0.01);
        assert(abs(srgb.g / srgb2.g - 1) < 0.01);
        assert(abs(srgb.b / srgb2.b - 1) < 0.01);
        printf("\n");
    }
}
#endif

#if UNIT_TEST
int32_t UT_RGB(void)
{
    uint8_t subid = 0;

    // SRGB -> HSV -> SRGB
    {
        debug("#%d %s start\n", subid, __func__);
        RGB rgb[] = {
            {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
        };
        for (uint8_t i = 0; i < ARRAY_LENGTH(rgb); i++) {
            HSV hsv;

            RGB_ConvertToHSV(&rgb[i], &hsv);
            RGB rgb2;

            HSV_ConvertToRGB(&hsv, &rgb2);
            debug("rgb=(\t%d\t%d\t%d\t)=>hsv(\t%f\t%f\t%f\t)=>rgb(\t%d\t%d\t%d\t)\n", rgb[i].r,
                  rgb[i].g, rgb[i].b, hsv.h, hsv.s, hsv.v, rgb2.r, rgb2.g, rgb2.b);
            assert(rgb[i].r == rgb2.r);
            assert(rgb[i].g == rgb2.g);
            assert(rgb[i].b == rgb2.b);
        }
        debug("#%d %s end\n", subid++, __func__);
    }

    // rgb -> SRGB -> rgb
    {
        debug("#%d %s start\n", subid, __func__);
        RGB rgb[] = {
            {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
        };

        for (uint8_t i = 0; i < ARRAY_LENGTH(rgb); i++) {
            SRGB srgb;

            RGB_ConvertToSRGB(&rgb[i], &srgb);
            RGB rgb2;

            SRGB_ConvertToRGB(&srgb, &rgb2);
            assert(rgb[i].r == rgb2.r);
            assert(rgb[i].g == rgb2.g);
            assert(rgb[i].b == rgb2.b);
#if 0
			debug("rgb(\t%d\t%d\t%d\t)=>rgb2(\t%d\t%d\t%d\t)\n",
				rgb[i].r, rgb[i].g, rgb[i].b, rgb2.r, rgb2.g, rgb2.b);
#endif
        }
        debug("#%d %s end\n", subid++, __func__);
    }
}
#endif

//-----------------------------------------------------------------------------

static inline int32_t CLUT_ADDRESS_Write(CLUT_ADDRESS *address, uint16_t ri, uint16_t gi,
                                         uint16_t bi)
{
    CLUT_ADDRESS a = (bi << 16) + (gi << 8) + ri;

    *address = a;

    return 0;
}

static inline int32_t CLUT_ADDRESS_Read(const CLUT_ADDRESS *address, uint16_t *ri, uint16_t *gi,
                                        uint16_t *bi)
{
    CLUT_ADDRESS a = *address;

    *ri = a & 0x1F;  // 0-17
    *gi = (a >> 8) & 0x1F;
    *bi = (a >> 16) & 0x1F;

    return 0;
}

static inline int32_t CLUT_DATA_Write(CLUT_DATA *item, const SRGB *rgb)
{
    CLUT_DATA temp = ((rgb->r & 0x3FF) << 20) + ((rgb->g & 0x3FF) << 10) + (rgb->b & 0x3FF);

    *item = temp;

    return 0;
}

static inline int32_t CLUT_DATA_Read(const CLUT_DATA *item, SRGB *rgb)
{
    CLUT_DATA v = *item;

    rgb->b = v & 0x3FF;
    rgb->g = (v >> 10) & 0x3FF;
    rgb->r = (v >> 20) & 0x3FF;

    return 0;
}

#if UNIT_TEST
int32_t UT_CLUT_ADDRESS_DATA(void)
{
    CLUT_ADDRESS address = 0x030201;
    CLUT_DATA    data    = 4 + (5 << 10) + (6 << 20);

    uint16_t ri, gi, bi;

    CLUT_ADDRESS_Read(&address, &ri, &gi, &bi);
    RGB rgb;

    CLUT_DATA_Read(&data, &rgb);

    assert(ri == 1);
    assert(gi == 2);
    assert(bi == 3);
    assert(rgb.r == 4);
    assert(rgb.g == 5);
    assert(rgb.b == 6);

    CLUT_ADDRESS address2;

    CLUT_ADDRESS_Write(&address2, ri, gi, bi);
    CLUT_DATA data2;

    CLUT_DATA_Write(&data2, &rgb);

    assert(address == address2);
    assert(data == data2);

    return 0;
}
#endif

//-----------------------------------------------------------------------------

CVI_S32 CLUT_TABLE_Init(CLUT_TABLE *table, const CVI_U16 *r, const CVI_U16 *g, const CVI_U16 *b)
{
    CVI_U32 idx = 0;

    for (uint16_t bi = 0; bi < 17; bi++) {
        for (uint16_t gi = 0; gi < 17; gi++) {
            for (uint16_t ri = 0; ri < 17; ri++, idx++) {
                SRGB srgb;
#if SIMULATION
                srgb.r = (ri == 16) ? 1023 : (ri << 6);
                srgb.g = (gi == 16) ? 1023 : (gi << 6);
                srgb.b = (bi == 16) ? 1023 : (bi << 6);
#else
                srgb.r = r[idx];
                srgb.g = g[idx];
                srgb.b = b[idx];
#endif
                CLUT_DATA_Write(&table->clut[bi][gi][ri], &srgb);
            }
        }
    }

    UNUSED(r);
    UNUSED(g);
    UNUSED(b);

    return 0;
}

#if SIMULATION
int32_t CLUT_TABLE_Print(const CLUT_TABLE *table)
{
    for (uint8_t bi = 0; bi < 17; bi++) {
        for (uint8_t gi = 0; gi < 17; gi++) {
            for (uint8_t ri = 0; ri < 17; ri++) {
                SRGB srgb;

                CLUT_DATA_Read(&table->clut[bi][gi][ri], &srgb);
                debug("[\t%d\t%d\t%d\t]=(\t%d\t%d\t%d\t)\n", ri, gi, bi, srgb.r, srgb.g, srgb.b);
            }
        }
    }

    return 0;
}

int32_t CLUT_TABLE_ApplyUpdateList(CLUT_TABLE *table, const CLUT_UPDATE_LIST *list)
{
    for (uint16_t i = 0; i < list->length; i++) {
        CLUT_ADDRESS address = list->items[i].addr;
        CLUT_DATA    data    = list->items[i].data;

        uint16_t ri, gi, bi;

        CLUT_ADDRESS_Read(&address, &ri, &gi, &bi);

        // debug
        SRGB srgb;

        CLUT_DATA_Read(&table->clut[bi][gi][ri], &srgb);
        debug("in  rgb(%d,%d,%d)\n", srgb.r, srgb.g, srgb.b);

        //
        table->clut[bi][gi][ri] = data;

        // debug
        CLUT_DATA_Read(&table->clut[bi][gi][ri], &srgb);
        debug("out rgb(%d,%d,%d)\n", srgb.r, srgb.g, srgb.b);
    }

    return 0;
}

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;
} Point;

/**
 *  modified from c-model
 */
int32_t CLUT_TABLE_Mapping(const CLUT_TABLE *table, SRGB *srgb)
{
    uint16_t R = srgb->r;
    uint16_t G = srgb->g;
    uint16_t B = srgb->b;

#if DEBUG
    debug("in   ");
    SRGB_Print(srgb);
#endif

    Point points[8] = {};

    // Coordinates
    points[0].x = (R >> 6) & 0x0f;
    points[0].y = (G >> 6) & 0x0f;
    points[0].z = (B >> 6) & 0x0f;

    uint16_t wr = R & 0x3f;
    uint16_t wg = G & 0x3f;
    uint16_t wb = B & 0x3f;

    points[1].x = points[0].x + 1;
    points[1].y = points[0].y;
    points[1].z = points[0].z;

    points[2].x = points[0].x;
    points[2].y = points[0].y + 1;
    points[2].z = points[0].z;

    points[3].x = points[0].x + 1;
    points[3].y = points[0].y + 1;
    points[3].z = points[0].z;

    points[4].x = points[0].x;
    points[4].y = points[0].y;
    points[4].z = points[0].z + 1;

    points[5].x = points[0].x + 1;
    points[5].y = points[0].y;
    points[5].z = points[0].z + 1;

    points[6].x = points[0].x;
    points[6].y = points[0].y + 1;
    points[6].z = points[0].z + 1;

    points[7].x = points[0].x + 1;
    points[7].y = points[0].y + 1;
    points[7].z = points[0].z + 1;

    // Colors
    SRGB colors[8];

    for (int n = 0; n < 8; n++) {
        CLUT_DATA_Read(&table->clut[points[n].z][points[n].y][points[n].x], &colors[n]);
    }

    SRGB c00, c01, c10, c11;
    SRGB c0, c1;

    /* R */
    c00.r = (colors[1].r * wr + colors[0].r * abs(64 - wr) + 32) / 64;
    c10.r = (colors[3].r * wr + colors[2].r * abs(64 - wr) + 32) / 64;
    c0.r  = (c10.r * wg + c00.r * (64 - wg) + 32) / 64;

    c01.r = (colors[5].r * wr + colors[4].r * abs(64 - wr) + 32) / 64;
    c11.r = (colors[7].r * wr + colors[6].r * abs(64 - wr) + 32) / 64;
    c1.r  = (c11.r * wg + c01.r * (64 - wg) + 32) / 64;

    uint16_t R_out = (c1.r * wb + c0.r * (64 - wb) + 32) / 64;  // Output R

    /* G */
    c00.g = (colors[1].g * wr + colors[0].g * abs(64 - wr) + 32) / 64;
    c10.g = (colors[3].g * wr + colors[2].g * abs(64 - wr) + 32) / 64;
    c0.g  = (c10.g * wg + c00.g * (64 - wg) + 32) / 64;

    c01.g = (colors[5].g * wr + colors[4].g * abs(64 - wr) + 32) / 64;
    c11.g = (colors[7].g * wr + colors[6].g * abs(64 - wr) + 32) / 64;
    c1.g  = (c11.g * wg + c01.g * (64 - wg) + 32) / 64;

    uint16_t G_out = (c1.g * wb + c0.g * (64 - wb) + 32) / 64;  // Output G

    /* B */
    c00.b = (colors[1].b * wr + colors[0].b * abs(64 - wr) + 32) / 64;
    c10.b = (colors[3].b * wr + colors[2].b * abs(64 - wr) + 32) / 64;
    c0.b  = (c10.b * wg + c00.b * (64 - wg) + 32) / 64;

    c01.b = (colors[5].b * wr + colors[4].b * abs(64 - wr) + 32) / 64;
    c11.b = (colors[7].b * wr + colors[6].b * abs(64 - wr) + 32) / 64;
    c1.b  = (c11.b * wg + c01.b * (64 - wg) + 32) / 64;

    uint16_t B_out = (c1.b * wb + c0.b * (64 - wb) + 32) / 64;  // Output B

    // Output
    // dst.at<Vec3w>(y, x)[0] = R_out;
    // dst.at<Vec3w>(y, x)[1] = G_out;
    // dst.at<Vec3w>(y, x)[2] = B_out;
    srgb->r = R_out;
    srgb->g = G_out;
    srgb->b = B_out;

#if DEBUG
    debug("Out   ");
    SRGB_Print(srgb);
// exit(0);
#endif
}
#endif

#if UNIT_TEST
int32_t UT_CLUT_TABLE(void)
{
    debug("# %s start\n", __func__);
    CLUT_TABLE table;

    CLUT_TABLE_Init(&table, NULL, NULL, NULL);
    CLUT_TABLE_Print(&table);
    debug("# %s end\n", __func__);

    return 0;
}
#endif

//-----------------------------------------------------------------------------
typedef int32_t (*GetSaturationCb)(const RGB *data, float *saturation);
static int32_t CLUT_UPDATE_LIST_CreateBySaturation(CLUT_UPDATE_LIST *list, const CLUT_TABLE *table,
                                                   float low, float high, GetSaturationCb cb)
{
    list->length = 0;

    for (uint8_t bi = 0; bi < 17; bi++) {
        for (uint8_t gi = 0; gi < 17; gi++) {
            for (uint8_t ri = 0; ri < 17; ri++) {
                RGB  rgb;
                SRGB srgb;

#if 0
				CLUT_DATA_Read(&table->clut[bi][gi][ri], &rgb);
#else
                CLUT_DATA_Read(&table->clut[bi][gi][ri], &srgb);
                SRGB_ConvertToRGB(&srgb, &rgb);
#endif
                float saturation;

                // RGB_GetSaturation(&rgb, &saturation);
                cb(&rgb, &saturation);
                // debug("rgb(\t%d\t%d\t%d\t), saturation=\t%f\n", rgb.r, rgb.g, rgb.b, saturation);

                if ((low <= saturation) && (saturation <= high)) {
                    if (list->length == MAX_UPDATE_ITEM) {
                        printf("items over %d", MAX_UPDATE_ITEM);
                        printf(" try to reduce the saturation range");
                        printf(", e.g increase low or reduce high\n");
                        return CVI_FAILURE;
                    }

                    uint32_t l = list->length;

                    CLUT_ADDRESS_Write(&list->items[l].addr, ri, gi, bi);
                    list->items[l].data = table->clut[bi][gi][ri];
                    list->length++;
                }
            }
        }
    }

#if DEBUG
    debug("list items = %d\n", list->length);
// exit(0);
#endif

    return 0;
}

#if SIMULATION
static int32_t CLUT_UPDATE_LIST_Print(const CLUT_UPDATE_LIST *list)
{
    printf("# CLUT_UPDATE_LIST_Print\n");
    for (uint16_t i = 0; i < list->length; i++) {
        uint16_t     ri, gi, bi;
        CLUT_ADDRESS addr = list->items[i].addr;

        CLUT_ADDRESS_Read(&addr, &ri, &gi, &bi);

        SRGB      srgb;
        CLUT_DATA data = list->items[i].data;

        CLUT_DATA_Read(&data, &srgb);

        printf("[\t%d\t%d\t%d\t]=(\t%d\t%d\t%d\t)\n", ri, gi, bi, srgb.r, srgb.g, srgb.b);
    }
    printf("length=%d\n", list->length);

    return 0;
}
#endif

//-----------------------------------------------------------------------------
static int32_t CLUT_UPDATE_LIST_UpdateSaturation(CLUT_UPDATE_LIST          *listIn,
                                                 const CLUT_SATURATION_LUT *satLut,
                                                 CLUT_UPDATE_LIST          *listOut)
{
    listOut->length = listIn->length;
    for (uint16_t i = 0; i < listIn->length; i++) {
        SRGB srgb;
        RGB  rgb;

        CLUT_DATA_Read(&listIn->items[i].data, &srgb);
        SRGB_ConvertToRGB(&srgb, &rgb);

#if COLOR_SPACE == COLOR_SPACE_HSV
        HSV hsv;

        RGB_ConvertToHSV(&rgb, &hsv);
        debug("in  srgb(%d,%d,%d), rgb(%d,%d,%d), hsv(%f,%f,%f)\n", srgb.r, srgb.g, srgb.b, rgb.r,
              rgb.g, rgb.b, hsv.h, hsv.s, hsv.v);
        LUT_FLOAT_Mapping(satLut, hsv.s, &hsv.s);
        HSV_ConvertToRGB(&hsv, &rgb);
        RGB_ConvertToSRGB(&rgb, &srgb);
#if DEBUG_CONTOUR
        // srgb.r = 60 * 1023 / 255 * ((uint16_t)(hsv.s * 10));
        if (hsv.s < 0.1) {
            srgb.r = 1023 * 0.2;
            srgb.g = 0;
            srgb.b = 0;
        } else if (hsv.s < 0.2) {
            srgb.r = 1023 * 0.4;
            srgb.g = 0;
            srgb.b = 0;
        } else if (hsv.s < 0.3) {
            srgb.r = 1023 * 0.6;
            srgb.g = 0;
            srgb.b = 0;
        } else if (hsv.s < 0.4) {
            srgb.r = 1023 * 0.8;
            srgb.g = 0;
            srgb.b = 0;
        }
#endif
        debug("out srgb(%d,%d,%d), rgb(%d,%d,%d), hsv(%f,%f,%f)\n", srgb.r, srgb.g, srgb.b, rgb.r,
              rgb.g, rgb.b, hsv.h, hsv.s, hsv.v);
#elif COLOR_SPACE == COLOR_SPACE_YUV
        YUV yuv;

        SRGB_ConvertToYUV(&srgb, &yuv);
        debug("in  srgb(%d,%d,%d), yuv(%u,%u,%u)\n", srgb.r, srgb.g, srgb.b, yuv.y, yuv.u, yuv.v);
        float s0, s1;

        YUV_GetSaturation(&yuv, &s0);
        LUT_FLOAT_Mapping(satLut, s0, &s1);
        YUV yuv2;

        yuv2 = yuv;
        if (s0 != 0) {
            YUV_SetSaturation(&yuv2, s1 / s0);
        }
        SRGB srgb2;

        YUV_ConvertToSRGB(&yuv2, &srgb2);

#if 0
		if (yuv2.u < 100 && yuv2.v < 100) {
			printf("s0=%f s1=%f\n", s0, s1);
			printf("in  srgb(%d,%d,%d), rgb(%d,%d,%d), yuv(%u,%u,%u)\n"
				, srgb.r, srgb.g, srgb.b
				, rgb.r, rgb.g, rgb.b
				, yuv.y, yuv.u, yuv.v);
			printf("out srgb(%d,%d,%d), rgb(%d,%d,%d), yuv(%u,%u,%u)\n"
				, srgb2.r, srgb2.g, srgb2.b
				, rgb2.r, rgb2.g, rgb2.b
				, yuv2.y, yuv2.u, yuv2.v);
			printf("\n");
		}
#endif

        debug("out srgb(%d,%d,%d), rgb(%d,%d,%d), yuv(%u,%u,%u)\n", srgb2.r, srgb2.g, srgb2.b,
              rgb.r, rgb.g, rgb.b, yuv2.y, yuv2.u, yuv2.v);

        srgb = srgb2;
#elif COLOR_SPACE == COLOR_SPACE_LAB
#error "todo"
#endif

        CLUT_DATA_Write(&listOut->items[i].data, &srgb);
        listOut->items[i].addr = listIn->items[i].addr;
    }

    return 0;
}

#if UNIT_TEST
int32_t UT_CLUT_UPDATE_LIST(void)
{
    debug("# %s start\n", __func__);
    CLUT_TABLE table;

    CLUT_TABLE_Init(&table, NULL, NULL, NULL);

    CLUT_UPDATE_LIST list;

    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, 0.2, RGB_GetSaturation);
    // list.length = 2;	//test

    CLUT_UPDATE_LIST list2 = list;
    // float out[4] = {0.0, 0.0, 0.2, 1.0};
    CLUT_SATURATION_LUT lut = {
        .length = 4,
        .in     = {0.0, 0.1, 0.2, 1.0},
        .out    = {0.0, 0.1, 0.2, 1.0},
    };

#if 1
    CLUT_UPDATE_LIST_UpdateSaturation(&list2, &lut, &list2);
    // CLUT_UPDATE_LIST_Print(&list);
    // CLUT_UPDATE_LIST_Print(&list2);
#else  // measure performance
    clock_t begin = clock();

    for (uint16_t i = 0; i < 1000; i++) {
        CLUT_UPDATE_LIST_UpdateSaturation(&list2, &lut);
    }

    clock_t end = clock();

    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;

    printf("execute time %f s\n", time_spent);
#endif

    debug("# %s end\n", __func__);
}
#endif

//-----------------------------------------------------------------------------

#if SIMULATION
#if PNG
#include <png.h>
int x, y;

int      width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop   info_ptr;
int         number_of_passes;
png_bytep  *row_pointers;

void read_png_file(char *file_name)
{
    char header[8];  // 8 is the maximum size that can be checked

    /* open file and test for it being a png */
    FILE *fp = fopen(file_name, "rb");

    if (!fp) printf("[read_png_file] File %s could not be opened for reading", file_name);
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8))
        printf("[read_png_file] File %s is not recognized as a PNG file", file_name);

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) printf("[read_png_file] png_create_read_struct failed");

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) printf("[read_png_file] png_create_info_struct failed");

    if (setjmp(png_jmpbuf(png_ptr))) printf("[read_png_file] Error during init_io");

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    width      = png_get_image_width(png_ptr, info_ptr);
    height     = png_get_image_height(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth  = png_get_bit_depth(png_ptr, info_ptr);

    number_of_passes = png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    /* read file */
    if (setjmp(png_jmpbuf(png_ptr))) printf("[read_png_file] Error during read_image");

    row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
    for (y = 0; y < height; y++)
        row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png_ptr, info_ptr));

    png_read_image(png_ptr, row_pointers);

    fclose(fp);
}

void write_png_file(char *file_name)
{
    /* create file */
    FILE *fp = fopen(file_name, "wb");

    /* initialize stuff */
    png_ptr  = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);
    setjmp(png_jmpbuf(png_ptr));
    png_init_io(png_ptr, fp);

    /* write header */
    setjmp(png_jmpbuf(png_ptr));
    png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    /* write bytes */
    setjmp(png_jmpbuf(png_ptr));
    png_write_image(png_ptr, row_pointers);

    /* end write */
    setjmp(png_jmpbuf(png_ptr));
    png_write_end(png_ptr, NULL);

    /* cleanup heap allocation */
    for (y = 0; y < height; y++) free(row_pointers[y]);
    free(row_pointers);

    fclose(fp);
}

char g_suffix[64] = "";
void process_file(const CLUT_SATURATION_LUT *lut, const float filter)
{
    CLUT_TABLE table;

    CLUT_TABLE_Init(&table, NULL, NULL, NULL);

    CLUT_UPDATE_LIST list;
#if COLOR_SPACE == COLOR_SPACE_HSV
    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, filter, RGB_GetSaturation);
#elif COLOR_SPACE == COLOR_SPACE_YUV
    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, filter, RGB_GetSaturationInYUV);
#elif COLOR_SPACE == COLOR_SPACE_HSV
    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, filter, RGB_GetSaturationInLAB);
#endif

#if 0
	printf("list items = %d\n", list.length);
	exit(0);
#endif

    CLUT_UPDATE_LIST list2 = list;

    // key
    CLUT_UPDATE_LIST_UpdateSaturation(&list2, lut, &list2);

    CLUT_TABLE_ApplyUpdateList(&table, &list2);
// CLUT_TABLE_ApplyUpdateList(&table, &list);
#if DEBUG_PLATFORM
    CLUT_UPDATE_LIST_Print(&list);
    CLUT_UPDATE_LIST_Print(&list2);
#endif

    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB) {
        printf("[process_file] input file is PNG_COLOR_TYPE_RGBA but must be PNG_COLOR_TYPE_RGB\n");
#if DEBUG
        exit(0);
#endif
        return;
    }

    for (y = 0; y < height; y++) {
        png_byte *row = row_pointers[y];

        for (x = 0; x < width; x++) {
            png_byte *ptr = &(row[x * 3]);

#if 1
            SRGB srgb = {ptr[0] << 2, ptr[1] << 2, ptr[2] << 2};
            // SRGB srgb = { 255 << 2, 255 << 2, 255 << 2 };
            // SRGB srgb = { 1023, 1023, 1023 };
            CLUT_TABLE_Mapping(&table, &srgb);
            ptr[0] = (srgb.r + 0) >> 2;
            ptr[1] = (srgb.g + 0) >> 2;
            ptr[2] = (srgb.b + 0) >> 2;
#else
            // printf("Pixel at position [ %d - %d ] has RGBA values: %d - %d - %d - %d\n", x, y,
            // ptr[0],
            //        ptr[1], ptr[2], ptr[3]);

            /* set red value to 0 and green value to the blue one */
            ptr[0] = 0;
            ptr[1] = ptr[2];
#endif
        }
    }
}

#if 0
void process_yuv_contour(void)
{
	CLUT_TABLE table;

	CLUT_TABLE_Init(&table);

	CLUT_UPDATE_LIST list;

	CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, 1.0, RGB_GetSaturationInYUV);

	CLUT_UPDATE_LIST list2 = list;
	// set as contour
	for (uint16_t i = 0; i < list2.length; i++) {
		CLUT_DATA data = list2.items[i].data;
		SRGB srgb;
		RGB rgb;
		XYZ xyz;
		LAB lab;
		YUV yuv;
		float saturation;
		YUV yuv2;
		RGB rgb2;
		SRGB srgb2 = {};
		CLUT_DATA data2;

		CLUT_DATA_Read(&data, &srgb);
		SRGB_ConvertToRGB(&srgb, &rgb);
		RGB_ConvertToYUV(&rgb, &yuv);
		YUV_GetSaturation(&yuv, &saturation);
		srgb2.r = srgb2.g = srgb2.b = saturation * 1023 + 0.5;

		CLUT_DATA_Write(&data, &srgb2);
		list2.items[i].data = data;
	}

	CLUT_TABLE_ApplyUpdateList(&table, &list2);
#if DEBUG_PLATFORM
	CLUT_UPDATE_LIST_Print(&list);
	CLUT_UPDATE_LIST_Print(&list2);
#endif

	if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB) {
		printf("[process_file] input file is PNG_COLOR_TYPE_RGBA but must be PNG_COLOR_TYPE_RGB\n");
#if DEBUG
		exit(0);
#endif
		return;
	}

	for (y = 0; y < height; y++) {
		png_byte *row = row_pointers[y];

		for (x = 0; x < width; x++) {
			png_byte *ptr = &(row[x * 3]);

			SRGB srgb = { ptr[0] << 2, ptr[1] << 2, ptr[2] << 2 };
			// SRGB srgb = { 255 << 2, 255 << 2, 255 << 2 };
			// SRGB srgb = { 1023, 1023, 1023 };
			CLUT_TABLE_Mapping(&table, &srgb);
			ptr[0] = (srgb.r + 0) >> 2;
			ptr[1] = (srgb.g + 0) >> 2;
			ptr[2] = (srgb.b + 0) >> 2;
		}
	}
}
#endif

typedef enum {
    CLUT_COLOR_MODEL_HSV,
    CLUT_COLOR_MODEL_YUV,
    CLUT_COLOR_MODEL_LAB,
    CLUT_COLOR_MODEL_SIZE,
} CLUT_COLOR_MODEL;

#define SRGB2YUV 1
void process_contour(CLUT_COLOR_MODEL model)
{
    CLUT_TABLE table;

    CLUT_TABLE_Init(&table, NULL, NULL, NULL);

    CLUT_UPDATE_LIST list;

    GetSaturationCb cb[CLUT_COLOR_MODEL_SIZE] = {
        RGB_GetSaturationInHSV,
        RGB_GetSaturationInYUV,
        RGB_GetSaturationInLAB,
    };

    // get full list
    // don't use RGB_GetSaturationInLAB() since it's saturation is not in range[0, 1]
    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, 1.0, RGB_GetSaturationInYUV);

    CLUT_UPDATE_LIST list2 = list;
    // set as contour
    for (uint16_t i = 0; i < list2.length; i++) {
        CLUT_DATA data = list2.items[i].data;
        SRGB      srgb;
        RGB       rgb;
        XYZ       xyz;
        LAB       lab;
        YUV       yuv;
        float     saturation;
        YUV       yuv2;
        RGB       rgb2;
        SRGB      srgb2 = {};
        CLUT_DATA data2;

        CLUT_DATA_Read(&data, &srgb);
#if SRGB2YUV
        SRGB_ConvertToRGB(&srgb, &rgb);
        cb[model](&rgb, &saturation);
        srgb2.r = srgb2.g = srgb2.b = saturation * 1023 + 0.5;
#else
        SRGB_ConvertToRGB(&srgb, &rgb);
        RGB_ConvertToYUV(&rgb, &yuv);
        // YUV_GetSaturation(&yuv, &saturation);
        cb[model](&rgb, &saturation);
        srgb2.r = srgb2.g = srgb2.b = saturation * 1023 + 0.5;
#endif

        CLUT_DATA_Write(&data, &srgb2);
        list2.items[i].data = data;
    }

    CLUT_TABLE_ApplyUpdateList(&table, &list2);
#if DEBUG_PLATFORM
    CLUT_UPDATE_LIST_Print(&list);
    CLUT_UPDATE_LIST_Print(&list2);
#endif

    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB) {
        printf("[process_file] input file is PNG_COLOR_TYPE_RGBA but must be PNG_COLOR_TYPE_RGB\n");
#if DEBUG
        exit(0);
#endif
        return;
    }

    for (y = 0; y < height; y++) {
        png_byte *row = row_pointers[y];

        for (x = 0; x < width; x++) {
            png_byte *ptr = &(row[x * 3]);

            SRGB srgb = {ptr[0] << 2, ptr[1] << 2, ptr[2] << 2};
            // SRGB srgb = { 255 << 2, 255 << 2, 255 << 2 };
            // SRGB srgb = { 1023, 1023, 1023 };
            CLUT_TABLE_Mapping(&table, &srgb);
            ptr[0] = (srgb.r + 0) >> 2;
            ptr[1] = (srgb.g + 0) >> 2;
            ptr[2] = (srgb.b + 0) >> 2;
        }
    }
}
#endif
#endif

#if SIMULATION
int main_simulation(int argc, char **argv)
{
#if PNG
    // iso1000 done
    //      filter = 0.2
    //      false color = 11.5%
    //      coring = 10%
    // iso7000
    //      filter = 0.2,
    //      false color = 13%
    //      coring = 15%
    // iso12800 done
    //      false color = 14%
    //      filter = 20%

#if DEBUG_PLATFORM
    // prepare clut
    //  const float filter = 0.1;   //17    //53
    //  const float filter = 0.2;   //53    //209
    //  const float filter = 0.3;   //113   //413
    const float filter = 0.4;  // 209   //767
    // const float filter = 0.5;   //323	//241   //1323
    // const float filter = 1.0;   //4913  //4913

#if 0  // gray
	float in[6] = {
		0 / 8192.0,		//0
		409 / 8192.0,	//1
		3100 / 8192.0,	//2
		3276 / 8192.0,	//3
		filter,			//4
		1.0,			//5
	};
	float out[6] = {
		0 / 8192.0,		//0
		0 / 8192.0,		//1
		0 / 8192.0,		//2
		0 / 8192.0,		//3
		filter,			//4
		1.0,			//5
#endif

    uint16_t length = 7;

    CLUT_SATURATION_LUT lut[] = {
        {
            .length = length,
            .in =
                {
                    0.00,    // 0	fix
                    0.04,    // 1
                    0.08,    // 2
                    0.12,    // 3
                    0.16,    // 4
                    filter,  // 5	fix
                    1.0,     // 6	fix
                },
            .out =
                {
                    0.00,    // 0	fix
                    0.00,    // 1	12233 ok, close
                    0.08,    // 2
                    0.12,    // 3	no false color
                    0.20,    // 4
                    filter,  // 5	fix
                    1.0,     // 6	fix
                },
        },
    };
#else
    // prepare clut
    //  const float filter = 0.1;   //17    //53
    //  const float filter = 0.2;   //53    //209
    //  const float filter = 0.3;   //113   //413
    const float filter = 0.4;  // 209   //767
    // const float filter = 0.5;   //323	//241   //1323
    // const float filter = 1.0;   //4913  //4913

    //              0       1   2       3      4    5       6       7
    float in_5[8]  = {0.0, 0.05, 0.10, 0.15, 0.20, 0.25, filter, 1.0};
    float out_5[8] = {0.0, 0.00, 0.075, 0.15, 0.20, 0.25, filter, 1.0};
    //              0       1   2       3      4    5       6       7
    float in_10[8]  = {0.0, 0.05, 0.10, 0.15, 0.20, 0.25, filter, 1.0};
    float out_10[8] = {0.0, 0.00, 0.00, 0.10, 0.20, 0.25, filter, 1.0};
    //              0       1   2       3      4    5       6       7
    float in_15[8]  = {0.0, 0.05, 0.10, 0.15, 0.20, 0.25, filter, 1.0};
    float out_15[8] = {0.0, 0.00, 0.00, 0.00, 0.125, 0.25, filter, 1.0};
    //              0       1   2       3      4    5       6       7
    float in_20[8]  = {0.0, 0.05, 0.10, 0.15, 0.20, 0.30, filter, 1.0};
    float out_20[8] = {0.0, 0.00, 0.00, 0.00, 0.00, 0.30, filter, 1.0};
    //              0       1   2       3      4    5       6       7
    float in_25[8]  = {0.0, 0.05, 0.10, 0.15, 0.25, 0.40, filter, 1.0};
    float out_25[8] = {0.0, 0.00, 0.00, 0.00, 0.00, 0.40, filter, 1.0};
    //              0       1   2       3      4    5       6       7
    float in_30[8]  = {0.0, 0.05, 0.10, 0.15, 0.30, 0.50, filter, 1.0};
    float out_30[8] = {0.0, 0.00, 0.00, 0.00, 0.00, 0.50, filter, 1.0};
    //
    uint16_t length = 8;

    CLUT_SATURATION_LUT lut[] = {
        {.length = length, .in = in_5, .out = out_5},    // 5%
        {.length = length, .in = in_10, .out = out_10},  // 10%
        {.length = length, .in = in_15, .out = out_15},  // 15%
        {.length = length, .in = in_20, .out = out_20},  // 20%
        {.length = length, .in = in_25, .out = out_25},  // 25%
        {.length = length, .in = in_30, .out = out_30},  // 30%
        {.length = length, .in = in_30, .out = out_30},  // 30%
    };

#endif

    char inpng[][64] = {//  "/media/sf_ISP3/color-wheel-clipart3.png",
                        //  "ISO1000.png",
                        //  "ISO3200.png",
                        //  "ISO7000.png",
                        //  "ISO12800.png",
                        //  "/media/sf_ISP3/normal.png"
                        "/media/sf_ISP3/orig.png"};
    for (uint32_t config = 0; config < ARRAY_LENGTH(lut); config++) {
        // debug input
        LUT_FLOAT_Print(&lut[config]);

        sprintf(g_suffix, "_config%d", config);

        for (uint32_t i = 0; i < ARRAY_LENGTH(inpng); i++) {
            read_png_file(inpng[i]);
            process_file(&lut[config], filter);
            char outpng[128] = "";
            sprintf(outpng, "%s%s.png", inpng[i], g_suffix);
            printf("infile=%s, outfile=%s\n", inpng[i], outpng);
            write_png_file(outpng);
        }
    }
#endif

    UNUSED(argc);
    UNUSED(argv);

    return 0;
}
#endif

#if SIMULATION
int main_saturation_contour(int argc, char **argv)
{
    char inpng[][64] = {
        //  "/media/sf_ISP3/red.png",
        //  "/media/sf_ISP3/color-wheel-clipart3.png",
        "/media/sf_ISP3/ISO6400_Normal_sat50.png",
        //  "/media/sf_ISP3/ISO12800_Normal_sat50.png"
    };
    uint16_t length                            = ARRAY_LENGTH(inpng);
    char     suffix[CLUT_COLOR_MODEL_SIZE][64] = {
        "HSV",
        "YUV",
        "LAB",
    };

    if (argc < 1) {
        length = 1;
        strcpy(inpng[0], argv[1]);
    }

    for (uint16_t mi = 0; mi < CLUT_COLOR_MODEL_SIZE; mi++) {
        for (uint16_t i = 0; i < length; i++) {
            read_png_file(inpng[i]);
            // process_yuv_contour();
            process_contour(mi);
            char outpng[128] = "";

            sprintf(outpng, "%s_contour_%s.png", inpng[i], suffix[mi]);
            printf("infile=%s, outfile=%s\n", inpng[i], outpng);
            write_png_file(outpng);
        }
    }

    UNUSED(argc);
    UNUSED(argv);

    return 0;
}
#endif

#if SIMULATION
void process_yuv(uint16_t yuv_layer)
{
    CLUT_TABLE table;

    CLUT_TABLE_Init(&table, NULL, NULL, NULL);

    CLUT_UPDATE_LIST list;

    // get full list
    // don't use RGB_GetSaturationInLAB() since it's saturation is not in range[0, 1]
    CLUT_UPDATE_LIST_CreateBySaturation(&list, &table, 0, 1.0, RGB_GetSaturationInYUV);

    CLUT_UPDATE_LIST list2 = list;
    // set as contour
    for (uint16_t i = 0; i < list2.length; i++) {
        CLUT_DATA data = list2.items[i].data;
        SRGB      srgb;
        RGB       rgb;
        XYZ       xyz;
        LAB       lab;
        YUV       yuv;
        float     saturation;
        YUV       yuv2;
        RGB       rgb2;
        SRGB      srgb2 = {};
        CLUT_DATA data2;

        CLUT_DATA_Read(&data, &srgb);
        SRGB_ConvertToRGB(&srgb, &rgb);
        RGB_ConvertToYUV(&rgb, &yuv);
        uint16_t value[] = {yuv.y, yuv.u, yuv.v};

        srgb2.r = srgb2.g = srgb2.b = value[yuv_layer];

        CLUT_DATA_Write(&data, &srgb2);
        list2.items[i].data = data;
    }

    CLUT_TABLE_ApplyUpdateList(&table, &list2);
#if DEBUG_PLATFORM
    CLUT_UPDATE_LIST_Print(&list);
    CLUT_UPDATE_LIST_Print(&list2);
#endif

    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB) {
        printf("[process_file] input file is PNG_COLOR_TYPE_RGBA but must be PNG_COLOR_TYPE_RGB\n");
#if DEBUG
        exit(0);
#endif
        return;
    }

    for (y = 0; y < height; y++) {
        png_byte *row = row_pointers[y];

        for (x = 0; x < width; x++) {
            png_byte *ptr = &(row[x * 3]);

            SRGB srgb = {ptr[0] << 2, ptr[1] << 2, ptr[2] << 2};
            // SRGB srgb = { 255 << 2, 255 << 2, 255 << 2 };
            // SRGB srgb = { 1023, 1023, 1023 };
            CLUT_TABLE_Mapping(&table, &srgb);
            ptr[0] = (srgb.r + 0) >> 2;
            ptr[1] = (srgb.g + 0) >> 2;
            ptr[2] = (srgb.b + 0) >> 2;
        }
    }
}
#endif

#if SIMULATION
int main_yuv_contour(int argc, char **argv)
{
    char inpng[][64] = {
        //  "/media/sf_ISP3/red.png",
        //  "/media/sf_ISP3/color-wheel-clipart3.png",
        //  "/media/sf_ISP3/ISO12800_Normal_sat50.png"
        //  "/media/sf_ISP3/ISO6400_Normal_sat50.png",
        "/media/sf_ISP3/ISO1000.png",
    };
    uint16_t length        = ARRAY_LENGTH(inpng);
    char     suffix[3][64] = {
        "Y",
        "U",
        "V",
    };

    if (argc < 1) {
        length = 1;
        strcpy(inpng[0], argv[1]);
    }

    for (uint16_t mi = 0; mi < 3; mi++) {
        for (uint16_t i = 0; i < length; i++) {
            read_png_file(inpng[i]);
            // process_yuv_contour();
            process_yuv(mi);
            char outpng[128] = "";

            sprintf(outpng, "%s_%s.png", inpng[i], suffix[mi]);
            printf("infile=%s, outfile=%s\n", inpng[i], outpng);
            write_png_file(outpng);
        }
    }

    UNUSED(argc);
    UNUSED(argv);

    return 0;
}
#endif

#if SIMULATION
int main_ut(int argc, char **argv)
{
    clock_t begin = clock();

    // MAX3(2, 1, 4);
    // UT_LUT();
    // UT_RGB();
    UT_YUV();
    // UT_LAB();
    // UT_CLUT_ADDRESS_DATA();
    // UT_CLUT_TABLE();
    // UT_CLUT_UPDATE_LIST();
    return 0;
}
#endif

#if 0
int main_uv_to_saturation(int argc, char **argv)
{
	char inpng[][64] = {
		"/media/sf_ISP3/ISO1000-YCbCr_ITU_R709_CB.png",
		"/media/sf_ISP3/ISO1000-YCbCr_ITU_R709_CR.png",
	};

	read_png_file(inpng[0]);
	png_byte *u = calloc(width * height * sizeof(png_byte), 1);
	{
		uint32_t i;

		i = 0;
		for (y = 0; y < height; y++) {
			png_byte *row = row_pointers[y];

			for (x = 0; x < width; x++, i++) {
				png_byte *ptr = &(row[x * 3]);

				u[i] = ptr[1];
			}
		}
	}

	read_png_file(inpng[1]);
	png_byte *v = calloc(width * height * sizeof(png_byte), 1);
	{
		uint32_t i;

		i = 0;
		for (y = 0; y < height; y++) {
			png_byte *row = row_pointers[y];

			for (x = 0; x < width; x++, i++) {
				png_byte *ptr = &(row[x * 3]);

				u[i] = ptr[2];
			}
		}
	}

	{
		uint32_t i;

		i = 0;
		for (y = 0; y < height; y++) {
			png_byte *row = row_pointers[y];

			for (x = 0; x < width; x++, i++) {
				png_byte *ptr = &(row[x * 3]);
				float du, dv;

				du = (u[i] - 128.0) / 256.0;
				dv = (v[i] - 128.0) / 256.0;

				float saturation = sqrt(du*du + dv*dv);

				ptr[2] = ptr[1] = ptr[0] = saturation * 255;
			}
		}
	}

	char outpng[128] = "";

	sprintf(outpng, "%s_yuv_saturation.png", inpng[0]);
	printf("infile=%s, outfile=%s\n", inpng[0], outpng);
	write_png_file(outpng);

	return 0;
}
#endif

#if SIMULATION
int main(int argc, char **argv)
{
    // return main_yuv_contour(argc, argv);
    // return main_saturation_contour(argc, argv);
    // return main_ut(argc, argv);
    return main_simulation(argc, argv);
    // return  main_uv_to_saturation(argc, argv);
}
#endif

#if !SIMULATION
CVI_S32 isp_algo_clut_main(CVI_U32 ViPipe, struct clut_param_in *clut_param_in,
                           struct clut_param_out *clut_param_out)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);
    struct isp_algo_clut_runtime  *runtime     = *runtime_ptr;

    ISP_ALGO_CHECK_POINTER(runtime);

    runtime->tableUpdated |= ((clut_param_in->is_table_update) ? 1 : 0);

    ISP_CLUT_SATURATION_ATTR_S *saturation_attr =
        (ISP_CLUT_SATURATION_ATTR_S *)ISP_PTR_CAST_VOID(clut_param_in->saturation_attr);

    if (!saturation_attr->Enable || clut_param_in->is_table_updating) {
        return ret;
    }

    CVI_BOOL  isTableUpdate = runtime->tableUpdated;
    CVI_BOOL *isLutUpdate   = ISP_PTR_CAST_BOOL(clut_param_in->is_lut_update);

    if ((isTableUpdate || *isLutUpdate)) runtime->isUpdated = CVI_TRUE;

    CVI_BOOL isUpdateISOTable = CVI_FALSE;

    if (isTableUpdate) {
        isp_algo_clut_update_table(ViPipe, ISP_PTR_CAST_U16(clut_param_in->ClutR),
                                   ISP_PTR_CAST_U16(clut_param_in->ClutG),
                                   ISP_PTR_CAST_U16(clut_param_in->ClutB));
        runtime->tableUpdated = 0;
        isUpdateISOTable      = CVI_TRUE;
    }

    if (*isLutUpdate) {
        isp_algo_clut_update_lut(ViPipe, saturation_attr);
        *isLutUpdate     = CVI_FALSE;
        isUpdateISOTable = CVI_TRUE;
    }

    if (runtime->isUpdated && !isTableUpdate) {
        isp_algo_clut_update_iso_table(ViPipe);
        runtime->isUpdated = CVI_FALSE;
        isUpdateISOTable   = CVI_TRUE;
    }

    if (isUpdateISOTable) {
        clut_param_out->updateList.length = 0;
    } else {
        isp_algo_clut_interpolate(ViPipe, clut_param_in, clut_param_out);
    }

#if 0
	for (CVI_U32 i = 0 ; i < (clut_param_out->updateList.length >> 1) ; i++) {
		CLUT_UPDATE_ITEM items;

		items = clut_param_out->updateList.items[i];
		clut_param_out->updateList.items[i] =
			clut_param_out->updateList.items[clut_param_out->updateList.length - i - 1];
		clut_param_out->updateList.items[clut_param_out->updateList.length - i - 1] = items;
	}

	CLUT_TABLE_ApplyUpdateList(&runtime->tableUpdate, &clut_param_out->updateList);

	CVI_U32 idx = 0;

	for (uint16_t bi = 0; bi < 17; bi++) {
		for (uint16_t gi = 0; gi < 17; gi++) {
			for (uint16_t ri = 0; ri < 17; ri++, idx++) {
				SRGB srgb;

				CLUT_DATA_Read(&runtime->tableUpdate.clut[bi][gi][ri], &srgb);

				clut_param_out->ClutR[idx] = srgb.r;
				clut_param_out->ClutG[idx] = srgb.g;
				clut_param_out->ClutB[idx] = srgb.b;
			}
		}
	}
#endif

    UNUSED(runtime);

    return ret;
}

CVI_S32 isp_algo_clut_init(CVI_U32 ViPipe)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);

    ISP_ALGO_CREATE_RUNTIME(*runtime_ptr, struct isp_algo_clut_runtime);

    return ret;
}

CVI_S32 isp_algo_clut_uninit(CVI_U32 ViPipe)
{
    // ISP_DEBUG(LOG_INFO, "%s\n", "+");
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);

    ISP_ALGO_RELEASE_MEMORY(*runtime_ptr);

    return ret;
}

static CVI_S32 isp_algo_clut_update_table(CVI_U32 ViPipe, const CVI_U16 *r, const CVI_U16 *g,
                                          const CVI_U16 *b)
{
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);
    struct isp_algo_clut_runtime  *runtime     = *runtime_ptr;

    ISP_ALGO_CHECK_POINTER(runtime);

    CLUT_TABLE_Init(&runtime->table, r, g, b);
// memcpy(&runtime->tableUpdate, &runtime->table, sizeof(CLUT_TABLE));
#if COLOR_SPACE == COLOR_SPACE_HSV
    CLUT_UPDATE_LIST_CreateBySaturation(&runtime->candidateList, &runtime->table, 0, FILTER,
                                        RGB_GetSaturationInHSV);
#elif COLOR_SPACE == COLOR_SPACE_YUV
    CLUT_UPDATE_LIST_CreateBySaturation(&runtime->candidateList, &runtime->table, 0, FILTER,
                                        RGB_GetSaturationInYUV);
#elif COLOR_SPACE == COLOR_SPACE_LAB
    CLUT_UPDATE_LIST_CreateBySaturation(&runtime->candidateList, &runtime->table, 0, FILTER,
                                        RGB_GetSaturationInLAB);
#endif

    return ret;
}

static CVI_S32 isp_algo_clut_update_lut(CVI_U32                           ViPipe,
                                        const ISP_CLUT_SATURATION_ATTR_S *saturation_attr)
{
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);
    struct isp_algo_clut_runtime  *runtime     = *runtime_ptr;

    ISP_ALGO_CHECK_POINTER(runtime);

    runtime->lutManual.length = LUT_LENGTH;
    runtime->lutManual.in[0]  = 0;
    runtime->lutManual.out[0] = 0;
    for (CVI_U32 idx = 0; idx < LUT_LENGTH - 3; idx++) {
        runtime->lutManual.in[idx + 1]  = (CVI_FLOAT)saturation_attr->stManual.SatIn[idx] / 8192;
        runtime->lutManual.out[idx + 1] = (CVI_FLOAT)saturation_attr->stManual.SatOut[idx] / 8192;
    }
    runtime->lutManual.in[LUT_LENGTH - 2] = runtime->lutManual.out[LUT_LENGTH - 2] = FILTER;
    runtime->lutManual.in[LUT_LENGTH - 1] = runtime->lutManual.out[LUT_LENGTH - 1] = 1.0;

    for (CVI_U32 iso = 0; iso < ISP_AUTO_ISO_STRENGTH_NUM; iso++) {
        runtime->lutAuto[iso].length = LUT_LENGTH;
        runtime->lutAuto[iso].in[0]  = 0;
        runtime->lutAuto[iso].out[0] = 0;
        for (CVI_U32 idx = 0; idx < LUT_LENGTH - 3; idx++) {
            runtime->lutAuto[iso].in[idx + 1] =
                (CVI_FLOAT)saturation_attr->stAuto.SatIn[idx][iso] / 8192;
            runtime->lutAuto[iso].out[idx + 1] =
                (CVI_FLOAT)saturation_attr->stAuto.SatOut[idx][iso] / 8192;
        }
        runtime->lutAuto[iso].in[LUT_LENGTH - 2] = runtime->lutAuto[iso].out[LUT_LENGTH - 2] =
            FILTER;
        runtime->lutAuto[iso].in[LUT_LENGTH - 1] = runtime->lutAuto[iso].out[LUT_LENGTH - 1] = 1.0;
    }

    return ret;
}

static CVI_S32 isp_algo_clut_update_iso_table(CVI_U32 ViPipe)
{
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);
    struct isp_algo_clut_runtime  *runtime     = *runtime_ptr;

    ISP_ALGO_CHECK_POINTER(runtime);

    CLUT_UPDATE_LIST_UpdateSaturation(&runtime->candidateList, &runtime->lutManual,
                                      &runtime->updateListManual);
    for (CVI_U32 iso = 0; iso < ISP_AUTO_ISO_STRENGTH_NUM; iso++) {
        CLUT_UPDATE_LIST_UpdateSaturation(&runtime->candidateList, &runtime->lutAuto[iso],
                                          &runtime->updateListAuto[iso]);
    }

    return ret;
}

static CVI_S32 isp_algo_clut_interpolate(CVI_U32 ViPipe, struct clut_param_in *clut_param_in,
                                         struct clut_param_out *clut_param_out)
{
    CVI_S32                        ret         = CVI_SUCCESS;
    struct isp_algo_clut_runtime **runtime_ptr = _get_algo_clut_runtime(ViPipe);
    struct isp_algo_clut_runtime  *runtime     = *runtime_ptr;

    ISP_ALGO_CHECK_POINTER(runtime);

    ISP_CLUT_SATURATION_ATTR_S *saturation_attr =
        (ISP_CLUT_SATURATION_ATTR_S *)ISP_PTR_CAST_VOID(clut_param_in->saturation_attr);

    ISP_OP_TYPE_E op  = saturation_attr->enOpType;
    CVI_U32       iso = clut_param_in->iso;

    if (op == OP_TYPE_MANUAL) {
        memcpy(&clut_param_out->updateList, &runtime->updateListManual, sizeof(CLUT_UPDATE_LIST));
    } else {
        if (iso <= ISO_LUT[0]) {
            memcpy(&clut_param_out->updateList, &runtime->updateListAuto[0],
                   sizeof(CLUT_UPDATE_LIST));
        } else if (iso >= ISO_LUT[ISP_AUTO_ISO_STRENGTH_NUM - 1]) {
            memcpy(&clut_param_out->updateList,
                   &runtime->updateListAuto[ISP_AUTO_ISO_STRENGTH_NUM - 1],
                   sizeof(CLUT_UPDATE_LIST));
        } else {
            CVI_U32   idx0 = 0, idx1 = 0;
            CVI_FLOAT ratio0 = 0.0f, ratio1 = 0.0f;

            for (CVI_U32 i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; i++) {
                if ((ISO_LUT[i] <= iso) && (iso < ISO_LUT[i + 1])) {
                    // updateList [0] = manual, [1] = iso100, ...
                    idx0   = i;
                    idx1   = i + 1;
                    ratio1 = (float)(iso - ISO_LUT[i]) / (float)(ISO_LUT[i + 1] - ISO_LUT[i]);
                    ratio0 = 1.0 - ratio1;
                    break;
                }
            }

            clut_param_out->updateList.length = runtime->updateListAuto[idx0].length;
            for (CVI_U32 idx = 0; idx < clut_param_out->updateList.length; idx++) {
                SRGB srgb0, srgb1, srgb2;

                CLUT_DATA_Read(&runtime->updateListAuto[idx0].items[idx].data, &srgb0);
                CLUT_DATA_Read(&runtime->updateListAuto[idx1].items[idx].data, &srgb1);

                srgb2.r = srgb0.r * ratio0 + srgb1.r * ratio1;
                srgb2.g = srgb0.g * ratio0 + srgb1.g * ratio1;
                srgb2.b = srgb0.b * ratio0 + srgb1.b * ratio1;

                clut_param_out->updateList.items[idx].addr =
                    runtime->updateListAuto[idx0].items[idx].addr;

                CLUT_DATA_Write(&clut_param_out->updateList.items[idx].data, &srgb2);
            }
        }
    }

    return ret;
}

static struct isp_algo_clut_runtime **_get_algo_clut_runtime(VI_PIPE ViPipe)
{
    CVI_BOOL isVipipeValid = ((ViPipe >= 0) && (ViPipe < VI_MAX_PIPE_NUM));

    if (!isVipipeValid) {
        // ISP_DEBUG(LOG_ERR, "Wrong ViPipe(%d)\n", ViPipe);
        return NULL;
    }

    return &algo_clut_runtime[ViPipe];
}
#endif
