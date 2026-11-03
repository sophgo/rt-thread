// newlib_compat.h — newlib C 库兼容性层
// RT-Thread 使用 newlib，部分 C++ 标准库函数在 std 命名空间中不存在
// 此文件通过 SConscript 的 -include 自动包含在所有源文件中

#pragma once

#ifdef __cplusplus
#include <cmath>

// 将 C99 数学函数引入 std 命名空间
namespace std {
  using ::round;
  using ::ceil;
  using ::floor;
  using ::sqrt;
  using ::log1p;
  using ::exp;
  using ::log;
  using ::pow;
  using ::sin;
  using ::cos;
  using ::tan;
  using ::asin;
  using ::acos;
  using ::atan;
  using ::atan2;
  using ::abs;
  using ::fabs;
}
#endif
