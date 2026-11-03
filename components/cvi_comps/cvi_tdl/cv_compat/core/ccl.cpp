// core/ccl.cpp — 连通域标记 + 统计（Bolelli 算法，对齐 OpenCV 4.5.5）
//
// union-find 使用 OpenCV 4.5.5 的 setRoot + set_union（含路径压缩）
// flattenL 两遍扫描（与 OpenCV 完全一致）
//
// 4 连通：Rosenfeld 掩码（Q=上, S=左）逐像素扫描
// 8 连通：4 邻域掩码（N, NW, NE, W）逐像素扫描
//
// 注意：标签编号顺序可能与 OpenCV 不同（排列等价），因为 OpenCV 使用 2×2 块处理+DAG
//       而本实现使用逐像素扫描。连通域本身（像素分组 + stats + centroids）完全一致。
#include "hwcv.h"
#include <vector>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cmath>
#include <limits>

namespace hwcv {

namespace {
  // findRoot: 沿 P 链找到根（P[root] == root 时为根，P[i] < i 时为子节点）
  inline int32_t findRoot(const int32_t* P, int32_t i) {
    int32_t root = i;
    while (P[root] < root) root = P[root];
    return root;
  }

  // setRoot: 路径压缩 —— 把 i 到根的路径上所有节点直接指向 root
  inline void setRoot(int32_t* P, int32_t i, int32_t root) {
    while (P[i] < i) {
      int32_t j = P[i];
      P[i] = root;
      i = j;
    }
    P[i] = root;
  }

  // set_union: 合并 i 和 j 所在的树，返回新根（与 OpenCV 4.5.5 完全一致）
  inline int32_t set_union(int32_t* P, int32_t i, int32_t j) {
    int32_t root = findRoot(P, i);
    if (i != j) {
      int32_t rootj = findRoot(P, j);
      if (root > rootj) root = rootj;
      setRoot(P, j, root);
    }
    setRoot(P, i, root);
    return root;
  }

  // flattenL: 与 OpenCV 4.5.5 完全一致
  // 非根 → P[i] = P[P[i]]（指向祖父，两步到位）
  // 根   → P[i] = k++（重编号为 1..n-1）
  inline int32_t flattenL(int32_t* P, int32_t length) {
    int32_t k = 1;
    for (int32_t i = 1; i < length; ++i) {
      if (P[i] < i)
        P[i] = P[P[i]];
      else
        P[i] = k++;
    }
    return k;
  }
}  // anonymous namespace

int connected_components_with_stats(const uint8_t* src, int src_step,
                                    int width, int height, int connectivity,
                                    int32_t* labels, int labels_step,
                                    int32_t* stats, int stats_step,
                                    double* centroids, int centroids_step) {
  if (!src || !labels || width <= 0 || height <= 0) return -1;

  auto get_lab = [&](int y, int x) -> int32_t& {
    return *(int32_t*)((uint8_t*)labels + (size_t)y * labels_step + (size_t)x * 4);
  };

  const int32_t Pmax = (int32_t)(((size_t)width * height + 1) / 2 + 2);
  std::vector<int32_t> P(Pmax, 0);
  P[0] = 0;
  int32_t lunique = 1;

  // ---- 第一遍扫描：标记 ----
  if (connectivity == 4) {
    // 4 连通：Rosenfeld 掩码（Q=上, S=左），Q 优先
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = src + (size_t)y * src_step;
      for (int x = 0; x < width; ++x) {
        if (!row[x]) { get_lab(y, x) = 0; continue; }
        bool q = (y > 0) && (row[-src_step + x] > 0);
        bool s = (x > 0) && (row[x - 1] > 0);
        if (q) {
          int32_t ql = get_lab(y - 1, x);
          if (s) {
            get_lab(y, x) = set_union(P.data(), ql, get_lab(y, x - 1));
          } else {
            get_lab(y, x) = ql;
          }
        } else if (s) {
          get_lab(y, x) = get_lab(y, x - 1);
        } else {
          get_lab(y, x) = lunique;
          P[lunique] = lunique;
          ++lunique;
        }
      }
    }
  } else {
    // 8 连通：4 邻域掩码（N, NW, NE, W）
    for (int y = 0; y < height; ++y) {
      const uint8_t* row = src + (size_t)y * src_step;
      for (int x = 0; x < width; ++x) {
        if (!row[x]) { get_lab(y, x) = 0; continue; }
        int32_t cands[4]; int nc = 0;
        if (y > 0)                          { int32_t l = get_lab(y-1, x);   if (l) cands[nc++] = l; }
        if (x > 0 && y > 0)                 { int32_t l = get_lab(y-1, x-1); if (l) cands[nc++] = l; }
        if (x < width - 1 && y > 0)         { int32_t l = get_lab(y-1, x+1); if (l) cands[nc++] = l; }
        if (x > 0)                          { int32_t l = get_lab(y, x-1);   if (l) cands[nc++] = l; }
        if (nc == 0) {
          get_lab(y, x) = lunique;
          P[lunique] = lunique;
          ++lunique;
        } else {
          int32_t root = findRoot(P.data(), cands[0]);
          for (int i = 1; i < nc; ++i) {
            int32_t r = findRoot(P.data(), cands[i]);
            if (r != root) {
              if (r < root) { setRoot(P.data(), root, r); root = r; }
              else          { setRoot(P.data(), r, root); }
            }
          }
          get_lab(y, x) = root;
        }
      }
    }
  }

  // ---- flattenL：压平并重编号 ----
  int32_t nLabels = flattenL(P.data(), lunique);

  // ---- 第二遍扫描：解析标签 + 累计 stats / centroids ----
  if (stats) memset(stats, 0, (size_t)stats_step * nLabels);
  if (centroids) memset(centroids, 0, (size_t)centroids_step * nLabels);
  std::vector<long> sum_x(nLabels, 0), sum_y(nLabels, 0), area(nLabels, 0);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int32_t& lab = get_lab(y, x);
      lab = P[lab];
      int l = lab;
      area[l]++;
      sum_x[l] += x; sum_y[l] += y;
      if (stats) {
        int32_t* st = (int32_t*)((uint8_t*)stats + (size_t)l * stats_step);
        if (area[l] == 1) { st[0] = x; st[1] = y; st[2] = x; st[3] = y; }
        else {
          if (x < st[0]) st[0] = x;
          if (y < st[1]) st[1] = y;
          if (x > st[2]) st[2] = x;
          if (y > st[3]) st[3] = y;
        }
      }
    }
  }

  // 最终化 stats / centroids（对齐 OpenCV 4.5.5 空组件语义）
  if (stats) {
    for (int l = 0; l < nLabels; ++l) {
      int32_t* st = (int32_t*)((uint8_t*)stats + (size_t)l * stats_step);
      if (area[l] == 0) {
        st[0] = -1;
        st[1] = INT32_MAX;
        st[2] = 0;
        st[3] = 0;
        st[4] = 0;
        continue;
      }
      st[2] = st[2] - st[0] + 1;
      st[3] = st[3] - st[1] + 1;
      st[4] = (int32_t)area[l];
    }
  }
  if (centroids) {
    for (int l = 0; l < nLabels; ++l) {
      double* ct = (double*)((uint8_t*)centroids + (size_t)l * centroids_step);
      if (area[l] == 0) {
        ct[0] = std::numeric_limits<double>::quiet_NaN();
        ct[1] = std::numeric_limits<double>::quiet_NaN();
        continue;
      }
      ct[0] = (double)sum_x[l] / area[l];
      ct[1] = (double)sum_y[l] / area[l];
    }
  }

  return nLabels;
}

}  // namespace hwcv
