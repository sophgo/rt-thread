// fake_mat.h — fake_cv 的 Mat 容器（Route A 核心）
//
// 设计纪律（方案 §3.1 / L2 测试锚点）：
// 1. 惰性拷贝语义与真 Mat 一致：浅拷=共享数据+引用计数；clone()/copyTo=按行深拷（保 stride）
// 2. 外部内存构造（data+step）不拥有内存 —— release() 不 free
// 3. operator()(Rect) ROI 视图保持父 stride（不压缩！）
// 4. 逐元素访问 = data 指针算术：data + y*step + x*es
// 5. copyTo 传值接收 ROI 临时视图：尺寸匹配时 create() 无操作 → 直写父内存（真 Mat 机制）
// 6. 每条高危行为都有 L2 差分测试锚定（tests/unit/test_fake_mat.cpp，与真 OpenCV 同 TU 对拍）
//
// 命名空间开关：L2 差分测试里 #define FAKE_CV_NS fcv 后包含本头，
// fake Mat 即以 fcv::Mat 与同 TU 的真 cv::Mat 隔离对拍。
#pragma once

// 系统头必须在命名空间之外（include 进命名空间会把 std 也嵌进去）
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <new>
#include <string>

#ifdef FAKE_CV_NS
#define FAKE_CV_NS_BEGIN namespace FAKE_CV_NS {
#define FAKE_CV_NS_END }
#else
#define FAKE_CV_NS_BEGIN namespace cv {
#define FAKE_CV_NS_END }
#endif

FAKE_CV_NS_BEGIN

// ---- depth 与类型常量（编码与真 OpenCV CV_MAKETYPE 一致：type = depth + (cn-1)<<3）----
// 注意：真 OpenCV 把 CV_8U 等定义为宏。为在同 TU 与真侧共存，fake 侧全部带 FCVX_ 前缀。
enum { FCVX_CV_8U = 0, FCVX_CV_8S = 1, FCVX_CV_16U = 2, FCVX_CV_16S = 3, FCVX_CV_32S = 4, FCVX_CV_32F = 5, FCVX_CV_64F = 6 };
static const int CV_8UC1_ = FCVX_CV_8U + (1 - 1) * 8, CV_8UC2_ = FCVX_CV_8U + (2 - 1) * 8, CV_8UC3_ = FCVX_CV_8U + (3 - 1) * 8;
static const int CV_8SC1_ = FCVX_CV_8S, CV_8SC3_ = FCVX_CV_8S + 16;
static const int CV_16UC1_ = FCVX_CV_16U, CV_16SC2_ = FCVX_CV_16S + 8;
static const int CV_32SC1_ = FCVX_CV_32S, CV_32S_ = FCVX_CV_32S;
static const int CV_32FC1_ = FCVX_CV_32F, CV_32FC3_ = FCVX_CV_32F + 16;
static const int CV_64FC1_ = FCVX_CV_64F;
static const int CV_64F_ = FCVX_CV_64F;

// CV_ELEM_SIZE: 返回类型对应的每个元素字节数（与真 OpenCV 兼容）
#define CV_ELEM_SIZE(type) \
  ((((type) & 0x7) == cv::FCVX_CV_8U || ((type) & 0x7) == cv::FCVX_CV_8S) ? 1 : \
   (((type) & 0x7) == cv::FCVX_CV_16U || ((type) & 0x7) == cv::FCVX_CV_16S) ? 2 : \
   (((type) & 0x7) == cv::FCVX_CV_32S || ((type) & 0x7) == cv::FCVX_CV_32F) ? 4 : \
   (((type) & 0x7) == cv::FCVX_CV_64F) ? 8 : 1)

// ---- 基础几何类型 ----
struct Point {
  int x = 0, y = 0;
  Point() {}
  Point(int x_, int y_) : x(x_), y(y_) {}
};
struct Point2f {
  float x = 0.f, y = 0.f;
  Point2f() {}
  Point2f(float x_, float y_) : x(x_), y(y_) {}
};
struct Point3f {
  float x = 0.f, y = 0.f, z = 0.f;
  Point3f() {}
  Point3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
// Vec 类型（SDK 用到的子集）
template <typename T, int N> struct Vec {
  T val[N];
  Vec() { for (int i=0;i<N;++i) val[i]=T(); }
  Vec(const T* v) { for (int i=0;i<N;++i) val[i]=v[i]; }
  Vec(T v0, T v1) { val[0]=v0; val[1]=v1; for (int i=2;i<N;++i) val[i]=T(); }
  Vec(T v0, T v1, T v2) { val[0]=v0; val[1]=v1; val[2]=v2; for (int i=3;i<N;++i) val[i]=T(); }
  Vec(T v0, T v1, T v2, T v3) { val[0]=v0; val[1]=v1; val[2]=v2; val[3]=v3; }
  T& operator[](int i) { return val[i]; }
  const T& operator[](int i) const { return val[i]; }
};
typedef Vec<int, 4> Vec4i;
typedef Vec<float, 6> Vec6f;
typedef Vec<uint8_t, 3> Vec3b;
struct Size {
  int width = 0, height = 0;
  Size() {}
  Size(int w, int h) : width(w), height(h) {}
  bool empty() const { return width <= 0 || height <= 0; }
  bool operator==(const Size& o) const { return width == o.width && height == o.height; }
  bool operator!=(const Size& o) const { return !(*this == o); }
};
struct Rect {
  int x = 0, y = 0, width = 0, height = 0;
  Rect() {}
  Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
  Point tl() const { return Point(x, y); }
  Point br() const { return Point(x + width, y + height); }
  Size size() const { return Size(width, height); }
  bool empty() const { return width <= 0 || height <= 0; }
  bool contains(const Point& p) const {
    return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height;
  }
};
struct Scalar {
  double val[4] = {0, 0, 0, 0};
  Scalar() {}
  Scalar(double v0) { val[0] = v0; }
  Scalar(double v0, double v1) { val[0] = v0; val[1] = v1; }
  Scalar(double v0, double v1, double v2, double v3 = 0) {
    val[0] = v0; val[1] = v1; val[2] = v2; val[3] = v3;
  }
  double operator[](int i) const { return val[i]; }         // color_segment: mean[0]
  double& operator[](int i) { return val[i]; }
};

// ---- 拥有权凭据：纯引用计数 ----
struct MatData {
  uint8_t* base = nullptr;  // malloc 首地址；外部内存路径不建 MatData
  std::atomic<int> refs{1};
};

// ---- 元素读写小工具（depth 分发；saturate_cast 语义=四舍五入+饱和）----
inline int fake_round(double v) { return (int)(v >= 0 ? v + 0.5 : v - 0.5); }
inline int fake_sat(double v, int lo, int hi) {
  int i = fake_round(v);
  return i < lo ? lo : (i > hi ? hi : i);
}
inline int fake_elem_size(int type) {
  static const int dsz[] = {1, 1, 2, 2, 4, 4, 8};
  return dsz[type & 7] * (((type >> 3) & 7) + 1);
}
inline double fake_read_double(const uint8_t* p, int depth) {
  switch (depth) {
    case FCVX_CV_8U: return (double)*p;
    case FCVX_CV_8S: return (double)*(int8_t*)p;
    case FCVX_CV_16U: return (double)*(uint16_t*)p;
    case FCVX_CV_16S: return (double)*(int16_t*)p;
    case FCVX_CV_32S: return (double)*(int*)p;
    case FCVX_CV_32F: return (double)*(float*)p;
    default: return *(double*)p;
  }
}
inline void fake_write_double(uint8_t* p, int depth, double v) {
  switch (depth) {
    case FCVX_CV_8U: *p = (uint8_t)fake_sat(v, 0, 255); break;  // 饱和+四舍五入
    case FCVX_CV_8S: *(int8_t*)p = (int8_t)fake_sat(v, -128, 127); break;
    case FCVX_CV_16U: *(uint16_t*)p = (uint16_t)fake_sat(v, 0, 65535); break;
    case FCVX_CV_16S: *(int16_t*)p = (int16_t)fake_sat(v, -32768, 32767); break;
    case FCVX_CV_32S: *(int*)p = (int)fake_round(v); break;
    case FCVX_CV_32F: *(float*)p = (float)v; break;
    default: *(double*)p = v; break;
  }
}

// ---- MStep（照抄真 Mat::MStep：既能 m.step 当 size_t 用，又能 m.step[0] 下标）----
struct MStep {
  size_t p[2] = {0, 0};
  MStep() {}
  MStep(size_t v) { p[0] = v; }
  operator size_t() const { return p[0]; }
  size_t operator[](int i) const { return p[i]; }
  size_t& operator[](int i) { return p[i]; }
};

// ---- Mat 容器 ----
struct Mat {
  int rows = 0, cols = 0;
  int type_ = CV_8UC1_;
  uint8_t* data = nullptr;       // 首元素地址（ROI 时指向 ROI 左上角）
  uint8_t* datastart = nullptr;
  // 行步长（字节）——ROI/外部内存的关键。
  // 真 Mat 的 step 是 MStep（size_t 语义 + step[0] 下标双形态），照抄；
  // SDK 两种写法都有：gray_mat.step[0]（tdl_utils.cpp:492）与 m.step（门面/测试）。
  MStep step;
  MatData* holder = nullptr;     // 拥有权；外部内存/未分配时为 null
  // YUV 直通元数据（imread 读 .nv12/.yuv 时设置，供 image_factory 识别走 VPSS YUV 路径）
  bool is_yuv_ = false;          // 标记 data 携带的是 YUV 原始数据而非 BGR
  int yuv_format_ = 0;           // 0=NV12(YUV420SP_UV), 1=NV21(YUV420SP_VU), 2=I420(YUV420P_UV)

  static constexpr size_t AUTO_STEP = ~(size_t)0;

  Mat() {}
  Mat(int rows_, int cols_, int type) { create(rows_, cols_, type); }
  Mat(const Size& s, int type) { create(s.height, s.width, type); }
  // Scalar 填充构造（sot.cpp:167：cv::Mat(crop,crop,CV_8UC3,mean)——均值色画布）
  Mat(int rows_, int cols_, int type, const Scalar& s) { create(rows_, cols_, type); setTo(s); }
  // 外部内存构造：不拥有（无 holder），release() 不 free —— SDK VB/推理输出路径
  Mat(int rows_, int cols_, int type, void* data_, size_t step_ = AUTO_STEP)
      : rows(rows_), cols(cols_), type_(type), data((uint8_t*)data_),
        datastart((uint8_t*)data_) {
    step = MStep(step_ == AUTO_STEP ? (size_t)cols_ * fake_elem_size(type) : step_);
  }
  // Size 版本（meta_visualize.cpp 用 Mat(Size, type, data, step)）
  Mat(const Size& s, int type, void* data_, size_t step_ = AUTO_STEP)
      : rows(s.height), cols(s.width), type_(type), data((uint8_t*)data_),
        datastart((uint8_t*)data_) {
    step = MStep(step_ == AUTO_STEP ? (size_t)cols * fake_elem_size(type) : step_);
  }

  // 浅拷=共享（引用计数 +1），与真 Mat 一致
  Mat(const Mat& m)
      : rows(m.rows), cols(m.cols), type_(m.type_), data(m.data),
        datastart(m.datastart), step(m.step), holder(m.holder),
        is_yuv_(m.is_yuv_), yuv_format_(m.yuv_format_) {
    if (holder) holder->refs.fetch_add(1);
  }
  Mat& operator=(const Mat& m) {
    if (this != &m) {
      release();
      rows = m.rows; cols = m.cols; type_ = m.type_; data = m.data;
      datastart = m.datastart; step = m.step; holder = m.holder;
      is_yuv_ = m.is_yuv_; yuv_format_ = m.yuv_format_;
      if (holder) holder->refs.fetch_add(1);
    }
    return *this;
  }
  Mat(Mat&& m) noexcept { take(m); }
  Mat& operator=(Mat&& m) noexcept {
    if (this != &m) { release(); take(m); }
    return *this;
  }
  ~Mat() { release(); }
  void take(Mat& m) {
    rows = m.rows; cols = m.cols; type_ = m.type_; data = m.data;
    datastart = m.datastart; step = m.step; holder = m.holder;
    is_yuv_ = m.is_yuv_; yuv_format_ = m.yuv_format_;
    m.rows = m.cols = 0; m.data = m.datastart = nullptr; m.step = MStep(0); m.holder = nullptr;
    m.is_yuv_ = false; m.yuv_format_ = 0;
  }

  // ---- 查询 ----
  int depth() const { return type_ & 7; }
  int channels() const { return ((type_ >> 3) & 7) + 1; }
  int type() const { return type_; }
  bool empty() const { return data == nullptr || rows == 0 || cols == 0; }
  size_t total() const { return (size_t)rows * cols; }
  Size size() const { return Size(cols, rows); }
  bool isContinuous() const { return step[0] == (size_t)cols * fake_elem_size(type_); }
  int elem_size() const { return fake_elem_size(type_); }
  size_t elemSize() const { return (size_t)fake_elem_size(type_); }

  // ---- 分配/释放 ----
  void create(int rows_, int cols_, int type) {
    if (data && holder && rows_ == rows && cols_ == cols && type == type_) return;  // ROI 直写关键
    release();
    rows = rows_; cols = cols_; type_ = type;
    size_t minstep = (size_t)cols_ * fake_elem_size(type);
    step = MStep(minstep);  // 自有内存紧凑
    size_t sz = minstep * rows_;
    holder = new MatData();
    holder->base = (uint8_t*)malloc(sz ? sz : 1);
    if (!holder->base) { delete holder; holder = nullptr; throw std::bad_alloc(); }
    data = datastart = holder->base;
  }
  void release() {
    if (holder) {
      if (holder->refs.fetch_sub(1) == 1) {
        free(holder->base);
        delete holder;
      }
      holder = nullptr;
    }
    data = datastart = nullptr;
    rows = cols = 0; step = MStep(0); type_ = CV_8UC1_;
  }

  // ---- ROI 视图：保父 stride，借引用锁父内存 ----
  Mat operator()(const Rect& roi) const {
    Mat m;
    m.rows = roi.height; m.cols = roi.width; m.type_ = type_;
    m.step[0] = step[0];
    m.data = data + (size_t)roi.y * step[0] + (size_t)roi.x * fake_elem_size(type_);
    m.datastart = datastart;
    m.holder = holder;
    if (m.holder) m.holder->refs.fetch_add(1);
    return m;
  }
  Mat row(int y) const { return (*this)(Rect(0, y, cols, 1)); }
  Mat col(int x) const { return (*this)(Rect(x, 0, 1, rows)); }
  Mat rowRange(int a, int b) const { return (*this)(Rect(0, a, cols, b - a)); }
  Mat colRange(int a, int b) const { return (*this)(Rect(a, 0, b - a, rows)); }

  // ---- 逐元素访问（指针算术，含 stride）----
  template <typename T>
  T& at(int y, int x) {
    return *(T*)(data + (size_t)y * step[0] + (size_t)x * sizeof(T));
  }
  template <typename T>
  const T& at(int y, int x) const {
    return *(const T*)(data + (size_t)y * step[0] + (size_t)x * sizeof(T));
  }
  template <typename T>
  T& at(const Point& p) { return at<T>(p.y, p.x); }
  template <typename T>
  const T& at(const Point& p) const { return at<T>(p.y, p.x); }
  template <typename T>
  T* ptr(int y = 0) { return (T*)(data + (size_t)y * step[0]); }
  template <typename T>
  const T* ptr(int y = 0) const { return (const T*)(data + (size_t)y * step[0]); }

  // ---- 深拷 / 拷到（按行 memcpy，源侧保 stride）----
  Mat clone() const {
    Mat m;
    m.create(rows, cols, type_);
    size_t rowbytes = (size_t)cols * fake_elem_size(type_);
    for (int y = 0; y < rows; ++y)
      memcpy(m.data + (size_t)y * m.step[0], data + (size_t)y * step[0], rowbytes);
    return m;
  }
  // 传值接收（b(rect) 临时视图可绑定）。尺寸/类型匹配 → create 无操作 → 直写父内存
  void copyTo(Mat dst) const {
    if (dst.data != data || dst.datastart != datastart) {
      dst.create(rows, cols, type_);
      size_t rowbytes = (size_t)cols * fake_elem_size(type_);
      for (int y = 0; y < rows; ++y)
        memcpy(dst.data + (size_t)y * dst.step[0], data + (size_t)y * step[0], rowbytes);
    }
  }
  void copyTo(Mat dst, const Mat& mask) const;  // P2 掩码版（color_segment 未用，grabcut 用）

  void convertTo(Mat& dst, int rtype, double alpha = 1, double beta = 0) const {
    int nd = rtype & 7;
    Mat out;
    out.create(rows, cols, (rtype & ~7) | ((channels() - 1) << 3));
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          const uint8_t* sp = data + (size_t)y * step[0] + ((size_t)x * channels() + c) * fake_elem_size(type_) / channels();
          uint8_t* dp = out.data + (size_t)y * out.step[0] + ((size_t)x * channels() + c) * fake_elem_size(out.type_) / channels();
          fake_write_double(dp, nd, fake_read_double(sp, depth()) * alpha + beta);
        }
    dst = out;
  }
  // 重载：允许临时对象（ROI 视图）。注意：fake 实现不会写回父 Mat，仅编译期兼容
  void convertTo(const Mat& dst, int rtype, double alpha = 1, double beta = 0) const {
    Mat tmp;
    convertTo(tmp, rtype, alpha, beta);
    const_cast<Mat&>(dst) = tmp;
  }
  Mat reshape(int cn, int rows_ = 0) const {
    Mat m = *this;
    int new_cn = cn ? cn : channels();
    size_t total_elems = total() * channels();
    m.type_ = depth() + ((new_cn - 1) << 3);
    m.cols = rows_ ? (int)(total_elems / new_cn / rows_) : (int)(total_elems / new_cn);
    m.rows = rows_ ? rows_ : 1;
    m.step = (size_t)m.cols * fake_elem_size(m.type_);
    return m;
  }
  void setTo(const Scalar& s) {
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          int es = fake_elem_size(type_), dsz = es / channels();
          uint8_t* p = data + (size_t)y * step[0] + (size_t)x * es + c * dsz;
          fake_write_double(p, depth(), s.val[c < 4 ? c : 3]);
        }
  }

  // ---- 静态构造 ----
  static Mat zeros(int rows_, int cols_, int type) {
    Mat m;
    m.create(rows_, cols_, type);
    memset(m.data, 0, (size_t)m.step * m.rows);
    return m;
  }
  static Mat ones(int rows_, int cols_, int type) {
    Mat m;
    m.create(rows_, cols_, type);
    m.setTo(Scalar(1));
    return m;
  }
  static Mat eye(int rows_, int cols_, int type) {
    Mat m = zeros(rows_, cols_, type);
    int es = fake_elem_size(type), cn = m.channels(), dsz = es / cn;
    for (int i = 0; i < std::min(rows_, cols_); ++i) {
      uint8_t* p = m.data + (size_t)i * m.step + (size_t)i * es;
      if (dsz == 1) *p = 1;
      else if (dsz == 2) *(int16_t*)p = 1;
      else if (dsz == 4) *(int*)p = 1;
      else *(double*)p = 1;
    }
    return m;
  }

  // Mat * 标量（grabcut: Mat::ones * GC_PR_BGD）—— 逐元素乘后饱和
  Mat operator*(const Scalar& s) const {
    Mat out;
    out.create(rows, cols, type_);
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          int es = fake_elem_size(type_), dsz = es / channels();
          const uint8_t* sp = data + (size_t)y * step[0] + (size_t)x * es + c * dsz;
          uint8_t* dp = out.data + (size_t)y * out.step[0] + (size_t)x * es + c * dsz;
          fake_write_double(dp, depth(), fake_read_double(sp, depth()) * s.val[c < 4 ? c : 3]);
        }
    return out;
  }

  // ---- Mat 代数（occlusion_detect:46 `laplacian - 2 * laplacianAbs`，64FC1）----
  // 真 OpenCV：标量*Mat 与 Mat-Mat 全类型逐元素；此处实现当前 SDK 需要的路径，
  // 深度组合不足时 create 输出后逐元素 fake_read/write（天然覆盖 8U/32S/64F）
  Mat operator*(double k) const {
    Mat out;
    out.create(rows, cols, type_);
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          int es = fake_elem_size(type_), dsz = es / channels();
          const uint8_t* sp = data + (size_t)y * step[0] + (size_t)x * es + c * dsz;
          uint8_t* dp = out.data + (size_t)y * out.step[0] + (size_t)x * es + c * dsz;
          fake_write_double(dp, depth(), fake_read_double(sp, depth()) * k);
        }
    return out;
  }
  friend Mat operator*(double k, const Mat& a) { return a * k; }
  friend Mat operator*(int k, const Mat& a) { return a * (double)k; }
  Mat operator-(const Mat& b) const {
    Mat out;
    out.create(rows, cols, type_);
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          int es = fake_elem_size(type_), dsz = es / channels();
          const uint8_t* sp = data + (size_t)y * step[0] + (size_t)x * es + c * dsz;
          const uint8_t* bp = b.data + (size_t)y * b.step[0] + (size_t)x * es + c * dsz;
          uint8_t* dp = out.data + (size_t)y * out.step[0] + (size_t)x * es + c * dsz;
          fake_write_double(dp, depth(),
                            fake_read_double(sp, depth()) - fake_read_double(bp, b.depth()));
        }
    return out;
  }

  // ---- transpose（image_alignment.cpp:754 `trans.t()`）----
  Mat t() const {
    Mat out;
    out.create(cols, rows, type_);
    int es = fake_elem_size(type_);
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        memcpy(out.data + (size_t)x * out.step[0] + (size_t)y * es,
               data + (size_t)y * step[0] + (size_t)x * es, es);
    return out;
  }

  // ---- operator*= （image_alignment.cpp:771 `A *= -1`）----
  Mat& operator*=(int v) { return (*this *= (double)v); }
  Mat& operator*=(double v) {
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        for (int c = 0; c < channels(); ++c) {
          int es = fake_elem_size(type_), dsz = es / channels();
          uint8_t* p = data + (size_t)y * step[0] + (size_t)x * es + c * dsz;
          fake_write_double(p, depth(), fake_read_double(p, depth()) * v);
        }
    return *this;
  }

  // ---- 1D at<int>（image_alignment.cpp:778 `x.at<double>(i)`）----
  template <typename T>
  T& at(int i) { return *(T*)(data + (size_t)i * fake_elem_size(type_)); }
  template <typename T>
  const T& at(int i) const { return *(const T*)(data + (size_t)i * fake_elem_size(type_)); }

  // ---- Mat * Mat（矩阵乘法，image_alignment.cpp:754 `uv_h * trans.t()`）----
  // 仅支持 64F 单通道（SDK 里 matrix alignment 用的是 CV_64F）
  Mat operator*(const Mat& b) const {
    Mat out;
    out.create(rows, b.cols, FCVX_CV_64F);
    int es = (int)sizeof(double);
    for (int i = 0; i < rows; ++i)
      for (int j = 0; j < b.cols; ++j) {
        double sum = 0;
        for (int k = 0; k < cols; ++k) {
          double va = *(const double*)(data + (size_t)i * step[0] + (size_t)k * es);
          double vb = *(const double*)(b.data + (size_t)k * b.step[0] + (size_t)j * es);
          sum += va * vb;
        }
        *(double*)(out.data + (size_t)i * out.step[0] + (size_t)j * es) = sum;
      }
    return out;
  }
};

// ---- Mat_ 模板（(Mat_<double>(3,3) << 9 个数)，image_alignment.cpp:778）----
template <typename T>
struct Mat_ : Mat {
  Mat_() {}
  Mat_(int rows_, int cols_) : Mat(rows_, cols_, Mat_::fake_typeof()) {}
  static int fake_typeof();
  struct Inserter {
    Mat_ m;  // 持有临时对象的拷（共享 holder，数据存活到表达式结束）
    int i = 0;
    Inserter(const Mat_& m_, int i_) : m(m_), i(i_) {}
    Inserter operator,(T v) const {
      Inserter n(*this);
      ((T*)m.data)[i] = v;
      n.i = i + 1;
      return n;
    }
    operator Mat_() const { return m; }
  };
};
// 注意：operator<< 按 Mat 值传递（临时对象可绑定），写入后由外层 Mat 赋值接走数据
template <typename T>
typename Mat_<T>::Inserter operator<<(Mat_<T> m, T v) {
  ((T*)m.data)[0] = v;
  return typename Mat_<T>::Inserter(m, 1);
}
template <> inline int Mat_<double>::fake_typeof() { return FCVX_CV_64F; }
template <> inline int Mat_<float>::fake_typeof() { return FCVX_CV_32F; }
template <> inline int Mat_<uint8_t>::fake_typeof() { return FCVX_CV_8U; }
template <> inline int Mat_<int>::fake_typeof() { return FCVX_CV_32S; }

FAKE_CV_NS_END

// 类型字面量宏（SDK 直接写 CV_8UC3 等）——仅当真 OpenCV 尚未定义时（L2 双侧同 TU 时先 undef）
#ifndef CV_8UC1
#define CV_8UC1 (cv::CV_8UC1_)
#define CV_8UC2 (cv::CV_8UC2_)
#define CV_8UC3 (cv::CV_8UC3_)
#define CV_8SC1 (cv::CV_8SC1_)
#define CV_8SC3 (cv::CV_8SC3_)
#define CV_16UC1 (cv::CV_16UC1_)
#define CV_16SC2 (cv::CV_16SC2_)
#define CV_32SC1 (cv::CV_32SC1_)
#define CV_32S (cv::CV_32S_)
#define CV_32FC1 (cv::CV_32FC1_)
#define CV_32FC3 (cv::CV_32FC3_)
#define CV_64FC1 (cv::CV_64FC1_)
#define CV_64F (cv::CV_64F_)
#endif
