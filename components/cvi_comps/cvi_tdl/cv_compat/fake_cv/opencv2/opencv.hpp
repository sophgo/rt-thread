// fake_cv/opencv2/opencv.hpp — Route A 影子总头（shadow 真实 OpenCV）
//
// 编译方式：-I fake_cv（在真实 opencv include 之前）。SDK 源 #include <opencv2/opencv.hpp>
// 或 <opencv2/core.hpp> 等全部落到这里（各子头均转发到本头，见同目录 core.hpp 等）。
//
// 分层纪律：
//   fake_mat.h    —— Mat 容器（行为由 L2 锚定，逐字段对齐真 Mat）
//   fake_funcs.h  —— cv:: 算法函数薄封装（逐个转发 hwcv 裸指针核；每加一个，对应核 L1 必须先绿）
// P1a 阶段：只放 Mat 容器 + 几何类型；算法函数在 P1b 起按 L1 绿的核逐个补。
#pragma once

#include "fake_mat.h"
#include "fake_funcs.h"  // P1b: mean/inRange/countNonZero/CCL/cvtColor 门面（核已 L1 绿）
