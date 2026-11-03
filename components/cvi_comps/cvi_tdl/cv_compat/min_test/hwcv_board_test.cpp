// tools/handwrite-cv/min_test/hwcv_board_test.cpp — 板端手写 CV 自测
//
// 覆盖所有核函数的基本调用路径，在板端 MSH shell 中运行：
//   hwcv_test
//
// 判据：每个核函数返回预期值 + 不会 crash/panic
#include <rtthread.h>
#include <cstring>
#include <cstdlib>
#include <cmath>

#include "hwcv.h"

// ---- 测试辅助 ----
static int g_ok = 0, g_fail = 0;

static void check(const char* name, bool cond) {
  if (cond) {
    g_ok++;
  } else {
    g_fail++;
    rt_kprintf("  FAIL: %s\n", name);
  }
}

// ---- 测试用例 ----

static void test_bgr_rgb() {
  uint8_t src[9] = {10, 20, 30, 40, 50, 60, 70, 80, 90};  // BGR BGR BGR
  uint8_t dst[9];
  hwcv::cvt_color_bgr_rgb(src, 9, dst, 9, 3, 1);
  // BGR(10,20,30) → RGB(30,20,10)
  check("bgr_rgb R", dst[0] == 30 && dst[3] == 60 && dst[6] == 90);
  check("bgr_rgb G", dst[1] == 20 && dst[4] == 50 && dst[7] == 80);
  check("bgr_rgb B", dst[2] == 10 && dst[5] == 40 && dst[8] == 70);
}

static void test_bgr2gray() {
  // 白色 (255,255,255) → 255
  uint8_t src[3] = {255, 255, 255};
  uint8_t dst;
  hwcv::cvt_color_bgr2gray(src, 3, &dst, 1, 1, 1);
  check("bgr2gray white", dst >= 254 && dst <= 255);

  // 黑色 (0,0,0) → 0
  src[0] = src[1] = src[2] = 0;
  hwcv::cvt_color_bgr2gray(src, 3, &dst, 1, 1, 1);
  check("bgr2gray black", dst == 0);
}

static void test_nv12_to_bgr() {
  // 灰度 NV12：Y=128, UV=128 → 灰色 BGR
  const int W = 4, H = 2;
  uint8_t y[W * H], uv[W * H / 2];
  uint8_t bgr[W * H * 3];
  memset(y, 128, sizeof(y));
  memset(uv, 128, sizeof(uv));
  hwcv::cvt_color_nv12_to_bgr(y, W, uv, W, bgr, W * 3, W, H, true);
  // 灰色 BGR 各通道应接近 128
  check("nv12_to_bgr gray", bgr[0] >= 120 && bgr[0] <= 136);
}

static void test_mean() {
  uint8_t src[12] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
  double out[4];
  hwcv::mean_u8c3(src, 12, 4, 1, out);
  // B channel: (10+40+70+100)/4 = 55
  check("mean_u8c3 B", std::fabs(out[0] - 55.0) < 0.01);
  check("mean_u8c3 G", std::fabs(out[1] - 65.0) < 0.01);
  check("mean_u8c3 R", std::fabs(out[2] - 75.0) < 0.01);
}

static void test_in_range() {
  uint8_t src[9] = {10, 50, 100, 200, 150, 30, 80, 90, 255};
  uint8_t dst[9];
  double lo[3] = {20, 20, 20}, hi[3] = {100, 100, 100};
  hwcv::in_range_u8c3(src, 9, 4, 3, lo, hi, dst, 3);
  // 3 像素，每个 3 通道全在 [20,100] 内的才输出 255
  check("in_range px0", dst[0] == 0);   // (10,50,100): B=10<20 → 0
  check("in_range px1", dst[1] == 0);   // (200,150,30): B=200>100 → 0
  check("in_range px2", dst[2] == 0);   // (80,90,255): R=255>100 → 0

  // 补一个真正在范围内的像素
  uint8_t src2[3] = {50, 60, 70};  // BGR 全在 [20,100]
  uint8_t dst2;
  hwcv::in_range_u8c3(src2, 3, 1, 1, lo, hi, &dst2, 1);
  check("in_range in", dst2 == 255);
}

static void test_count_nonzero() {
  uint8_t src[5] = {0, 255, 0, 128, 255};
  long n = hwcv::count_nonzero_u8(src, 5, 5, 1);
  check("count_nonzero", n == 3);
}

static void test_ccl() {
  // 简单的 2x2 全前景 8C
  uint8_t src[4] = {255, 255, 255, 255};
  int32_t labels[4];
  int32_t stats[2 * 5];
  double centroids[2 * 2];
  int n = hwcv::connected_components_with_stats(
      src, 2, 2, 2, 8,
      labels, 2 * 4, stats, 5 * 4, centroids, 2 * 8);
  check("ccl nLabels", n == 2);  // 背景 + 1 个前景组件
  check("ccl stats area", stats[5 + 4] == 4);  // 前景组件面积=4
}

static void test_laplacian() {
  // 5×5 sparse: center(2,2)=10, rest=0
  // REFLECT_101 on 5×5: neighbors of (2,2) are (1,2),(3,2),(2,1),(2,3) all 0
  // laplacian(2,2) = 4*10 - 0 - 0 - 0 - 0 = 40
  uint8_t src[25];
  memset(src, 0, 25);
  src[2 * 5 + 2] = 10;
  double dst[25];
  hwcv::laplacian_64f(src, 5, dst, 5 * 8, 5, 5);
  // Laplacian kernel: {0,1,0; 1,-4,1; 0,1,0} → neighbors - 4*center = 0 - 40 = -40
  check("laplacian center", std::fabs(dst[2 * 5 + 2] - (-40.0)) < 1.0);
}

static void test_threshold() {
  double src[3] = {1.0, 5.0, 10.0};
  double dst[3];
  hwcv::threshold_trunc_f64(src, 3 * 8, dst, 3 * 8, 3, 1, 5.0);
  check("threshold_trunc", dst[0] == 1.0 && dst[1] == 5.0 && dst[2] == 5.0);

  uint8_t su8[3] = {100, 200, 50};
  uint8_t du8[3];
  hwcv::threshold_binary_u8(su8, 3, du8, 3, 3, 1, 128.0);
  check("threshold_binary", du8[0] == 0 && du8[1] == 255 && du8[2] == 0);
}

static void test_convert_scale_abs() {
  // Use integer values: no rounding ambiguity regardless of FPU rounding mode
  double src[3] = {-2.0, 0.0, 300.0};
  uint8_t dst[3];
  hwcv::convert_scale_abs_f64(src, 3 * 8, dst, 3, 3, 1);
  // |-2|=2 -> 2, |0|=0 -> 0, |300|=300 -> clamp 255
  check("convert_scale_abs", dst[0] == 2 && dst[1] == 0 && dst[2] == 255);
}

static void test_morph_close() {
  uint8_t src[9] = {0, 0, 0, 0, 255, 0, 0, 0, 0};
  uint8_t dst[9];
  hwcv::morph_close_rect_u8(src, 3, dst, 3, 3, 3, 3, 3);
  // 3x3 close 应填充孤立点周围的区域
  check("morph_close", dst[4] == 255);
}

static void test_bitwise_not() {
  uint8_t src[3] = {0, 128, 255};
  uint8_t dst[3];
  hwcv::bitwise_not_u8(src, 3, dst, 3, 3, 1);
  check("bitwise_not", dst[0] == 255 && dst[1] == 127 && dst[2] == 0);
}

static void test_add_weighted() {
  uint8_t s1[3] = {100, 50, 200}, s2[3] = {50, 100, 0};
  uint8_t dst[3];
  hwcv::add_weighted(s1, 3, s2, 3, dst, 3, 3, 1, 0.5, 0.5, 0);
  // 0.5*100 + 0.5*50 = 75
  check("add_weighted", dst[0] == 75 && dst[1] == 75 && dst[2] == 100);
}

static void test_compare() {
  uint8_t s1[3] = {1, 2, 3}, s2[3] = {1, 0, 3};
  uint8_t dst[3];
  hwcv::compare_eq(s1, 3, s2, 3, dst, 3, 3, 1);
  check("compare_eq", dst[0] == 255 && dst[1] == 0 && dst[2] == 255);
}

static void test_bitwise_or() {
  uint8_t s1[3] = {0x0F, 0xF0, 0xAA}, s2[3] = {0xF0, 0x0F, 0x55};
  uint8_t dst[3];
  hwcv::bitwise_or_u8(s1, 3, s2, 3, dst, 3, 3, 1);
  check("bitwise_or", dst[0] == 0xFF && dst[1] == 0xFF && dst[2] == 0xFF);
}

static void test_circle() {
  // 5x5 image, center (2,2), r=1, color=255 → filled disk (Manhattan-like but Euclidean)
  uint8_t img[25]; memset(img, 0, sizeof(img));
  hwcv::circle_filled_u8(img, 5, 5, 5, 2, 2, 1, 255);
  check("circle center", img[2 * 5 + 2] == 255);
  check("circle N/S/E/W", img[1 * 5 + 2] == 255 && img[3 * 5 + 2] == 255 &&
                           img[2 * 5 + 1] == 255 && img[2 * 5 + 3] == 255);
  check("circle corner clear", img[0] == 0 && img[4] == 0 &&
                                 img[20] == 0 && img[24] == 0);
  // r=0 → single pixel
  uint8_t img2[9]; memset(img2, 0, sizeof(img2));
  hwcv::circle_filled_u8(img2, 3, 3, 3, 1, 1, 0, 200);
  check("circle r=0 single", img2[1 * 3 + 1] == 200 && img2[0] == 0);
  // negative radius → no-op
  uint8_t img3[4] = {10, 20, 30, 40};
  hwcv::circle_filled_u8(img3, 2, 2, 2, 0, 0, -1, 255);
  check("circle r<0 noop", img3[0] == 10 && img3[3] == 40);
}

// ---- MSH 命令入口 ----
extern "C" int hwcv_test(void) {
  g_ok = 0; g_fail = 0;

  rt_kprintf("\n=== handwrite-cv board self-test ===\n");

  test_bgr_rgb();
  test_bgr2gray();
  test_nv12_to_bgr();
  test_mean();
  test_in_range();
  test_count_nonzero();
  test_ccl();
  test_laplacian();
  test_threshold();
  test_convert_scale_abs();
  test_morph_close();
  test_bitwise_not();
  test_add_weighted();
  test_compare();
  test_bitwise_or();
  test_circle();

  rt_kprintf("=== result: %d OK, %d FAIL ===\n\n", g_ok, g_fail);
  return g_fail;
}
MSH_CMD_EXPORT(hwcv_test, run handwrite-cv kernel self-test);