// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "hungarian_algorithm.h"

namespace beatrice::common {

std::vector<size_t> hungarian_algorithm(
    const std::vector<std::vector<float>>& cost_matrix) {
  if (cost_matrix.empty() || cost_matrix[0].empty()) {
    return {};
  }

  const auto N = cost_matrix.size();
  const auto M = cost_matrix[0].size();
  std::vector<size_t> assignment(M + 1, 0);
  std::vector<float> y(N, 0.0);
  std::vector<float> w(M + 1, 0.0);
  for (size_t m = 0; m < M + 1; m++) {
    assignment[m] = N;
  }
  std::vector<float> min_to(M + 1, 0.0);
  std::vector<size_t> prv(M + 1, 0);
  std::vector<bool> in_Z(M + 1, false);

  for (size_t n = 0; n < N; n++) {
    size_t m = M;
    assignment[m] = n;
    for (size_t i = 0; i < M + 1; i++) {
      min_to[i] = FLT_MAX;
      prv[i] = N;
      in_Z[i] = false;
    }
    while (assignment[m] < N) {
      in_Z[m] = true;
      const size_t j = assignment[m];
      float delta = FLT_MAX;
      size_t next_m = M;
      for (size_t i = 0; i < M; i++) {
        if (!in_Z[i]) {
          float criteria = cost_matrix[j][i] - y[j] - w[i];
          if (criteria < min_to[i]) {
            min_to[i] = criteria;
            prv[i] = m;
          }
          if (min_to[i] < delta) {
            delta = min_to[i];
            next_m = i;
          }
        }
      }
      for (size_t i = 0; i <= M; i++) {
        if (in_Z[i]) {
          y[assignment[i]] += delta;
          w[i] -= delta;
        } else {
          min_to[i] -= delta;
        }
      }
      m = next_m;
    }
    for (size_t i = 0; m < M; m = i) {
      i = prv[m];
      assignment[m] = assignment[i];
    }
  }
  return assignment;
}
}  // namespace beatrice::common
