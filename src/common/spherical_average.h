#ifndef SPHERICAL_AVERAGE_H_
#define SPHERICAL_AVERAGE_H_

#define _USE_MATH_DEFINES 
#include <cmath>
#include <cstddef>
#include <vector>
#include <memory>
#include <algorithm>
#include <limits>
#include <cassert>

/**
 * This class implements spherical averages (https://mathweb.ucsd.edu/~sbuss/ResearchWeb/spheremean/index.html),
 *  which correspond to spherical linear interpolation between two or more vectors.
 */

namespace beatrice::common {

template<typename T>
class SphericalAverage{
 public:
  SphericalAverage( )
    : N( 0 ), M( 0 ), converged(false)
    , w(), p(), q(), v(), r(), u(), s()
  {}

  SphericalAverage( size_t num_point, size_t num_feature, const T* unnormalized_vectors )
    : N( num_point ), M( num_feature ), converged(false)
    , w(), p(), q(), v(), r(), u(), s()
  {
    initialize( N, M, unnormalized_vectors );
  }
  ~SphericalAverage() = default;

  void initialize( size_t num_point, size_t num_feature, const T* unnormalized_vectors ){
    N = num_point;
    M = num_feature;
    w.resize(N, (T)0.0);   // size = N
    p.resize(N*M, (T)0.0); // size = N * M
    q.resize(M, (T)0.0);   // size = M
  
    v.resize(N, (T)0.0);   // size = N
    r.resize(N*M, (T)0.0); // size = N * M
    u.resize(M, (T)0.0);   // size = M

    s.resize(N, (T)0.0);   // size = N

    std::copy_n(
      unnormalized_vectors, N * M, p.begin()
    );
    for (size_t n = 0; n < N; n++){
      normalize_vector( M, &p[ n * M ]);
    }
  }
  void set_weights( size_t num_point, T* weights ){
    assert( N == num_point );
    converged = false;
    std::copy_n( weights, N, w.begin() );
    if( normalize_weight( N, w.data() ) ){
      weighted_sum( w.data(), p.data(), q.data() );
      if( !normalize_vector( M, q.data() ) ){
        converged = true;
      }
    }else{
      converged = true;
    }
    if( converged ){
      std::fill_n( v.begin(), N, (T)0.0);
    }else{
      update_r_v();
    }
  }

  bool update( void ){
    if( converged ){
      return true;
    }
    // update u
    weighted_sum( w.data(), r.data(), u.data() );
    T norm_u = sqrt( dot( M, u.data(), u.data() ) );
    if( norm_u >= std::numeric_limits<T>::epsilon() ){
      update_q( norm_u );
      update_r_v();
    }else{
      converged = true;
    }
    return converged;
  }

  const std::vector<T>& get_weights( void ){
    return v;
  
  }

  void apply_weights(size_t num_point, size_t num_feature, const T* src_vectors, T* dst_vector ){
    assert( N==num_point && M==num_feature );
    weighted_sum( v.data(), src_vectors, dst_vector );
  }

private:

  T dot( size_t len, const T* x1, const T* x2){
    T y = (T)0;
    for( size_t l = 0; l < len; l++ ){
      y += x1[l] * x2[l];
    }
    return y;
  }

  bool normalize_vector( size_t len, T* x ){
    T norm = sqrt( dot( len, x, x ) );
    if( norm > 0.0 ){
      T scale_factor = ((T)1.0) / norm;
      for( size_t l = 0; l < len; l++ ){
        x[l] *= scale_factor;
      }
      return true;
    }else{
      //std::fill_n( x, len, (T)0.0 );
      return false;
    }
  }

  bool normalize_weight( size_t len, T* x ){
    T sum = 0.0;
    for( size_t l = 0; l < len; l++ ){
      sum += x[l];
    }
    if( sum > 0.0 ){
      T scale_factor = ((T)1.0) / sum;
      for( size_t l = 0; l < len; l++ ){
        x[l] *= scale_factor;
      }
      return true;
    }else{
      //std::fill_n( x, len, (T)0.0 );
      return false;
    }
  }

  void weighted_sum( const T* weights, const T* x, T* y ){
    std::fill_n( y, M, (T)0.0 );
    for( size_t n = 0; n < N; n++ ){
      for( size_t m = 0; m < M; m++ ){
        y[m] += weights[n] * x[ n * M + m ];
      }
    }
  }

  T sinc( T x ){
    static const T threshold_0 = std::numeric_limits<T>::epsilon();
    static const T threshold_1 = sqrt( threshold_0 );
    static const T threshold_2 = sqrt( threshold_1 );
    T y = (T)0.0;
    T abs_x = abs( x );
    if( abs_x >= threshold_2 ){
      y = sin( x ) / x;
    }else{
      y = (T)1.0;
      if( abs_x >= threshold_0 ){
        T x2 = x * x;
        y -= x2 / ((T)6.0);
        if( abs_x >= threshold_1 ){
          y += x2 * x2 / ((T)120.0);
        }
      }
    }
    return y;
  }

  void update_r_v( void ){
    T sum_w_c_s = 0.0;
    for( size_t n = 0; n < N; n++ ){
      T cos_th = dot( M, &p[n*M], q.data());
      s[n] = ((T)1.0) / ( sinc( acos( cos_th ) ) + std::numeric_limits<T>::epsilon() );

      T c_s = cos_th * s[n];
      sum_w_c_s += w[n] * c_s;

      for( size_t m = 0; m < M; m++ ){
        r[ n * M + m ] = p[ n * M + m ] * s[n] - q [ m ] * c_s;
      }
    }
    T inv_sum_w_c_s = ((T)1.0) / ( sum_w_c_s + std::numeric_limits<T>::epsilon() );
    for( size_t n = 0; n < N; n++ ){
      v[n] = w[n] * s[n] * inv_sum_w_c_s;
    }
  }

  void update_q( T phi ){
    T alpha = cos( phi );
    T beta = sinc( phi );
    for (size_t m = 0; m < M; m++){
      q[ m ] = q[ m ] * alpha + u[ m ] * beta;
    }
  }

  size_t N;
  size_t M;
  bool converged;

  // vectors in original space
  std::vector<T> w;   // size = N
  std::vector<T> p;   // size = N * M
  std::vector<T> q;   // size = M
  
  // vectors in tangent plane
  std::vector<T> v;   // size = N
  std::vector<T> r;   // size = N * M
  std::vector<T> u;   // size = M

  // working memory
  std::vector<T> s;   // size = N
};

}
#endif