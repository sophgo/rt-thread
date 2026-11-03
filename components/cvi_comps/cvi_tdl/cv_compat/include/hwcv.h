// hwcv.h — 手写 CV 算法核统一裸指针 API（Route B 时整体搬入 tdl_sdk）
//
// 纪律：
// 1. 核函数只收裸指针 + 宽高 + 步长，不依赖 fake_cv 任何类型
// 2. 每个核与真 OpenCV 做 L1 差分（tests/unit/），绿了才算存在
// 3. 命名：hwcv_<opencv 对应函数名小写下划线>
#pragma once

#include <cstddef>
#include <cstdint>

namespace hwcv {

// ---- 像素格式转换（core/arith.cpp）----

// cvtColor COLOR_BGR2RGB / COLOR_RGB2BGR（同一实现：交换 R/B 通道）
// src/dst: 3 通道交错 uint8；步长单位=字节
void cvt_color_bgr_rgb(const uint8_t* src, int src_step,
                       uint8_t* dst, int dst_step,
                       int width, int height);

// cvtColor COLOR_BGR2GRAY（BT.601，与 OpenCV 取整对齐——见 L1 差分）
void cvt_color_bgr2gray(const uint8_t* src, int src_step,
                        uint8_t* dst, int dst_step,
                        int width, int height);

// cvtColor COLOR_YUV2BGR_NV12 / NV21（BT.601 limited，L1 判据 |Δ|≤1）
void cvt_color_nv12_to_bgr(const uint8_t* src_y, int src_y_step,
                           const uint8_t* src_uv, int src_uv_step,
                           uint8_t* dst, int dst_step,
                           int width, int height, bool uv_order /*true=UV(NV12)*/);

// ---- 统计 / 掩码核（color_segment L3 收口所需，core/arith.cpp）----

// cv::mean(8UC3 ROI) —— 返回 4 通道均值（B,G,R,0），ROI 保 stride 逐行累加
void mean_u8c3(const uint8_t* src, int src_step, int width, int height,
               double out[4]);

// cv::mean(8UC1) —— out[0]=均值，其余 0
void mean_u8c1(const uint8_t* src, int src_step, int width, int height,
               double out[4]);

// cv::inRange(8UC3, Scalar lower, Scalar upper) —— 逐像素 3 通道全在界内 → 255，否则 0
void in_range_u8c3(const uint8_t* src, int src_step, int width, int height,
                   const double lower[3], const double upper[3],
                   uint8_t* dst, int dst_step);

// cv::countNonZero(8UC1)
long count_nonzero_u8(const uint8_t* src, int src_step, int width, int height);

// ---- 连通域（core/ccl.cpp）----

// cv::connectedComponentsWithStats(8UC1, connectivity=8, ltype=CV_32S)
// labels: int32 [h][w]（步长 labels_step 字节）
// stats:    int32 [num+1][5]（x,y,w,h,area；行 0=背景）
// centroids: double [num+1][2]
// 返回连通域个数 num（含背景 0 号）；-1 = 失败
int connected_components_with_stats(const uint8_t* src, int src_step,
                                    int width, int height, int connectivity,
                                    int32_t* labels, int labels_step,
                                    int32_t* stats, int stats_step,
                                    double* centroids, int centroids_step);

// ---- 滤波 / 形态学 / 阈值（core/filter.cpp，occlusion_detect 链）----

// cv::Laplacian(8UC1, CV_64F, ksize=1)：核 {0,1,0;1,-4,1;0,1,0}（浮点卷积），
// BORDER_DEFAULT=BORDER_REFLECT_101（gfedcba|abcdefgh|hgfedgb），锚点=核中心。
// 双精度累加，结果位级一致（无舍入路径）
void laplacian_64f(const uint8_t* src, int src_step,
                   double* dst, int dst_step, int width, int height);

// cv::threshold THRESH_TRUNC（64F）：dst = min(src, thresh)
void threshold_trunc_f64(const double* src, int src_step,
                         double* dst, int dst_step,
                         int width, int height, double thresh);

// cv::threshold THRESH_BINARY（8U）：dst = src > thresh ? 255 : 0（严格 >）
void threshold_binary_u8(const uint8_t* src, int src_step,
                         uint8_t* dst, int dst_step,
                         int width, int height, double thresh);

// cv::convertScaleAbs（64F→8U, α/β 可调）：saturate_cast<uchar>(|αx+β|)
// 注意 cvRound 为四舍五入到偶（banker's rounding），由核内实现
void convert_scale_abs_f64(const double* src, int src_step,
                           uint8_t* dst, int dst_step,
                           int width, int height,
                           double alpha = 1.0, double beta = 0.0);

// cv::morphologyEx MORPH_CLOSE + MORPH_RECT 核（可分离）：
//   dilate(行 max 窗口 kw) → 列 max 窗口 kh → erode(行 min) → 列 min
// 形态学默认 BORDER_CONSTANT：dilate 边界=0（max 幺元），erode 边界=255（min 幺元）
void morph_close_rect_u8(const uint8_t* src, int src_step,
                         uint8_t* dst, int dst_step,
                         int width, int height, int kw, int kh);

// cv::bitwise_not（8UC1）
void bitwise_not_u8(const uint8_t* src, int src_step,
                    uint8_t* dst, int dst_step, int width, int height);

// cv::addWeighted：dst = alpha*src1 + beta*src2 + gamma（多通道逐元素，uint8 饱和）
void add_weighted(const uint8_t* src1, int src1_step,
                  const uint8_t* src2, int src2_step,
                  uint8_t* dst, int dst_step,
                  int width, int height, double alpha, double beta, double gamma);

// cv::compare(CMP_EQ)：逐元素相等比较，src1==src2 → 255, else 0
void compare_eq(const uint8_t* src1, int src1_step,
                const uint8_t* src2, int src2_step,
                uint8_t* dst, int dst_step, int width, int height);

// cv::compare(Mat, Scalar, CMP_EQ)：逐元素与标量相等比较
void compare_scalar(const uint8_t* src, int src_step, uint8_t value,
                    uint8_t* dst, int dst_step, int width, int height);

// cv::bitwise_or（8UC1）：逐元素按位或
void bitwise_or_u8(const uint8_t* src1, int src1_step,
                   const uint8_t* src2, int src2_step,
                   uint8_t* dst, int dst_step, int width, int height);

// ---- 绘图（core/draw.cpp，grabcut_segment 种子点）----

// cv::circle filled（thickness=-1）：8UC1 图像上画填充圆
// (x-cx)^2 + (y-cy)^2 <= r^2 的像素填 color；半径 <0 视为 no-op
void circle_filled_u8(uint8_t* img, int img_step, int img_w, int img_h,
                     int cx, int cy, int radius, uint8_t color);

}  // namespace hwcv
