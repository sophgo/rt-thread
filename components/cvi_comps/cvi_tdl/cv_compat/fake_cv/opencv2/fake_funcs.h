// fake_funcs.h — fake_cv 的函数门面（Route A 核心）
//
// 把 SDK 调用点的 cv::xxx(...) 转发到已通过 L1 差分的 hwcv 裸指针核。
// 纪律：
// 1. 这里只做「拆 Mat → 取 data/step/宽高 → 调 hwcv 核 → 回填」，不做算法
// 2. 每个 fake 函数对应一个 L1 绿核；没有核的函数不在这里出现（编译期暴露缺口）
// 3. L3 判据：同一 SDK 源文件 golden(真 OpenCV) / fake(本头) 双编均通过
#pragma once

#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>    // atan2f for cvFastArctan
#include <ctime>    // clock for getTickCount

#include "fake_mat.h"
#include "hwcv.h"
#include "cvi_jpg_interface.h"
#include "cvi_sys.h"

FAKE_CV_NS_BEGIN

// ---- cvtColor 色彩码（SDK 用到的子集；数值与真 OpenCV 一致）----
// SDK 源两种写法都出现：cv::COLOR_BGR2GRAY 与裸 COLOR_YUV2BGR_NV12。
// 因此除 enum 外再给 cv:: 域内的同名常量引用（真 OpenCV 也是 enum 成员，
// fake 侧用引用模拟「cv::COLOR_xxx 可用」）。
enum {
  FCVX_COLOR_BGR2BGRA = 0, FCVX_COLOR_BGR2GRAY = 6, FCVX_COLOR_RGB2GRAY = 7,
  FCVX_COLOR_BGR2RGB = 4, FCVX_COLOR_RGB2BGR = 4,
  FCVX_COLOR_YUV2BGR_NV12 = 90, FCVX_COLOR_YUV2RGB_NV12 = 91,
  FCVX_COLOR_YUV2RGB_NV21 = 92, FCVX_COLOR_YUV2BGR_NV21 = 93,
};
// enum 成员无法用 using 别名带 cv:: 前缀访问 —— 直接在 cv 域内建同名常量对象
static const int COLOR_BGR2GRAY_ = FCVX_COLOR_BGR2GRAY;
static const int COLOR_RGB2GRAY_ = FCVX_COLOR_RGB2GRAY;
static const int COLOR_BGR2RGB_ = FCVX_COLOR_BGR2RGB;
static const int COLOR_RGB2BGR_ = FCVX_COLOR_RGB2BGR;
static const int COLOR_YUV2BGR_NV12_ = FCVX_COLOR_YUV2BGR_NV12;
static const int COLOR_YUV2BGR_NV21_ = FCVX_COLOR_YUV2BGR_NV21;
// cv::COLOR_xxx 形态：cv 域内 const 对象即可（引用自枚举值）
static const int COLOR_BGR2GRAY = FCVX_COLOR_BGR2GRAY;
static const int COLOR_RGB2GRAY = FCVX_COLOR_RGB2GRAY;
static const int COLOR_BGR2RGB = FCVX_COLOR_BGR2RGB;
static const int COLOR_RGB2BGR = FCVX_COLOR_RGB2BGR;
static const int COLOR_YUV2BGR_NV12 = FCVX_COLOR_YUV2BGR_NV12;
static const int COLOR_YUV2BGR_NV21 = FCVX_COLOR_YUV2BGR_NV21;

// cvtColor（已支持码）：BGR2GRAY / BGR2RGB / RGB2BGR / NV12 / NV21
// 运行时 int code 分发（SDK 源里 cvt_code 常是运行期变量，如 image_encoder.cpp:228；
// 编译期 template 形态已弃 —— 真 OpenCV 签名就是普通函数，保持一致最省事）
inline void cvtColor(const Mat& src, Mat& dst, int code) {
  switch (code) {
    case FCVX_COLOR_BGR2GRAY:
      dst.create(src.rows, src.cols, FCVX_CV_8U);
      hwcv::cvt_color_bgr2gray(src.data, (int)src.step, dst.data, (int)dst.step,
                                src.cols, src.rows);
      break;
    case FCVX_COLOR_BGR2RGB:
      dst.create(src.rows, src.cols, FCVX_CV_8U + 16);
      hwcv::cvt_color_bgr_rgb(src.data, (int)src.step, dst.data, (int)dst.step,
                              src.cols, src.rows);
      break;
    // NV12/NV21 → RGB（SDK image_factory.cpp:337-338；输入为单 Mat, rows=H*3/2）
    case FCVX_COLOR_YUV2RGB_NV12:
    case FCVX_COLOR_YUV2RGB_NV21:
    case FCVX_COLOR_YUV2BGR_NV12:
    case FCVX_COLOR_YUV2BGR_NV21: {
      int W = src.cols;
      int H = src.rows * 2 / 3;
      int step = (int)src.step;
      const uint8_t* y_ptr = src.data;
      const uint8_t* uv_ptr = src.data + (size_t)step * H;
      bool uv_order = (code == FCVX_COLOR_YUV2RGB_NV12 ||
                       code == FCVX_COLOR_YUV2BGR_NV12);
      bool rgb_out = (code == FCVX_COLOR_YUV2RGB_NV12 ||
                      code == FCVX_COLOR_YUV2RGB_NV21);
      dst.create(H, W, FCVX_CV_8U + 16);
      hwcv::cvt_color_nv12_to_bgr(y_ptr, step, uv_ptr, step,
                                  dst.data, (int)dst.step, W, H, uv_order);
      if (rgb_out) {
        // 核输出 BGR → 通道交换为 RGB
        for (int p = 0; p < W * H; ++p) {
          uint8_t* px = dst.data + (size_t)p * 3;
          uint8_t tmp = px[0]; px[0] = px[2]; px[2] = tmp;
        }
      }
      break;
    }
    default:
      // 未支持码：真侧走不到的分支；到这 = 显式缺口（L4/板上跑时暴露）
      std::abort();
  }
}
// NV12/NV21：src 为双平面（Y 平面 h 行 + UV 平面 h/2 行，同一 Mat）。
// SDK 用法（yolo_sort 等拿到的是 Y/UV 分离指针），此处收分离平面。
inline void cvtColor_yuv2bgr_nv12(const Mat& y, const Mat& uv, Mat& bgr,
                                  bool nv21_order) {
  bgr.create(y.rows, y.cols, FCVX_CV_8U + 16);
  hwcv::cvt_color_nv12_to_bgr(y.data, (int)y.step, uv.data, (int)uv.step,
                              bgr.data, (int)bgr.step, y.cols, y.rows,
                              !nv21_order);
}

// ---- mean（cv::mean 返回 Scalar，mean[0..2] 取 B/G/R）----
inline Scalar mean(const Mat& m) {
  // 现支持 8UC1/8UC3（color_segment 路径 8UC3）
  Scalar s;
  if (m.channels() == 3 && m.depth() == FCVX_CV_8U) {
    hwcv::mean_u8c3(m.data, (int)m.step, m.cols, m.rows, s.val);
  } else if (m.channels() == 1 && m.depth() == FCVX_CV_8U) {
    hwcv::mean_u8c1(m.data, (int)m.step, m.cols, m.rows, s.val);
  }
  return s;
}

// ---- inRange（8UC3, Scalar 界）----
inline void inRange(const Mat& src, const Scalar& lower, const Scalar& upper,
                    Mat& dst) {
  dst.create(src.rows, src.cols, FCVX_CV_8U);
  hwcv::in_range_u8c3(src.data, (int)src.step, src.cols, src.rows,
                      lower.val, upper.val, dst.data, (int)dst.step);
}

// ---- countNonZero（8UC1）----
inline int countNonZero(const Mat& m) {
  return (int)hwcv::count_nonzero_u8(m.data, (int)m.step, m.cols, m.rows);
}

// ---- connectedComponentsWithStats（8UC1 → 32S labels + stats + centroids）----
inline int connectedComponentsWithStats(const Mat& image, Mat& labels,
                                        Mat& stats, Mat& centroids,
                                        int connectivity = 8,
                                        int ltype = FCVX_CV_32S) {
  (void)ltype;  // fake 侧只有 32S 一种实现（SDK 也只用 32S）
  labels.create(image.rows, image.cols, FCVX_CV_32S);
  // 域数上界（OpenCV Wu 上界 (w*h+1)/2+1）；hwcv 核一次跑完，拷入自有 Mat（n 通常很小）
  int max_labels = (int)(((size_t)image.rows * image.cols + 1) / 2 + 2);
  std::vector<int32_t> sbuf((size_t)max_labels * 5);
  std::vector<double> cbuf((size_t)max_labels * 2);
  int n = hwcv::connected_components_with_stats(
      image.data, (int)image.step, image.cols, image.rows, connectivity,
      (int32_t*)labels.data, (int)labels.step,
      sbuf.data(), (int)(5 * sizeof(int32_t)),
      cbuf.data(), (int)(2 * sizeof(double)));
  stats.create(n, 5, FCVX_CV_32S);
  centroids.create(n, 2, FCVX_CV_64F);
  memcpy(stats.data, sbuf.data(), (size_t)n * 5 * sizeof(int32_t));
  memcpy(centroids.data, cbuf.data(), (size_t)n * 2 * sizeof(double));
  return n;
}

// ---- connectedComponents（labels-only；occlusion_detect 4 连通）----
// hwcv CCL 核 stats/centroids 传 null 即只算 labels
inline int connectedComponents(const Mat& image, Mat& labels,
                               int connectivity = 8,
                               int ltype = FCVX_CV_32S) {
  (void)ltype;
  labels.create(image.rows, image.cols, FCVX_CV_32S);
  return hwcv::connected_components_with_stats(
      image.data, (int)image.step, image.cols, image.rows, connectivity,
      (int32_t*)labels.data, (int)labels.step,
      nullptr, 0, nullptr, 0);
}

// ---- 阈值 / 滤波 / 形态学（occlusion_detect 链，核见 core/filter.cpp）----

// 阈值类型常量（与真 OpenCV imgproc.hpp 数值一致；域内 const 对象 + enum 双形态）
enum {
  FCVX_THRESH_BINARY = 0,
  FCVX_THRESH_BINARY_INV = 1,
  FCVX_THRESH_TRUNC = 2,
};
static const int THRESH_BINARY = FCVX_THRESH_BINARY;
static const int THRESH_BINARY_INV = FCVX_THRESH_BINARY_INV;
static const int THRESH_TRUNC = FCVX_THRESH_TRUNC;

inline void threshold(const Mat& src, Mat& dst, double thresh, double maxval,
                      int type) {
  if (type == FCVX_THRESH_TRUNC && src.depth() == FCVX_CV_64F) {
    dst.create(src.rows, src.cols, src.type());
    hwcv::threshold_trunc_f64((const double*)src.data, (int)src.step,
                              (double*)dst.data, (int)dst.step,
                              src.cols, src.rows, thresh);
  } else if (type == FCVX_THRESH_BINARY && src.depth() == FCVX_CV_8U) {
    dst.create(src.rows, src.cols, src.type());
    hwcv::threshold_binary_u8(src.data, (int)src.step, dst.data, (int)dst.step,
                              src.cols, src.rows, thresh);
  } else if (type == FCVX_THRESH_BINARY_INV && src.depth() == FCVX_CV_8U) {
    // BINARY_INV = 255 - BINARY（SDK 未用，grabcut 预留；核未做，走 BINARY 差分语义待 L1）
    dst.create(src.rows, src.cols, src.type());
    hwcv::threshold_binary_u8(src.data, (int)src.step, dst.data, (int)dst.step,
                              src.cols, src.rows, thresh);
    hwcv::bitwise_not_u8(dst.data, (int)dst.step, dst.data, (int)dst.step,
                         src.cols, src.rows);
    (void)maxval;
  }
  (void)maxval;
}

// Laplacian（8UC1 → 64F，ksize=1；SDK 只用默认参数）
inline void Laplacian(const Mat& src, Mat& dst, int ddepth,
                      int ksize = 1, double scale = 1, double delta = 0,
                      int borderType = 4 /*BORDER_DEFAULT*/) {
  (void)ksize; (void)scale; (void)delta; (void)borderType;
  // ddepth 必为 CV_64F（SDK 唯一调用形态），输出 64FC1
  dst.create(src.rows, src.cols, ddepth <= FCVX_CV_64F ? FCVX_CV_64F : ddepth);
  hwcv::laplacian_64f(src.data, (int)src.step,
                      (double*)dst.data, (int)dst.step,
                      src.cols, src.rows);
}

// convertScaleAbs（64F→8U；α/β 默认 1/0）
inline void convertScaleAbs(const Mat& src, Mat& dst, double alpha = 1,
                            double beta = 0) {
  dst.create(src.rows, src.cols, FCVX_CV_8U);
  hwcv::convert_scale_abs_f64((const double*)src.data, (int)src.step,
                              dst.data, (int)dst.step,
                              src.cols, src.rows, alpha, beta);
}

// 形态学形状/操作常量（数值与真 OpenCV 一致；域内 const 对象）
enum {
  FCVX_MORPH_RECT = 0,
  FCVX_MORPH_CLOSE = 3,
};
static const int MORPH_RECT = FCVX_MORPH_RECT;
static const int MORPH_CLOSE = FCVX_MORPH_CLOSE;

// getStructuringElement（MORPH_RECT → 全 1 u8 核；其他形状 SDK 未用）
inline Mat getStructuringElement(int shape, const Size& ksize,
                                 const Point& anchor = Point(-1, -1)) {
  (void)anchor;
  Mat k(ksize.height, ksize.width, FCVX_CV_8U);
  if (shape == FCVX_MORPH_RECT) memset(k.data, 1, (size_t)k.step * k.rows);
  return k;
}

// morphologyEx（仅 MORPH_CLOSE + rect 核路径；occlusion 调用形态）
inline void morphologyEx(const Mat& src, Mat& dst, int op, const Mat& kernel,
                         const Point& anchor = Point(-1, -1), int iterations = 1,
                         int borderType = 0 /*BORDER_CONSTANT*/,
                         const Scalar& borderValue = Scalar()) {
  (void)anchor; (void)iterations; (void)borderType; (void)borderValue;
  if (op == FCVX_MORPH_CLOSE) {
    dst.create(src.rows, src.cols, src.type());
    hwcv::morph_close_rect_u8(src.data, (int)src.step, dst.data, (int)dst.step,
                              src.cols, src.rows, kernel.cols, kernel.rows);
  }
}

// bitwise_not（8UC1）
inline void bitwise_not(const Mat& src, Mat& dst) {
  dst.create(src.rows, src.cols, src.type());
  hwcv::bitwise_not_u8(src.data, (int)src.step, dst.data, (int)dst.step,
                       src.cols, src.rows);
}

// ---- addWeighted（sot.cpp:289：cv::addWeighted(bg, 1, crop, 1, 0, crop)）----
inline void addWeighted(const Mat& src1, double alpha, const Mat& src2,
                        double beta, double gamma, Mat& dst, int dtype = -1) {
  (void)dtype;
  dst.create(src1.rows, src1.cols, src1.type());
  hwcv::add_weighted(src1.data, (int)src1.step, src2.data, (int)src2.step,
                     dst.data, (int)dst.step, src1.cols, src1.rows,
                     alpha, beta, gamma);
}

// ---- merge（image_factory.cpp:218 `cv::merge(channels, img)`）----
inline void merge(std::vector<Mat>& mv, Mat& dst) {
  if (mv.empty()) return;
  dst.create(mv[0].rows, mv[0].cols, (mv[0].depth() | ((mv.size() - 1) << 3)));
  for (size_t c = 0; c < mv.size(); ++c) {
    int es = mv[c].elem_size();
    for (int y = 0; y < mv[c].rows; ++y)
      memcpy(dst.data + (size_t)y * dst.step[0] + c * es,
             mv[c].data + (size_t)y * mv[c].step[0],
             (size_t)mv[c].cols * es);
  }
}

// ---- 常量扩展（SDK 用到但尚未定义的）----
static const int RETR_TREE = 3;
static const int CHAIN_APPROX_SIMPLE = 2;
static const int DECOMP_SVD = 4;
static const int COLOR_YUV2RGB_NV12 = 91;
static const int COLOR_YUV2RGB_NV21 = 92;
static const int IMWRITE_JPEG_QUALITY = 1;

// ---- findContours（TODO：复杂算法，编译器桩，板端调用点需替换为手写核）----
inline void findContours(const Mat&, std::vector<std::vector<Point>>&,
                         std::vector<Vec4i>&, int, int,
                         const Point& = Point()) {
  // TODO: 实现轮廓提取（联通域边界追踪）
  // SDK 调用点：tdl_core.cpp:933-935（ISP 分类后处理）
  std::abort();
}

// ---- solve（TODO：线性求解器，板端调用点需替换为 Eigen）----
inline bool solve(const Mat&, const Mat&, Mat&, int = 0) {
  // TODO: 实现线性求解（最小二乘/SVD）
  // SDK 调用点：image_alignment.cpp:777
  std::abort();
  return false;
}

// ---- imread YUV 直通（跳过 JPU，读 .nv12/.yuv 文件直接进 VPSS）----
// 文件名需含 _WxH 尺寸编码（如 img_640x480.nv12），否则无法推断分辨率
inline Mat imreadYUV(const std::string& filename, int /*flags*/) {
  int width = 0, height = 0;
  // 从文件名解析 _WxH（取最后一个 _ 之后到扩展名之前）
  size_t pos = filename.rfind('_');
  if (pos != std::string::npos) {
    if (sscanf(filename.c_str() + pos + 1, "%dx%d", &width, &height) != 2) {
      width = height = 0;
    }
  }
  if (width <= 0 || height <= 0) {
    rt_kprintf("[YUV ERR] need _WxH in filename: %s\n", filename.c_str());
    return Mat();
  }

  size_t y_size = (size_t)width * height;
  size_t uv_size = y_size / 2;       // NV12: UV 半交错 = w*h/2
  size_t yuv_size = y_size + uv_size;

  FILE* fp = fopen(filename.c_str(), "rb");
  if (!fp) { rt_kprintf("[YUV ERR] open failed: %s\n", filename.c_str()); return Mat(); }
  uint8_t* yuv_buf = (uint8_t*)malloc(yuv_size);
  if (!yuv_buf) { fclose(fp); return Mat(); }
  size_t nread = fread(yuv_buf, 1, yuv_size, fp);
  fclose(fp);
  if (nread != yuv_size) {
    rt_kprintf("[YUV ERR] size mismatch: read %zu, expect %zu (%s)\n",
               nread, yuv_size, filename.c_str());
    free(yuv_buf); return Mat();
  }

  // 构造 Mat 携带 YUV 原始数据（data = Y平面起始，UV 跟在 Y 之后）
  Mat mat;
  mat.rows = height;
  mat.cols = width;
  mat.type_ = CV_8UC1_;
  mat.step = MStep((size_t)width);
  mat.data = yuv_buf;
  mat.datastart = yuv_buf;
  mat.holder = new MatData();
  mat.holder->base = yuv_buf;
  mat.is_yuv_ = true;
  mat.yuv_format_ = 0;  // NV12

  rt_kprintf("[YUV] %s: %dx%d NV12, size=%zu\n", filename.c_str(), width, height, yuv_size);
  return mat;
}

// ---- imread（使用 CVI 硬件 JPEG 解码器；.nv12/.yuv 走 YUV 直通）----
inline Mat imread(const std::string& filename, int flags = 1) {
  // YUV 文件直通：按后缀分流，跳过 JPU
  {
    std::string fn = filename;
    for (auto& c : fn) c = (char)tolower((unsigned char)c);
    auto ends_with = [&](const char* s) {
      size_t n = strlen(s);
      return fn.size() >= n && fn.compare(fn.size() - n, n, s) == 0;
    };
    if (ends_with(".nv12") || ends_with(".yuv")) {
      return imreadYUV(filename, flags);
    }
  }

  // 读取 JPEG 文件
  FILE* fp = fopen(filename.c_str(), "rb");
  if (!fp) return Mat();

  fseek(fp, 0, SEEK_END);
  size_t file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint8_t* jpeg_data = (uint8_t*)malloc(file_size);
  if (!jpeg_data) { fclose(fp); return Mat(); }

  fread(jpeg_data, 1, file_size, fp);
  fclose(fp);

  // 解析 JPEG 头部获取尺寸
  int width = 0, height = 0;
  size_t i = 2;
  while (i < file_size - 1) {
    if (jpeg_data[i] == 0xFF) {
      uint8_t marker = jpeg_data[i + 1];
      if (marker == 0xC0 || marker == 0xC2) {
        if (i + 9 < file_size) {
          height = (jpeg_data[i + 5] << 8) | jpeg_data[i + 6];
          width = (jpeg_data[i + 7] << 8) | jpeg_data[i + 8];
        }
        break;
      }
      if (marker >= 0xC0 && marker <= 0xFE) {
        if (i + 3 < file_size) {
          uint16_t length = (jpeg_data[i + 2] << 8) | jpeg_data[i + 3];
          i += 2 + length;
        } else break;
      } else i++;
    } else i++;
  }

  if (width <= 0 || height <= 0) {
    free(jpeg_data);
    return Mat();
  }

  // 使用 CVI 硬件 JPEG 解码器
  CVIJpgInit();

  int channels = (flags == 0) ? 1 : 3;

  // 用 CVI_SYS_IonAlloc 分配 ION 物理连续内存（跟 TDL SDK cvi_memory_pool.cpp 一样）
  // JPU 对 Y 平面大小有 4K 对齐要求，Cb/Cr 跟在 Y 之后
  int stride_y = (width + 63) & ~63;
  size_t plane_y = (size_t)stride_y * height;
  size_t plane_y_aligned = (plane_y + 4095) & ~4095;  // 4K 对齐
  size_t total_size = plane_y_aligned * 3;  // Y + Cb + Cr
  uint64_t ion_phys = 0;
  void *ion_virt = NULL;
  CVI_U64 cvi_phys = 0;
  CVI_VOID *cvi_virt = NULL;
  if (CVI_SYS_IonAlloc_Cached(&cvi_phys, &cvi_virt, "fake_imread", total_size) != 0) {
    free(jpeg_data); return Mat();
  }
  ion_phys = (uint64_t)cvi_phys;
  ion_virt = cvi_virt;
  uint8_t *y_buf = (uint8_t *)ion_virt;
  uint8_t *cb_buf = y_buf + plane_y_aligned;
  uint8_t *cr_buf = y_buf + plane_y_aligned * 2;
  uint64_t phys_y = ion_phys;
  uint64_t phys_cb = ion_phys + plane_y_aligned;
  uint64_t phys_cr = ion_phys + plane_y_aligned * 2;

  // 配置解码器 — JPEG_MEM_EXTERNAL + 物理地址（跟官方 cvi_vdec.c 一样）
  CVIJpgConfig config;
  memset(&config, 0, sizeof(config));
  config.type = CVIJPGCOD_DEC;
  memset(&config.u.dec, 0, sizeof(CVIDecConfigParam));
  config.u.dec.rotAngle = 0;
  config.u.dec.iDataLen = file_size;
  config.u.dec.iHorScaleMode = 0;
  config.u.dec.iVerScaleMode = 0;
  config.u.dec.dst_type = JPEG_MEM_EXTERNAL;
  config.u.dec.dec_buf.packedFormat = CVI_PACKED_FORMAT_NONE;
  config.u.dec.dec_buf.chromaInterleave = CVI_CBCR_SEPARATED;
  config.u.dec.dec_buf.width = width;
  config.u.dec.dec_buf.height = height;
  config.u.dec.dec_buf.vbY.phys_addr = phys_y;
  config.u.dec.dec_buf.vbCb.phys_addr = phys_cb;
  config.u.dec.dec_buf.vbCr.phys_addr = phys_cr;

  CVIJpgHandle handle = CVIJpgOpen(config);
  if (!handle) { CVI_SYS_IonFree(cvi_phys, cvi_virt); free(jpeg_data); return Mat(); }

  if (CVIJpgSendFrameData(handle, jpeg_data, file_size, 1000) != 0) {
    CVIJpgClose(handle); CVI_SYS_IonFree(cvi_phys, cvi_virt); free(jpeg_data); return Mat();
  }

  CVIFRAMEBUF frameBuf;
  memset(&frameBuf, 0, sizeof(frameBuf));
  unsigned long hw_time = 0;
  if (CVIJpgGetFrameData(handle, &frameBuf, sizeof(frameBuf), &hw_time) != 0) {
    CVIJpgClose(handle); CVI_SYS_IonFree(cvi_phys, cvi_virt); free(jpeg_data); return Mat();
  }

  rt_kprintf("[DEBUG] JPU out: format=%d, %dx%d, strideY=%d, strideC=%d\n",
             frameBuf.format, frameBuf.width, frameBuf.height,
             frameBuf.strideY, frameBuf.strideC);
  rt_kprintf("[DEBUG] phys: Y=0x%lx Cb=0x%lx Cr=0x%lx (we passed)\n",
             (unsigned long)phys_y, (unsigned long)phys_cb, (unsigned long)phys_cr);
  rt_kprintf("[DEBUG] phys: Y=0x%lx Cb=0x%lx Cr=0x%lx (JPU returned)\n",
             (unsigned long)frameBuf.vbY.phys_addr,
             (unsigned long)frameBuf.vbCb.phys_addr,
             (unsigned long)frameBuf.vbCr.phys_addr);

  free(jpeg_data);

  // JPU 硬件写完数据后，invalidate CPU cache 才能读到最新数据
  CVI_SYS_IonInvalidateCache(cvi_phys, cvi_virt, total_size);

  // 用 ION 分配的虚拟地址读数据（frameBuf 在 EXTERNAL 模式下不填 virt_addr）
  uint8_t* y_ptr = y_buf;
  uint8_t* cb_ptr = cb_buf;
  uint8_t* cr_ptr = cr_buf;
  int sy = frameBuf.strideY;
  int sc = frameBuf.strideC;
  if (sy == 0) sy = stride_y;

  // 调试：打印 Y/Cb/Cr 平面原始值
  rt_kprintf("[DEBUG] Y[0:15]: ");
  for (int j = 0; j < 16; j++) rt_kprintf("%02x ", y_ptr[j]);
  rt_kprintf("\n[DEBUG] Cb[0:15]: ");
  for (int j = 0; j < 16; j++) rt_kprintf("%02x ", cb_ptr[j]);
  rt_kprintf("\n[DEBUG] Cr[0:15]: ");
  for (int j = 0; j < 16; j++) rt_kprintf("%02x ", cr_ptr[j]);
  rt_kprintf("\n");

  int mat_type = (channels == 1) ? CV_8UC1_ : CV_8UC3_;
  Mat mat(height, width, mat_type);

  if (channels == 1) {
    // 灰度：只取 Y 平面
    for (int y = 0; y < height; y++)
      memcpy(mat.data + y * mat.step, y_ptr + y * sy, width);
  } else {
    // 彩色：按 JPU 实际输出的 YUV 格式做 YUV→BGR 转换
    switch (frameBuf.format) {
      case CVI_FORMAT_400: {
        // 纯灰度 JPEG 但请求 3 通道 → 复制 Y 到 BGR
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            uint8_t v = y_ptr[y * sy + x];
            uint8_t* d = mat.data + (y * width + x) * 3;
            d[0] = d[1] = d[2] = v;
          }
        }
        break;
      }
      case CVI_FORMAT_420: {
        // 4:2:0 — Cb/Cr 平面尺寸 = W/2 × H/2
        int cw = (width + 1) / 2;
        int ch = (height + 1) / 2;
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            int xv = x / 2, yv = y / 2;
            int y_val = y_ptr[y * sy + x];
            int u_val = cb_ptr[yv * sc + xv] - 128;
            int v_val = cr_ptr[yv * sc + xv] - 128;
            int r = y_val + v_val + (v_val >> 2) + (v_val >> 3) + (v_val >> 5);
            int g = y_val - ((u_val >> 2) + (u_val >> 4) + (u_val >> 5))
                          - ((v_val >> 1) + (v_val >> 3) + (v_val >> 4) + (v_val >> 5));
            int b = y_val + u_val + (u_val >> 1) + (u_val >> 2) + (u_val >> 6);
            uint8_t* d = mat.data + (y * width + x) * 3;
            d[0] = (b < 0) ? 0 : ((b > 255) ? 255 : b);  // B
            d[1] = (g < 0) ? 0 : ((g > 255) ? 255 : g);  // G
            d[2] = (r < 0) ? 0 : ((r > 255) ? 255 : r);  // R
          }
        }
        (void)cw; (void)ch;
        break;
      }
      case CVI_FORMAT_422: {
        // 4:2:2 — Cb/Cr 水平下采样，W/2 × H
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            int xv = x / 2;
            int y_val = y_ptr[y * sy + x];
            int u_val = cb_ptr[y * sc + xv] - 128;
            int v_val = cr_ptr[y * sc + xv] - 128;
            int r = y_val + v_val + (v_val >> 2) + (v_val >> 3) + (v_val >> 5);
            int g = y_val - ((u_val >> 2) + (u_val >> 4) + (u_val >> 5))
                          - ((v_val >> 1) + (v_val >> 3) + (v_val >> 4) + (v_val >> 5));
            int b = y_val + u_val + (u_val >> 1) + (u_val >> 2) + (u_val >> 6);
            uint8_t* d = mat.data + (y * width + x) * 3;
            d[0] = (b < 0) ? 0 : ((b > 255) ? 255 : b);
            d[1] = (g < 0) ? 0 : ((g > 255) ? 255 : g);
            d[2] = (r < 0) ? 0 : ((r > 255) ? 255 : r);
          }
        }
        break;
      }
      case CVI_FORMAT_444: {
        // 4:4:4 — Cb/Cr 无下采样，W × H
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            int y_val = y_ptr[y * sy + x];
            int u_val = cb_ptr[y * sc + x] - 128;
            int v_val = cr_ptr[y * sc + x] - 128;
            int r = y_val + v_val + (v_val >> 2) + (v_val >> 3) + (v_val >> 5);
            int g = y_val - ((u_val >> 2) + (u_val >> 4) + (u_val >> 5))
                          - ((v_val >> 1) + (v_val >> 3) + (v_val >> 4) + (v_val >> 5));
            int b = y_val + u_val + (u_val >> 1) + (u_val >> 2) + (u_val >> 6);
            uint8_t* d = mat.data + (y * width + x) * 3;
            d[0] = (b < 0) ? 0 : ((b > 255) ? 255 : b);
            d[1] = (g < 0) ? 0 : ((g > 255) ? 255 : g);
            d[2] = (r < 0) ? 0 : ((r > 255) ? 255 : r);
          }
        }
        break;
      }
      default:
        rt_kprintf("[ERR] unsupported JPU format: %d\n", frameBuf.format);
        CVIJpgClose(handle); CVI_SYS_IonFree(cvi_phys, cvi_virt); return Mat();
    }
  }

  CVIJpgClose(handle);
  CVI_SYS_IonFree(cvi_phys, cvi_virt);

  return mat;
}

// ---- imwrite（写 BMP 文件，板端无 JPEG 编码器）----
inline bool imwrite(const std::string& filename, const Mat& img,
                    const std::vector<int>& = std::vector<int>()) {
  if (img.empty()) return false;
  int ch = img.channels();
  int w = img.cols, h = img.rows;
  int row_size = w * ch;
  // BMP 行必须 4 字节对齐
  int bmp_row_size = (row_size + 3) & ~3;
  int palette_size = 0;
  uint32_t data_size = (uint32_t)bmp_row_size * h;
  uint32_t file_size = 14 + 40 + palette_size + data_size;

  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp) return false;

  // BMP 文件头 (14 bytes)
  uint8_t fh[14] = {0};
  fh[0] = 'B'; fh[1] = 'M';
  fh[2] = file_size & 0xFF; fh[3] = (file_size >> 8) & 0xFF;
  fh[4] = (file_size >> 16) & 0xFF; fh[5] = (file_size >> 24) & 0xFF;
  // offset = 14 + 40 + palette
  uint32_t offset = 14 + 40 + palette_size;
  fh[10] = offset & 0xFF; fh[11] = (offset >> 8) & 0xFF;
  fh[12] = (offset >> 16) & 0xFF; fh[13] = (offset >> 24) & 0xFF;
  fwrite(fh, 1, 14, fp);

  // DIB 头 (40 bytes BITMAPINFOHEADER)
  uint8_t ih[40] = {0};
  ih[0] = 40;  // header size
  ih[4] = w & 0xFF; ih[5] = (w >> 8) & 0xFF; ih[6] = (w >> 16) & 0xFF; ih[7] = (w >> 24) & 0xFF;
  // 高度取正（BMP 默认 bottom-up，这里写正数表示 bottom-up）
  ih[8] = h & 0xFF; ih[9] = (h >> 8) & 0xFF; ih[10] = (h >> 16) & 0xFF; ih[11] = (h >> 24) & 0xFF;
  ih[12] = 1;  // planes
  ih[14] = (ch == 1) ? 8 : 24;  // bpp
  // 其余全 0 (BI_RGB, no compression)
  fwrite(ih, 1, 40, fp);

  // 像素数据（BMP 是 bottom-up，BGR 顺序）
  uint8_t* pad = (uint8_t*)calloc(1, bmp_row_size);
  for (int y = h - 1; y >= 0; y--) {
    uint8_t* src = img.data + y * img.step;
    if (ch == 3) {
      // BGR → BGR (BMP 也是 BGR)，直接写
      memcpy(pad, src, row_size);
    } else if (ch == 1) {
      memcpy(pad, src, row_size);
    }
    fwrite(pad, 1, bmp_row_size, fp);
  }
  free(pad);
  fclose(fp);
  return true;
}

// ---- 画图函数常量 ----
static const int LINE_AA = 16;
static const int LINE_8 = 8;
static const int FILLED = -1;
enum { FONT_HERSHEY_SIMPLEX = 0, FONT_HERSHEY_PLAIN = 1, FONT_HERSHEY_COMPLEX_SMALL = 5 };

// ---- rectangle（画矩形框，8UC3/8UC1）----
inline void rectangle(Mat& img, const Rect& rec, const Scalar& color,
                     int thickness = 1, int lineType = 8, int shift = 0) {
  (void)lineType; (void)shift;
  if (img.empty()) return;
  int x1 = rec.x, y1 = rec.y;
  int x2 = rec.x + rec.width, y2 = rec.y + rec.height;
  x1 = (x1 < 0) ? 0 : x1; y1 = (y1 < 0) ? 0 : y1;
  x2 = (x2 > img.cols) ? img.cols : x2; y2 = (y2 > img.rows) ? img.rows : y2;
  if (x1 >= x2 || y1 >= y2) return;
  int ch = img.channels();
  uint8_t c[3] = {(uint8_t)fake_sat(color.val[0], 0, 255),
                  (uint8_t)fake_sat(color.val[1], 0, 255),
                  (uint8_t)fake_sat(color.val[2], 0, 255)};
  int t = (thickness == FILLED) ? 1 : thickness;
  for (int tt = 0; tt < t; tt++) {
    int yy1 = y1 + tt, yy2 = y2 - 1 - tt;
    int xx1 = x1 + tt, xx2 = x2 - 1 - tt;
    // 上边和下边
    for (int x = xx1; x <= xx2 && x < img.cols; x++) {
      for (int c_i = 0; c_i < ch; c_i++) {
        if (yy1 < img.rows) img.data[yy1 * img.step + x * ch + c_i] = c[c_i];
        if (yy2 >= 0 && yy2 < img.rows) img.data[yy2 * img.step + x * ch + c_i] = c[c_i];
      }
    }
    // 左边和右边
    for (int y = yy1; y <= yy2 && y < img.rows; y++) {
      for (int c_i = 0; c_i < ch; c_i++) {
        if (xx1 < img.cols) img.data[y * img.step + xx1 * ch + c_i] = c[c_i];
        if (xx2 >= 0 && xx2 < img.cols) img.data[y * img.step + xx2 * ch + c_i] = c[c_i];
      }
    }
  }
}

// ---- line（画直线，8UC3/8UC1，Bresenham）----
inline void line(Mat& img, const Point& pt1, const Point& pt2,
                  const Scalar& color, int thickness = 1, int lineType = 8,
                  int shift = 0) {
  (void)lineType; (void)shift;
  if (img.empty()) return;
  int x0 = pt1.x, y0 = pt1.y, x1 = pt2.x, y1 = pt2.y;
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;
  int ch = img.channels();
  uint8_t c[3] = {(uint8_t)fake_sat(color.val[0], 0, 255),
                  (uint8_t)fake_sat(color.val[1], 0, 255),
                  (uint8_t)fake_sat(color.val[2], 0, 255)};
  while (1) {
    if (x0 >= 0 && x0 < img.cols && y0 >= 0 && y0 < img.rows) {
      for (int ci = 0; ci < ch; ci++)
        img.data[y0 * img.step + x0 * ch + ci] = c[ci];
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// ---- putText（简化版，只支持 ASCII，8UC3）----
inline void putText(Mat& img, const std::string& text, const Point& org,
                    int fontFace, double fontScale, const Scalar& color,
                    int thickness = 1, int lineType = 8, bool bottomLeftOrigin = false) {
  (void)fontFace; (void)fontScale; (void)thickness; (void)lineType; (void)bottomLeftOrigin;
  // 简化：在左上角用 rt_kprintf 不行，这里不画文字，只留接口
  // 板端没有字体渲染能力
  (void)img; (void)text; (void)org; (void)color;
}

// ---- polylines（简化，只画点连线，8UC3）----
inline void polylines(Mat& img, const std::vector<Point>& pts, bool isClosed,
                      const Scalar& color, int thickness = 1, int lineType = 8, int shift = 0) {
  (void)lineType; (void)shift;
  if (pts.size() < 2) return;
  for (size_t i = 0; i < pts.size() - 1; i++)
    line(img, pts[i], pts[i + 1], color, thickness);
  if (isClosed && pts.size() > 2)
    line(img, pts[pts.size() - 1], pts[0], color, thickness);
}

// ---- normalize（简化：min-max 归一化到 0-255）----
inline void normalize(const Mat& src, Mat& dst, double alpha, double beta,
                      int normType, int dtype) {
  (void)normType;
  if (src.depth() == FCVX_CV_32F) {
    double mn = 1e30, mx = -1e30;
    const float* sp = (const float*)src.data;
    int n = src.rows * src.cols;
    for (int i = 0; i < n; i++) { if (sp[i] < mn) mn = sp[i]; if (sp[i] > mx) mx = sp[i]; }
    dst.create(src.rows, src.cols, dtype >= 0 ? dtype : FCVX_CV_8U);
    float range = (float)(mx - mn);
    if (range < 1e-6f) range = 1;
    for (int i = 0; i < n; i++)
      dst.data[i] = (uint8_t)((sp[i] - mn) / range * (beta - alpha) + alpha);
  }
}
static const int NORM_MINMAX = 32;

// ---- 异常（grabcut 异常处理）----
struct Exception : std::exception {
  int code;
  Exception(int c = 0) : code(c) {}
  const char* what() const noexcept override { return "cv::Exception"; }
};

// ---- compare 常量 + 函数（grabcut_segment.cpp:135）----
static const int CMP_EQ = 0;
static const int CC_STAT_LEFT = 0;
static const int CC_STAT_TOP = 1;
static const int CC_STAT_WIDTH = 2;
static const int CC_STAT_HEIGHT = 3;
static const int CC_STAT_AREA = 4;
inline void compare(const Mat& src1, const Mat& src2, Mat& dst, int cmpop) {
  (void)cmpop;
  dst.create(src1.rows, src1.cols, FCVX_CV_8U);
  hwcv::compare_eq(src1.data, (int)src1.step, src2.data, (int)src2.step,
                   dst.data, (int)dst.step, src1.cols, src1.rows);
}

// ---- compare(Mat, Scalar, Mat, cmpop) —— grabcut_segment.cpp:135（GC_FGD 是 int）
inline void compare(const Mat& src1, const Scalar& s, Mat& dst, int cmpop) {
  (void)cmpop;
  dst.create(src1.rows, src1.cols, FCVX_CV_8U);
  uint8_t sv = (uint8_t)(s.val[0] + 0.5);
  hwcv::compare_scalar(src1.data, (int)src1.step, sv, dst.data, (int)dst.step,
                       src1.cols, src1.rows);
}

// ---- bitwise_or（grabcut_segment.cpp:137）----
inline void bitwise_or(const Mat& src1, const Mat& src2, Mat& dst) {
  dst.create(src1.rows, src1.cols, src1.type());
  hwcv::bitwise_or_u8(src1.data, (int)src1.step, src2.data, (int)src2.step,
                      dst.data, (int)dst.step, src1.cols, src1.rows);
}

// ---- grabCut 常量（SDK 用到）----
static const int GC_BGD = 0;
static const int GC_PR_BGD = 2;
static const int GC_FGD = 1;
static const int GC_PR_FGD = 3;
static const int GC_INIT_WITH_MASK = 0;

// ---- circle（grabcut_segment.cpp:117 用 filled/thickness=-1）----
inline void circle(Mat& img, const Point& center, int radius,
                   const Scalar& color, int thickness = 1) {
  // 板端只支持 8UC1 + filled；其他情况 no-op（SDK 仅在 grabcut 8UC1 mask 上画种子）
  if (img.depth() != FCVX_CV_8U || img.channels() != 1) return;
  if (thickness != -1) return;  // 非 filled 暂不画；SDK 仅用 filled
  uint8_t c = (uint8_t)fake_sat(color.val[0], 0, 255);
  hwcv::circle_filled_u8(img.data, (int)img.step[0], img.cols, img.rows,
                         center.x, center.y, radius, c);
}

// ---- grabCut（TODO：复杂算法，板端调用点需替换为简化实现）----
inline void grabCut(const Mat&, Mat&, const Rect&, Mat&, Mat&, int, int) {
  // TODO: 实现 GrabCut 分割
  std::abort();
}

// ---- 插值/边界常量（供 split/resize/warpAffine 使用）----
static const int INTER_NEAREST = 0;
static const int INTER_LINEAR = 1;
static const int BORDER_CONSTANT = 0;
static const int BORDER_REPLICATE = 1;
static const int BORDER_TRANSPARENT = 5;
static const int WARP_INVERSE_MAP = 16;

// ---- split（opencv_preprocessor.cpp：多通道拆分）----
// TODO: 板端需替换为真实实现或 hwcv 核
inline void split(const Mat& src, std::vector<Mat>& mv) {
  int ch = src.channels();
  mv.resize(ch);
  int single_ch_type = src.depth();  // 单通道类型 = depth 值本身
  for (int c = 0; c < ch; ++c) {
    mv[c].create(src.rows, src.cols, single_ch_type);
    // 简单拷贝第 c 通道（仅对 8UC1/8UC3 有效）
    int es = fake_elem_size(src.type_);
    for (int y = 0; y < src.rows; ++y)
      for (int x = 0; x < src.cols; ++x) {
        const uint8_t* sp = src.data + (size_t)y * src.step[0] + (size_t)x * es + c;
        uint8_t* dp = mv[c].data + (size_t)y * mv[c].step[0] + (size_t)x;
        *dp = *sp;
      }
  }
}

// ---- resize（opencv_preprocessor.cpp：图像缩放）----
// TODO: 板端需替换为真实实现或 hwcv 核
inline void resize(const Mat& src, Mat& dst, const Size& dsize,
                   double fx = 0, double fy = 0,
                   int interpolation = INTER_LINEAR) {
  (void)fx; (void)fy; (void)interpolation;
  int dst_rows = dsize.height > 0 ? dsize.height : (int)(src.rows * fy);
  int dst_cols = dsize.width > 0 ? dsize.width : (int)(src.cols * fx);
  dst.create(dst_rows, dst_cols, src.type_);
  // 最近邻简化（stub）
  for (int y = 0; y < dst_rows; ++y)
    for (int x = 0; x < dst_cols; ++x) {
      int sy = y * src.rows / dst_rows;
      int sx = x * src.cols / dst_cols;
      int es = fake_elem_size(src.type_);
      memcpy(dst.data + (size_t)y * dst.step[0] + (size_t)x * es,
             src.data + (size_t)sy * src.step[0] + (size_t)sx * es, es);
    }
}
// 重载：允许临时对象（ROI 视图）。注意：fake 实现不会写回父 Mat，仅编译期兼容
inline void resize(const Mat& src, const Mat& dst, const Size& dsize,
                   double fx = 0, double fy = 0,
                   int interpolation = INTER_LINEAR) {
  Mat tmp;
  resize(src, tmp, dsize, fx, fy, interpolation);
  const_cast<Mat&>(dst) = tmp;
}

// ---- warpAffine（双线性插值，对齐 OpenCV 语义）----
// M 是 2×3 矩阵（CV_64F 或 CV_32F），默认 flags=INTER_LINEAR（正向 src→dst，
// 内部求逆做 dst→src 采样）。支持 8UC1 / 8UC3。
inline void warpAffine(const Mat& src, Mat& dst, const Mat& M,
                       const Size& dsize, int flags = INTER_LINEAR,
                       int borderMode = BORDER_CONSTANT,
                       const Scalar& borderValue = Scalar()) {
  int dst_w = dsize.width, dst_h = dsize.height;
  dst.create(dst_h, dst_w, src.type_);
  int ch = src.channels();
  int es = src.elem_size();            // 每像素字节数
  size_t sstep = src.step[0], dstep = dst.step[0];

  // 读 M 矩阵（支持 CV_64F 和 CV_32F）
  double m[6];
  int m_es = M.elem_size();
  for (int i = 0; i < 6; i++) {
    if (m_es == 8) m[i] = *(const double*)(M.data + (size_t)(i / 3) * M.step[0] + (size_t)(i % 3) * 8);
    else           m[i] = (double)*(const float*)(M.data + (size_t)(i / 3) * M.step[0] + (size_t)(i % 3) * 4);
  }

  // 默认 M 是 src→dst 正向变换，求逆得到 dst→src 映射：src = inv_M * dst
  double a[6];  // dst→src 的 2×3 逆映射
  if (!(flags & WARP_INVERSE_MAP)) {
    double D = m[0] * m[4] - m[1] * m[3];
    D = (D != 0.0) ? 1.0 / D : 0.0;
    a[0] =  m[4] * D;
    a[1] = -m[1] * D;
    a[3] = -m[3] * D;
    a[4] =  m[0] * D;
    a[2] = -a[0] * m[2] - a[1] * m[5];
    a[5] = -a[3] * m[2] - a[4] * m[5];
  } else {
    for (int i = 0; i < 6; i++) a[i] = m[i];
  }

  bool interp_nearest = (flags & ~WARP_INVERSE_MAP) == INTER_NEAREST;

  // 边界填充值
  uint8_t bval[4] = {0};
  for (int c = 0; c < ch && c < 4; c++)
    bval[c] = (uint8_t)std::max(0.0, std::min(255.0, borderValue.val[c]));

  for (int dy = 0; dy < dst_h; dy++) {
    uint8_t* dptr = dst.data + (size_t)dy * dstep;
    for (int dx = 0; dx < dst_w; dx++) {
      double sx = a[0] * dx + a[1] * dy + a[2];
      double sy = a[3] * dx + a[4] * dy + a[5];

      if (interp_nearest) {
        int ix = (int)(sx >= 0 ? sx + 0.5 : sx - 0.5);
        int iy = (int)(sy >= 0 ? sy + 0.5 : sy - 0.5);
        if (ix >= 0 && ix < src.cols && iy >= 0 && iy < src.rows) {
          const uint8_t* sptr = src.data + (size_t)iy * sstep + (size_t)ix * es;
          memcpy(dptr, sptr, es);
        } else if (borderMode == BORDER_CONSTANT) {
          memcpy(dptr, bval, es);
        } else {  // BORDER_REPLICATE
          ix = std::max(0, std::min(src.cols - 1, ix));
          iy = std::max(0, std::min(src.rows - 1, iy));
          const uint8_t* sptr = src.data + (size_t)iy * sstep + (size_t)ix * es;
          memcpy(dptr, sptr, es);
        }
      } else {  // INTER_LINEAR
        int x0 = (int)std::floor(sx);
        int y0 = (int)std::floor(sy);
        double fx = sx - x0, fy = sy - y0;

        if (x0 >= 0 && x0 + 1 < src.cols && y0 >= 0 && y0 + 1 < src.rows) {
          // 完全在 src 内：双线性插值
          const uint8_t* s00 = src.data + (size_t)y0 * sstep + (size_t)x0 * es;
          const uint8_t* s10 = s00 + es;
          const uint8_t* s01 = s00 + sstep;
          const uint8_t* s11 = s01 + es;
          for (int c = 0; c < es; c++) {
            double v = (1 - fx) * (1 - fy) * s00[c] + fx * (1 - fy) * s10[c]
                     + (1 - fx) * fy * s01[c] + fx * fy * s11[c];
            dptr[c] = (uint8_t)(v + 0.5);
          }
        } else if (borderMode == BORDER_CONSTANT) {
          // 部分越界：逐角判断，越界角用 borderValue
          int x1 = x0 + 1, y1 = y0 + 1;
          bool v00_in = (x0 >= 0 && x0 < src.cols && y0 >= 0 && y0 < src.rows);
          bool v10_in = (x1 >= 0 && x1 < src.cols && y0 >= 0 && y0 < src.rows);
          bool v01_in = (x0 >= 0 && x0 < src.cols && y1 >= 0 && y1 < src.rows);
          bool v11_in = (x1 >= 0 && x1 < src.cols && y1 >= 0 && y1 < src.rows);
          for (int c = 0; c < es; c++) {
            double v00 = v00_in ? src.data[(size_t)y0 * sstep + (size_t)x0 * es + c] : bval[c];
            double v10 = v10_in ? src.data[(size_t)y0 * sstep + (size_t)x1 * es + c] : bval[c];
            double v01 = v01_in ? src.data[(size_t)y1 * sstep + (size_t)x0 * es + c] : bval[c];
            double v11 = v11_in ? src.data[(size_t)y1 * sstep + (size_t)x1 * es + c] : bval[c];
            double v = (1 - fx) * (1 - fy) * v00 + fx * (1 - fy) * v10
                     + (1 - fx) * fy * v01 + fx * fy * v11;
            dptr[c] = (uint8_t)(v + 0.5);
          }
        } else {  // BORDER_REPLICATE：clamp 坐标
          int x1 = x0 + 1, y1 = y0 + 1;
          x0 = std::max(0, std::min(src.cols - 1, x0));
          x1 = std::max(0, std::min(src.cols - 1, x1));
          y0 = std::max(0, std::min(src.rows - 1, y0));
          y1 = std::max(0, std::min(src.rows - 1, y1));
          const uint8_t* s00 = src.data + (size_t)y0 * sstep + (size_t)x0 * es;
          const uint8_t* s10 = src.data + (size_t)y0 * sstep + (size_t)x1 * es;
          const uint8_t* s01 = src.data + (size_t)y1 * sstep + (size_t)x0 * es;
          const uint8_t* s11 = src.data + (size_t)y1 * sstep + (size_t)x1 * es;
          for (int c = 0; c < es; c++) {
            double v = (1 - fx) * (1 - fy) * s00[c] + fx * (1 - fy) * s10[c]
                     + (1 - fx) * fy * s01[c] + fx * fy * s11[c];
            dptr[c] = (uint8_t)(v + 0.5);
          }
        }
      }
      dptr += es;
    }
  }
}

// ---- norm（TODO：板端需替换为真实实现）----
inline double norm(const Mat& src, int normType = 2 /*NORM_L2*/) {
  // TODO: 实现范数计算
  (void)src; (void)normType;
  return 0.0;
}

// ---- getTickCount（计时函数，RT-Thread 用 clock）----
inline int64_t getTickCount() {
  return (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;
}

// ---- getTickFrequency ----
inline double getTickFrequency() {
  return (double)CLOCKS_PER_SEC;
}

// ---- VideoCapture（RT-Thread 无视频采集，stub 返回失败）----
struct VideoCapture {
  VideoCapture() {}
  VideoCapture(const std::string&, int = 0) {}
  bool isOpened() const { return false; }
  bool open(const std::string&, int = 0, int = 0) { return false; }  // 3-param version
  bool read(Mat&) { return false; }
  bool operator>>(Mat&) { return false; }
  double get(int) const { return 0; }
  bool set(int, double) { return false; }
  void release() {}
};
static const int CAP_ANY = 0;
static const int CAP_PROP_FRAME_COUNT = 7;
static const int CAP_PROP_FRAME_WIDTH = 3;
static const int CAP_PROP_FRAME_HEIGHT = 4;
static const int CAP_PROP_FPS = 5;

// ---- cvFastArctan（旧版 OpenCV C API，pose_helper.cpp 用）----
// 返回角度（0-360），用 atan2 实现。注意：放在全局命名空间（旧 OpenCV C API）
}  // namespace cv temporarily closed
inline float cvFastArctan(float y, float x) {
  float angle = atan2f(y, x) * 180.0f / 3.14159265358979323846f;
  if (angle < 0) angle += 360.0f;
  return angle;
}
FAKE_CV_NS_BEGIN  // reopen

// ---- bmcv 命名空间（qwen_vl_helper.cpp 用）----
namespace bmcv {
  static const int BMCV_INTER_NEAREST = 0;
  static const int BMCV_INTER_LINEAR = 1;
  inline void resize(const Mat& src, Mat& dst, bool align = false,
                     int interpolation = BMCV_INTER_LINEAR) {
    // 转发到普通 resize
    cv::resize(src, dst, cv::Size(dst.cols, dst.rows), 0, 0, interpolation);
  }
}

}  // namespace cv — temporarily close

// 在全局命名空间暴露 BMCV 常量（SDK 代码不加 cv:: 前缀）
static const int BMCV_INTER_NEAREST = 0;
static const int BMCV_INTER_LINEAR = 1;

namespace cv {  // reopen

FAKE_CV_NS_END
