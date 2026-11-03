// core/arith.cpp — §3.2 像素运算核（裸指针实现）
#include "hwcv.h"

namespace hwcv {

// cvtColor BGR<->RGB：同一实现（交换 R/B）。OpenCV 语义：src/dst 可同一块内存（in-place）。
void cvt_color_bgr_rgb(const uint8_t* src, int src_step,
                       uint8_t* dst, int dst_step,
                       int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      uint8_t b = s[0], g = s[1], r = s[2];
      d[0] = r; d[1] = g; d[2] = b;
      s += 3; d += 3;
    }
  }
}

// cvtColor BGR2GRAY：OpenCV 4.5.5 用 15-bit 定点系数
//   (B*3735 + G*19235 + R*9798 + 16384) >> 15
//   RY15=9798, GY15=19235, BY15=3735（BT.601：0.299/0.587/0.114）
void cvt_color_bgr2gray(const uint8_t* src, int src_step,
                        uint8_t* dst, int dst_step,
                        int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      d[x] = (uint8_t)((s[0] * 3735 + s[1] * 19235 + s[2] * 9798 + 16384) >> 15);
      s += 3;
    }
  }
}

// cvtColor NV12/NV21 -> BGR（BT.601 limited range）
// 逐字节复刻 OpenCV color_yuv.simd.hpp 的 ITUR_BT_601_* 定点实现（20 位定点）：
//   y   = max(0, Y-16) * 1220542        // 注意：Y-16 先钳 0 再乘！
//   ruv = (1<<19) + 1673527*(V-128)
//   guv = (1<<19) - 852492*(V-128) - 409993*(U-128)
//   buv = (1<<19) + 2116026*(U-128)
//   R/G/B = saturate_u8((y + {r,g,b}uv) >> 20)
void cvt_color_nv12_to_bgr(const uint8_t* src_y, int src_y_step,
                           const uint8_t* src_uv, int src_uv_step,
                           uint8_t* dst, int dst_step,
                           int width, int height, bool uv_order) {
  const int CY = 1220542, CVR = 1673527, CVG = -852492, CUG = -409993,
            CUB = 2116026, SHIFT = 20;
  for (int y = 0; y < height; ++y) {
    const uint8_t* y_row = src_y + (size_t)y * src_y_step;
    const uint8_t* uv_row = src_uv + (size_t)(y >> 1) * src_uv_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      int yy = (y_row[x] > 16 ? y_row[x] - 16 : 0) * CY;
      const uint8_t* uv = uv_row + (x >> 1) * 2;
      int u = uv[uv_order ? 0 : 1] - 128;
      int v = uv[uv_order ? 1 : 0] - 128;
      int ruv = (1 << (SHIFT - 1)) + CVR * v;
      int guv = (1 << (SHIFT - 1)) + CVG * v + CUG * u;
      int buv = (1 << (SHIFT - 1)) + CUB * u;
      int r = (yy + ruv) >> SHIFT;
      int g = (yy + guv) >> SHIFT;
      int b = (yy + buv) >> SHIFT;
      d[0] = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
      d[1] = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
      d[2] = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
      d += 3;
    }
  }
}

// ---- 统计 / 掩码核（color_segment.cpp 调用点）----

// cv::mean：8UC3 ROI 逐行累加（保 stride），double 精度求平均
void mean_u8c3(const uint8_t* src, int src_step, int width, int height,
               double out[4]) {
  double sb = 0, sg = 0, sr = 0;
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    for (int x = 0; x < width; ++x) {
      sb += s[0]; sg += s[1]; sr += s[2];
      s += 3;
    }
  }
  size_t n = (size_t)width * height;
  out[0] = sb / n; out[1] = sg / n; out[2] = sr / n; out[3] = 0;
}

// cv::mean：8UC1（out[0]=均值，out[3]=0，与真 OpenCV 未用通道=0 一致）
void mean_u8c1(const uint8_t* src, int src_step, int width, int height,
               double out[4]) {
  double sum = 0;
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    for (int x = 0; x < width; ++x) sum += s[x];
  }
  size_t n = (size_t)width * height;
  out[0] = sum / n; out[1] = 0; out[2] = 0; out[3] = 0;
}

// cv::inRange：OpenCV 语义 = lower ≤ v ≤ upper（闭区间），Scalar 隐式 double→与 u8 比较
void in_range_u8c3(const uint8_t* src, int src_step, int width, int height,
                   const double lower[3], const double upper[3],
                   uint8_t* dst, int dst_step) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      bool in = s[0] >= lower[0] && s[0] <= upper[0] &&
                s[1] >= lower[1] && s[1] <= upper[1] &&
                s[2] >= lower[2] && s[2] <= upper[2];
      d[x] = in ? 255 : 0;
      s += 3;
    }
  }
}

// cv::countNonZero：8UC1，保 stride
long count_nonzero_u8(const uint8_t* src, int src_step, int width, int height) {
  long n = 0;
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    for (int x = 0; x < width; ++x)
      if (s[x]) n++;
  }
  return n;
}

// cv::addWeighted：dst = alpha*src1 + beta*src2 + gamma（多通道逐元素，uint8 饱和）
void add_weighted(const uint8_t* src1, int src1_step,
                  const uint8_t* src2, int src2_step,
                  uint8_t* dst, int dst_step,
                  int width, int height, double alpha, double beta, double gamma) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s1 = src1 + (size_t)y * src1_step;
    const uint8_t* s2 = src2 + (size_t)y * src2_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x) {
      double v = alpha * s1[x] + beta * s2[x] + gamma;
      if (v < 0) v = 0; else if (v > 255) v = 255;
      d[x] = (uint8_t)(v + 0.5);
    }
  }
}

// cv::compare(CMP_EQ)：逐元素相等比较，src1==src2 → 255, else 0
void compare_eq(const uint8_t* src1, int src1_step,
                const uint8_t* src2, int src2_step,
                uint8_t* dst, int dst_step, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s1 = src1 + (size_t)y * src1_step;
    const uint8_t* s2 = src2 + (size_t)y * src2_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x)
      d[x] = s1[x] == s2[x] ? 255 : 0;
  }
}

// cv::compare(Mat, Scalar, CMP_EQ)：逐元素与标量相等比较
void compare_scalar(const uint8_t* src, int src_step, uint8_t value,
                    uint8_t* dst, int dst_step, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s = src + (size_t)y * src_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x)
      d[x] = s[x] == value ? 255 : 0;
  }
}

// cv::bitwise_or（8UC1）：逐元素按位或
void bitwise_or_u8(const uint8_t* src1, int src1_step,
                   const uint8_t* src2, int src2_step,
                   uint8_t* dst, int dst_step, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* s1 = src1 + (size_t)y * src1_step;
    const uint8_t* s2 = src2 + (size_t)y * src2_step;
    uint8_t* d = dst + (size_t)y * dst_step;
    for (int x = 0; x < width; ++x)
      d[x] = s1[x] | s2[x];
  }
}

}  // namespace hwcv
