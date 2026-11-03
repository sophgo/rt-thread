// core/filter.cpp — §3.3 滤波/形态学/阈值核（裸指针实现）
// 语义对齐参考（OpenCV 4.5.5 源）：
//   deriv.cpp:787        Laplacian ksize=1 → filter2D(3×3 浮点核 {0,1,0;1,-4,1;0,1,0})
//   copy.cpp:748         borderInterpolate BORDER_REFLECT_101: p<0 → -p-1; p≥len → 2len-2-p
//   thresh.cpp:52        BINARY 严格 >；TRUNC min(src,thresh)
//   morph.dispatch.cpp   CLOSE = dilate→erode；rect 核可分离；常量边界 dilate=0/erode=255
//   fast_math.hpp        cvRound = 四舍五入到偶
#include "hwcv.h"

#include <cmath>
#include <vector>
#include <algorithm>

namespace hwcv {

// BORDER_REFLECT_101 越界下标映射（core/copy.cpp:748：p<0 → -p；p≥len → 2len-2-p；len==1 特判 0）
static inline int refl101(int p, int len) {
  if ((unsigned)p < (unsigned)len) return p;
  if (len == 1) return 0;
  do {
    if (p < 0) p = -p;          // REFLECT_101 不复制边缘：-1 → 1
    else p = 2 * len - 2 - p;   // len → len-2
  } while ((unsigned)p >= (unsigned)len);
  return p;
}

// ---- Laplacian 8UC1→64F（ksize=1）----
// 卷积核（浮点）：{0,1,0; 1,-4,1; 0,1,0}，双精度累加。
// OpenCV filter2D 对 64F 目标无任何定点化路径，逐元素 == 可达。
void laplacian_64f(const uint8_t* src, int src_step,
                   double* dst, int dst_step, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* r0 = src + (size_t)refl101(y - 1, height) * src_step;
    const uint8_t* r1 = src + (size_t)y * src_step;
    const uint8_t* r2 = src + (size_t)refl101(y + 1, height) * src_step;
    double* d = dst + (size_t)y * (dst_step / (int)sizeof(double));
    for (int x = 0; x < width; ++x) {
      int xl = refl101(x - 1, width), xr = refl101(x + 1, width);
      d[x] = (double)r0[x] + (double)r2[x] + (double)r1[xl] + (double)r1[xr] -
             4.0 * (double)r1[x];
    }
  }
}

// ---- threshold TRUNC（64F）：min(src, thresh) ----
void threshold_trunc_f64(const double* src, int src_step,
                         double* dst, int dst_step,
                         int width, int height, double thresh) {
  for (int y = 0; y < height; ++y) {
    const double* s = src + (size_t)y * (src_step / (int)sizeof(double));
    double* d = dst + (size_t)y * (dst_step / (int)sizeof(double));
    for (int x = 0; x < width; ++x) d[x] = s[x] < thresh ? s[x] : thresh;
  }
}

// ---- threshold BINARY（8U）：严格 > ----
void threshold_binary_u8(const uint8_t* src, int src_step,
                         uint8_t* dst, int dst_step,
                         int width, int height, double thresh) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) d[x] = (double)s[x] > thresh ? 255 : 0;
  }
}

// ---- convertScaleAbs（64F→8U）----
// 逐像素 saturate_cast<uchar>(|αx+β|)。cvRound 语义=四舍五入到偶：
// r = nearbyint(v)（FE_TONEAREST），再饱和 [0,255]。
// （occlusion 调用点 α=1 β=0，|x| 落 .5 处 round-half-even 才与真侧一致）
static inline double round_half_even(double v) {
  // ::nearbyint 在 RISC-V newlib 中不在 std 命名空间
  return ::nearbyint(v);
}

void convert_scale_abs_f64(const double* src, int src_step,
                           uint8_t* dst, int dst_step,
                           int width, int height,
                           double alpha, double beta) {
  for (int y = 0; y < height; ++y) {
    const double* s = src + (size_t)y * (src_step / (int)sizeof(double));
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      double v = s[x] * alpha + beta;
      if (v < 0) v = -v;
      double r = round_half_even(v);
      d[x] = r <= 0 ? (uint8_t)0 : (r >= 255 ? (uint8_t)255 : (uint8_t)r);
    }
  }
}

// ---- morphology CLOSE + RECT 核（8U，可分离）----
// CLOSE = dilate → erode。rect 核可分离：
//   dilate: 行方向 max 窗 kw（出界=0，常量边界幺元）→ 列方向 max 窗 kh
//   erode : 行方向 min 窗 kw（出界=255）→ 列方向 min 窗 kh
// 中间图整幅缓存（w*h 字节；板端 ROI 规模可接受）
void morph_close_rect_u8(const uint8_t* src, int src_step,
                         uint8_t* dst, int dst_step,
                         int width, int height, int kw, int kh) {
  std::vector<uint8_t> tmp1((size_t)width * height);
  std::vector<uint8_t> tmp2((size_t)width * height);
  const int kx = kw / 2, ky = kh / 2;  // 锚点=核中心（OpenCV anchor 默认 (-1,-1)）
  // pass1: 行 max（dilate 行滤波，出界=0）
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = &tmp1[(size_t)y * width];
    for (int x = 0; x < width; ++x) {
      int lo = x - kx; if (lo < 0) lo = 0;
      int hi = x + kx; if (hi >= width) hi = width - 1;
      uint8_t v = 0;
      for (int xx = lo; xx <= hi; ++xx)
        if (s[xx] > v) v = s[xx];
      d[x] = v;
    }
  }
  // pass2: 列 max（dilate 完成，出界=0）
  for (int y = 0; y < height; ++y) {
    uint8_t* d = &tmp2[(size_t)y * width];
    int lo = y - ky; if (lo < 0) lo = 0;
    int hi = y + ky; if (hi >= height) hi = height - 1;
    for (int x = 0; x < width; ++x) {
      uint8_t v = 0;
      for (int yy = lo; yy <= hi; ++yy) {
        uint8_t sv = tmp1[(size_t)yy * width + x];
        if (sv > v) v = sv;
      }
      d[x] = v;
    }
  }
  // pass3: 行 min（erode 行滤波，出界=255）
  for (int y = 0; y < height; ++y) {
    uint8_t* d = &tmp1[(size_t)y * width];
    for (int x = 0; x < width; ++x) {
      int lo = x - kx; if (lo < 0) lo = 0;
      int hi = x + kx; if (hi >= width) hi = width - 1;
      uint8_t v = 255;
      for (int xx = lo; xx <= hi; ++xx)
        if (tmp2[(size_t)y * width + xx] < v) v = tmp2[(size_t)y * width + xx];
      d[x] = v;
    }
  }
  // pass4: 列 min（erode 完成）→ 写出（保 dst stride）
  for (int y = 0; y < height; ++y) {
    int lo = y - ky; if (lo < 0) lo = 0;
    int hi = y + ky; if (hi >= height) hi = height - 1;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      uint8_t v = 255;
      for (int yy = lo; yy <= hi; ++yy) {
        uint8_t sv = tmp1[(size_t)yy * width + x];
        if (sv < v) v = sv;
      }
      d[x] = v;
    }
  }
}

// ---- bitwise_not（8UC1）----
void bitwise_not_u8(const uint8_t* src, int src_step,
                    uint8_t* dst, int dst_step, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) d[x] = (uint8_t)~s[x];
  }
}

}  // namespace hwcv
