// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_HUNGARIAN_ALGORITHM_H_
#define BEATRICE_COMMON_HUNGARIAN_ALGORITHM_H_

#include <cfloat>
#include <vector>
namespace beatrice::common {
std::vector<size_t> hungarian_algorithm(
    const std::vector<std::vector<float>>& cost_matrix);
}  // namespace beatrice::common
#endif  // BEATRICE_COMMON_HUNGARIAN_ALGORITHM_H_