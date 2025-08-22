// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_SPHERICAL_AVERAGE_H_
#define BEATRICE_COMMON_SPHERICAL_AVERAGE_H_

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>
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
// custom allocator for aligned vectors.
template <typename T, std::size_t N>
class AlignedAllocator {
 public:
  using value_type = T;

  AlignedAllocator() noexcept = default;

  template <typename U>
  AlignedAllocator(const AlignedAllocator<U, N>&) noexcept {}

  T* allocate(std::size_t n) {
    if (n == 0) {
      return nullptr;
    }
    // calculate memory size
    // N must be a multiple of the size of T
    std::size_t size = n * sizeof(T);

    // allocate aligned memory at N-byte boundaries
    // note that size must be a multiple of N
    void* ptr = _aligned_malloc(size, N);
    // throw an exception if memory allocation fails
    if (ptr == nullptr) {
      throw std::bad_alloc();
    }
    return static_cast<T*>(ptr);
  }

  void deallocate(T* ptr, std::size_t) noexcept { _aligned_free(ptr); }

  template <class U>
  struct rebind {
    //! allocator type for rebinding
    using other = AlignedAllocator<U, N>;
  };
};

template <typename T, typename U, std::size_t N, std::size_t M>
bool operator==(const AlignedAllocator<T, N>&,
                const AlignedAllocator<U, M>&) noexcept {
  return (N == M);
}

template <typename T, typename U, std::size_t N, std::size_t M>
bool operator!=(const AlignedAllocator<T, N>& lhs,
                const AlignedAllocator<U, M>& rhs) noexcept {
  return !(lhs == rhs);
}

// vector class for allocating aligned memory
template <typename T, std::size_t N>
using AlignedVector = std::vector<T, AlignedAllocator<T, N>>;

template <typename T>
class SphericalAverage {
 public:
  SphericalAverage()
      : N_all_(0),
        N_lim_(0),
        N_(0),
        M_(0),
        K_(0),
        converged_(true),
        indices_(),
        w_(),
        p_(),
        p_all_(),
        p_raw_(),
        q_(),
        v_(),
        g_(),
        mem_idx_(0),
        gamma_(0),
        d_(),
        s_(),
        t_(),
        r_(),
        a_() {}

  SphericalAverage(size_t num_point_all, size_t num_feature,
                   const T* unnormalized_vectors, size_t num_point_limit = 0,
                   size_t num_memory = 2)
      : N_all_(num_point_all),
        N_lim_(0),
        N_(0),
        M_(num_feature),
        K_(num_memory),
        converged_(true),
        indices_(),
        w_(),
        p_(),
        p_all_(),
        p_raw_(),
        q_(),
        v_(),
        g_(),
        mem_idx_(0),
        gamma_(0),
        d_(),
        s_(),
        t_(),
        r_(),
        a_() {
    Initialize(num_point_all, num_feature, unnormalized_vectors,
               num_point_limit, num_memory);
  }

  ~SphericalAverage() = default;

  auto Initialize(size_t num_point_all, size_t num_feature,
                  const T* unnormalized_vectors, size_t num_point_limit = 0,
                  size_t num_memory = 2) -> void {
    N_all_ = num_point_all;
    if (num_point_limit == 0 || num_point_limit > num_point_all) {
      N_lim_ = num_point_all;
    } else {
      N_lim_ = num_point_limit;
    }

    assert(N_lim_ <= num_feature);

    N_ = 0;
    M_ = num_feature;
    K_ = num_memory;
    indices_.resize(N_lim_, 0);          // size = N_lim_
    w_.resize(N_lim_, (T)0.0);           // size = N_lim_
    p_.resize(N_lim_ * M_, (T)0.0);      // size = N_lim_ * M
    p_all_.resize(N_all_ * M_, (T)0.0);  // size = N_all_ * M
    p_raw_.resize(N_all_ * M_, (T)0.0);  // size = N_all_ * M
    q_.resize(M_, (T)0.0);               // size = M
    v_.resize(N_lim_, (T)0.0);           // size = N_lim_
    g_.resize(M_, (T)0.0);               // size = M
    d_.resize(M_, (T)0.0);               // size = M
    s_.resize(K_ * M_, (T)0.0);          // size = K*M
    t_.resize(K_ * M_, (T)0.0);          // size = K*M
    r_.resize(K_, (T)0.0);               // size = K
    a_.resize(K_, (T)0.0);               // size = K

    std::copy_n(unnormalized_vectors, N_all_ * M_, p_raw_.begin());
    std::copy_n(unnormalized_vectors, N_all_ * M_, p_all_.begin());
    for (size_t n = 0; n < N_all_; n++) {
      NormalizeVector(M_, &p_all_[n * M_]);
    }
  }

  auto SetWeights(size_t num_point, const T* weights,
                  const int* indices = nullptr) -> void {
    converged_ = false;

    std::fill_n(w_.begin(), N_lim_, (T)0.0);
    std::fill_n(v_.begin(), N_lim_, (T)0.0);
    std::fill_n(p_.begin(), N_lim_ * M_, (T)0.0);

    if (indices) {
      N_ = std::min(num_point, N_lim_);
      std::copy_n(indices, N_, indices_.begin());
      for (size_t i = 0; i < N_; i++) {
        w_[i] = weights[indices_[i]];
        std::copy_n(&p_all_[indices_[i] * M_], M_, &p_[i * M_]);
      }
    } else {
      N_ = 0;
      for (size_t i = 0; i < num_point; i++) {
        if (weights[i] > (T)0.0) {
          indices_[N_] = i;
          w_[N_] = weights[i];
          std::copy_n(&p_all_[i * M_], M_, &p_[N_ * M_]);
          N_++;
          if (N_ >= N_lim_) {
            break;
          }
        }
      }
    }

    if (N_ > 0 && NormalizeWeight(N_, w_.data())) {
      std::fill_n(q_.data(), M_, (T)0.0);
      for (size_t n = 0; n < N_; n++) {
        AddProductC(M_, w_[n], &p_[n * M_], q_.data());
      }
      if (!NormalizeVector(M_, q_.data())) {
        converged_ = true;
      }
    } else {
      converged_ = true;
    }

    if (converged_) {
      std::fill_n(v_.begin(), N_, (T)0.0);
    } else {
      mem_idx_ = 0;
      gamma_ = 1.0;
      std::fill_n(s_.begin(), K_ * M_, (T)0.0);
      std::fill_n(t_.begin(), K_ * M_, (T)0.0);
      std::fill_n(r_.begin(), K_, (T)0.0);
      std::fill_n(a_.begin(), K_, (T)0.0);
      std::fill_n(g_.begin(), M_, (T)0.0);
      UpdateVGD();
    }
  }

  auto Update() -> bool {
    if (converged_) {
      return true;
    }
    T norm_d = sqrt(Dot(M_, d_.data(), d_.data()));
    if (norm_d >= 8 * std::numeric_limits<T>::epsilon()) {
      UpdateQS();
      UpdateVGDT();
      UpdateGammaR();
    } else {
      converged_ = true;
    }
    return converged_;
  }

  auto GetResult(size_t num_feature, T* dst_vector) -> void {
    assert(M_ == num_feature);
    std::fill_n(dst_vector, M_, (T)0.0);
    for (size_t n = 0; n < N_; n++) {
      AddProductC(M_, v_[n], &p_raw_[indices_[n] * M_], dst_vector);
    }
  }

 private:
  auto Dot(size_t len, const T* x1, const T* x2) -> T {
    const T* xx1 = std::assume_aligned<64>(x1);
    const T* xx2 = std::assume_aligned<64>(x2);
    T y = (T)0;
    for (size_t l = 0; l < len; l++) {
      y += xx1[l] * xx2[l];
    }
    return y;
  }

  auto MulC(size_t len, T a, T* x) -> void {
    T* xx = std::assume_aligned<64>(x);
    for (size_t l = 0; l < len; l++) {
      xx[l] *= a;
    }
  }

  auto AddProductC(size_t len, T a, const T* __restrict x, T* __restrict y)
      -> void {
    const T* xx = std::assume_aligned<64>(x);
    T* yy = std::assume_aligned<64>(y);
    for (size_t l = 0; l < len; l++) {
      yy[l] += a * xx[l];
    }
  }

  auto Sum(size_t len, const T* __restrict x) -> T {
    const T* xx = std::assume_aligned<64>(x);
    T y = 0;
    for (size_t l = 0; l < len; l++) {
      y += xx[l];
    }
    return y;
  }

  auto NormalizeVector(size_t len, T* x) -> bool {
    const T* xx = std::assume_aligned<64>(x);
    T norm = sqrt(Dot(len, x, x));
    if (norm > 0.0) {
      T scale_factor = ((T)1.0) / norm;
      MulC(len, scale_factor, x);
      return true;
    } else {
      // std::fill_n( x, len, (T)0.0 );
      return false;
    }
  }

  auto NormalizeWeight(size_t len, T* x) -> bool {
    T sum_x = Sum(len, x);
    if (sum_x > 0.0) {
      T scale_factor = ((T)1.0) / sum_x;
      MulC(len, scale_factor, x);
      return true;
    } else {
      // std::fill_n( x, len, (T)0.0 );
      return false;
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

  auto ProjectVectorToPlane(size_t len, const T* __restrict x, T* __restrict y)
      -> void {
    T minus_inner_product = -Dot(len, x, y);
    AddProductC(len, minus_inner_product, x, y);
  }

  auto UpdateVGD(void) -> void {
    T sum_w_c_s = (T)0.0;
    std::fill_n(g_.begin(), M_, (T)0.0);

    for (size_t n = 0; n < N_; n++) {
      T cos_th = Dot(M_, &p_[n * M_], q_.data());
      T theta = acos(cos_th);
      T inv_sinc_th =
          ((T)1.0) / (Sinc(theta) + std::numeric_limits<T>::epsilon());
      sum_w_c_s += w_[n] * cos_th * inv_sinc_th;

      v_[n] = w_[n] * inv_sinc_th;

      T a_n = -((T)2.0) * w_[n] * theta / sqrt(((T)1.0) - cos_th * cos_th);
      AddProductC(M_, a_n, &p_[n * M_], g_.data());
    }

    T inv_sum_w_c_s =
        ((T)1.0) / (sum_w_c_s + std::numeric_limits<T>::epsilon());
    MulC(N_, inv_sum_w_c_s, v_.data());

    ProjectVectorToPlane(M_, q_.data(), g_.data());

    std::copy(g_.begin(), g_.end(), d_.begin());
    for (size_t k = 0; k < K_; k++) {
      size_t idx = (mem_idx_ - k - 1 + K_) % K_;
      a_[idx] = r_[idx] * Dot(M_, &s_[idx * M_], d_.data());
      AddProductC(M_, -a_[idx], &t_[idx * M_], d_.data());
    }
    MulC(M_, gamma_, d_.data());
    for (size_t k = 0; k < K_; k++) {
      size_t idx = (mem_idx_ + k) % K_;
      T b = r_[idx] * Dot(M_, &t_[idx * M_], d_.data());
      AddProductC(M_, (a_[idx] - b), &s_[idx * M_], d_.data());
    }
  }

  void UpdateVGDT(void) {
    std::copy(g_.begin(), g_.end(), &t_[mem_idx_ * M_]);

    UpdateVGD();

    T* __restrict tt = std::assume_aligned<64>(&t_[mem_idx_ * M_]);
    const T* __restrict gg = std::assume_aligned<64>(g_.data());
    for (size_t m = 0; m < M_; m++) {
      tt[m] = gg[m] - tt[m];
    }
    ProjectVectorToPlane(M_, q_.data(), &t_[mem_idx_ * M_]);
  }

  void UpdateQS(void) {
    std::copy(q_.begin(), q_.end(), &s_[mem_idx_ * M_]);

    T* __restrict qq = std::assume_aligned<64>(q_.data());
    const T* __restrict dd = std::assume_aligned<64>(d_.data());
    for (size_t m = 0; m < M_; m++) {
      qq[m] -= dd[m];
    }
    NormalizeVector(M_, q_.data());

    T* __restrict ss = std::assume_aligned<64>(&s_[mem_idx_ * M_]);
    for (size_t m = 0; m < M_; m++) {
      ss[m] = qq[m] - ss[m];
    }
  }

  void UpdateGammaR(void) {
    gamma_ = Dot(M_, &s_[mem_idx_ * M_], &t_[mem_idx_ * M_]);
    r_[mem_idx_] = ((T)1.0) / gamma_;
    gamma_ /= Dot(M_, &t_[mem_idx_ * M_], &t_[mem_idx_ * M_]);
    mem_idx_ += 1;
    if (mem_idx_ >= K_) {
      mem_idx_ = 0;
    }
  }

  size_t N_all_;
  size_t N_lim_;
  size_t N_;
  size_t M_;
  size_t K_;

  bool converged_;

  // vectors in original space
  std::vector<size_t> indices_;  // size = N_lim
  AlignedVector<T, 64> w_;       // size = N_lim
  AlignedVector<T, 64> p_;       // size = N_lim * M
  AlignedVector<T, 64> p_all_;   // size = N_all * M
  AlignedVector<T, 64> p_raw_;   // size = N_all * M
  AlignedVector<T, 64> q_;       // size = M
  AlignedVector<T, 64> v_;       // size = N_lim
  AlignedVector<T, 64> g_;       // size = M

  size_t mem_idx_;
  T gamma_;
  AlignedVector<T, 64> d_;  // size = M
  AlignedVector<T, 64> s_;  // size = K * M
  AlignedVector<T, 64> t_;  // size = K * M
  AlignedVector<T, 64> r_;  // size = K
  AlignedVector<T, 64> a_;  // size = K
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_SPHERICAL_AVERAGE_H_
