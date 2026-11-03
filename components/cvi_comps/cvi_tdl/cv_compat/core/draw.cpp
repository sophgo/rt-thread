// tools/handwrite-cv/core/draw.cpp
// 填充圆：与 cv::circle(..., -1) byte-exact 对齐
// 算法：对每一条扫描线 y，求圆与该线的相交水平区间并填充
//   (x - cx)^2 + (y - cy)^2 <= r^2  →  |x - cx| <= floor(sqrt(r^2 - (y-cy)^2))
// 参考 OpenCV 4.5.5 imgproc/src/drawing.cpp cv::circle 的 filled 路径：
//   当 thickness<0 时按上述整数距离判定填充，与 scanline 算法等价
#include "hwcv.h"
#include <cmath>
#include <cstring>

namespace hwcv {

void circle_filled_u8(uint8_t* img, int img_step, int img_w, int img_h,
                     int cx, int cy, int radius, uint8_t color) {
  if (!img || radius < 0 || img_w <= 0 || img_h <= 0) return;

  // 扫描线 y 范围裁剪到图像内
  int y0 = cy - radius;
  int y1 = cy + radius;
  if (y0 < 0) y0 = 0;
  if (y1 >= img_h) y1 = img_h - 1;

  long long r2 = (long long)radius * radius;

  for (int y = y0; y <= y1; ++y) {
    long long dy = (long long)(y - cy);
    long long rem = r2 - dy * dy;
    if (rem < 0) continue;
    int dx = (int)std::floor(std::sqrt((double)rem));
    int x0 = cx - dx;
    int x1 = cx + dx;
    if (x0 < 0) x0 = 0;
    if (x1 >= img_w) x1 = img_w - 1;
    if (x0 > x1) continue;
    uint8_t* row = img + (size_t)y * img_step;
    memset(row + x0, color, (size_t)(x1 - x0 + 1));
  }
}

}  // namespace hwcv
