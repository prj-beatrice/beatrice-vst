// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_SPHERICAL_AVERAGE_H_
#define BEATRICE_COMMON_SPHERICAL_AVERAGE_H_

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

/**
 * This class implements spherical averages
 * (https://mathweb.ucsd.edu/~sbuss/ResearchWeb/spheremean/index.html), which
 * correspond to spherical linear interpolation between two or more vectors.
 *
 * Note: If the number of points is greater than the number of features
 * (under-determined system), the solution of the weights will not be unique and
 * will be unstable.
 * To make this class usable in such cases, it is necessary to combine it with
 * some least-squares solution solver (e.g. Eigen) to obtain the weights as
 * a minimum-norm solution, but this is not currently the case.
 */

namespace beatrice::common {

template <typename T>
class SphericalAverage {
 public:
  SphericalAverage()
      : N_(0),
        M_(0),
        converged_(false),
        w_(),
        p_(),
        q_(),
        v_(),
        r_(),
        u_(),
        s_() {}

  SphericalAverage(size_t num_point, size_t num_feature,
                   const T* unnormalized_vectors)
      : N_(num_point),
        M_(num_feature),
        converged_(false),
        w_(),
        p_(),
        q_(),
        v_(),
        r_(),
        u_(),
        s_() {
    Initialize(N_, M_, unnormalized_vectors);
  }
  ~SphericalAverage() = default;

  void Initialize(size_t num_point, size_t num_feature,
                  const T* unnormalized_vectors) {
    assert(num_point <= num_feature);
    N_ = num_point;
    M_ = num_feature;
    w_.resize(N_, static_cast<T>(0.0));       // size = N_
    p_.resize(N_ * M_, static_cast<T>(0.0));  // size = N_ * M_
    q_.resize(M_, static_cast<T>(0.0));       // size = M_

    v_.resize(N_, static_cast<T>(0.0));       // size = N_
    r_.resize(N_ * M_, static_cast<T>(0.0));  // size = N_ * M_
    u_.resize(M_, static_cast<T>(0.0));       // size = M_

    s_.resize(N_, static_cast<T>(0.0));  // size = N_

    std::copy_n(unnormalized_vectors, N_ * M_, p_.begin());
    for (size_t n = 0; n < N_; ++n) {
      NormalizeVector(M_, &p_[n * M_]);
    }
  }
  void SetWeights(size_t num_point, const T* weights) {
    assert(N_ == num_point);
    converged_ = false;
    std::copy_n(weights, N_, w_.begin());
    if (NormalizeWeight(N_, w_.data())) {
      WeightedSum(w_.data(), p_.data(), q_.data());
      if (!NormalizeVector(M_, q_.data())) {
        converged_ = true;
      }
    } else {
      converged_ = true;
    }
    if (converged_) {
      std::fill_n(v_.begin(), N_, static_cast<T>(0.0));
    } else {
      UpdateRV();
    }
  }

  auto Update() -> bool {
    if (converged_) {
      return true;
    }
    // Update u_
    WeightedSum(w_.data(), r_.data(), u_.data());
    T norm_u = sqrt(Dot(M_, u_.data(), u_.data()));
    if (norm_u >= std::numeric_limits<T>::epsilon()) {
      UpdateQ(norm_u);
      UpdateRV();
    } else {
      converged_ = true;
    }
    return converged_;
  }

  void ApplyWeights(size_t num_point, size_t num_feature, const T* src_vectors,
                    T* dst_vector) {
    assert(N_ == num_point && M_ == num_feature);
    WeightedSum(v_.data(), src_vectors, dst_vector);
  }

 private:
  auto Dot(size_t len, const T* x1, const T* x2) -> T {
    T y = static_cast<T>(0.0);
    for (size_t l = 0; l < len; ++l) {
      y += x1[l] * x2[l];
    }
    return y;
  }

  auto NormalizeVector(size_t len, T* x) -> bool {
    T norm = sqrt(Dot(len, x, x));
    if (norm > 0.0) {
      T scale_factor = (static_cast<T>(1.0)) / norm;
      for (size_t l = 0; l < len; ++l) {
        x[l] *= scale_factor;
      }
      return true;
    } else {
      // std::fill_n( x, len, static_cast<T>(0.0) );
      return false;
    }
  }

  auto NormalizeWeight(size_t len, T* x) -> bool {
    T sum = 0.0;
    for (size_t l = 0; l < len; ++l) {
      sum += x[l];
    }
    if (sum > 0.0) {
      T scale_factor = (static_cast<T>(1.0)) / sum;
      for (size_t l = 0; l < len; ++l) {
        x[l] *= scale_factor;
      }
      return true;
    } else {
      // std::fill_n( x, len, static_cast<T>(0.0) );
      return false;
    }
  }

  void WeightedSum(const T* weights, const T* x, T* y) {
    std::fill_n(y, M_, static_cast<T>(0.0));
    for (size_t n = 0; n < N_; ++n) {
      for (size_t m = 0; m < M_; ++m) {
        y[m] += weights[n] * x[n * M_ + m];
      }
    }
  }

  auto Sinc(T x) -> T {
    static const T kThreshold0 = std::numeric_limits<T>::epsilon();
    static const T kThreshold1 = sqrt(kThreshold0);
    static const T kThreshold2 = sqrt(kThreshold1);
    T y = static_cast<T>(0.0);
    T abs_x = abs(x);
    if (abs_x >= kThreshold2) {
      y = sin(x) / x;
    } else {
      y = static_cast<T>(1.0);
      if (abs_x >= kThreshold0) {
        T x2 = x * x;
        y -= x2 / static_cast<T>(6.0);
        if (abs_x >= kThreshold1) {
          y += x2 * x2 / static_cast<T>(120.0);
        }
      }
    }
    return y;
  }

  void UpdateRV() {
    T sum_w_c_s = 0.0;
    for (size_t n = 0; n < N_; ++n) {
      T cos_th = Dot(M_, &p_[n * M_], q_.data());
      s_[n] = (static_cast<T>(1.0)) /
              (Sinc(acos(cos_th)) + std::numeric_limits<T>::epsilon());

      T c_s = cos_th * s_[n];
      sum_w_c_s += w_[n] * c_s;

      for (size_t m = 0; m < M_; ++m) {
        r_[n * M_ + m] = p_[n * M_ + m] * s_[n] - q_[m] * c_s;
      }
    }
    T inv_sum_w_c_s =
        (static_cast<T>(1.0)) / (sum_w_c_s + std::numeric_limits<T>::epsilon());
    for (size_t n = 0; n < N_; ++n) {
      v_[n] = w_[n] * s_[n] * inv_sum_w_c_s;
    }
  }

  void UpdateQ(T phi) {
    T alpha = cos(phi);
    T beta = Sinc(phi);
    for (size_t m = 0; m < M_; ++m) {
      q_[m] = q_[m] * alpha + u_[m] * beta;
    }
  }

  size_t N_;
  size_t M_;
  bool converged_;

  // vectors in original space
  std::vector<T> w_;  // size = N_
  std::vector<T> p_;  // size = N_ * M_
  std::vector<T> q_;  // size = M_

  // vectors in tangent plane
  std::vector<T> v_;  // size = N_
  std::vector<T> r_;  // size = N_ * M_
  std::vector<T> u_;  // size = M_

  // working memory
  std::vector<T> s_;  // size = N_
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_SPHERICAL_AVERAGE_H_
