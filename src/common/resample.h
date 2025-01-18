// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_RESAMPLE_H_
#define BEATRICE_COMMON_RESAMPLE_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>  // NOLINT(build/include_order)
#include <utility>
#include <vector>

namespace beatrice::resampler {

static inline auto NormalizedSinc(const double x) -> double {
  using std::numbers::pi;
  if (std::abs(x) < 1e-8) {
    return 1.0;
  }
  return std::sin(x * pi) / (x * pi);
}

static inline auto ComputeSimpleFraction(const double ratio) {
  struct Fraction {
    int numer, denom;
  };
  auto l = Fraction{.numer = 0, .denom = 1};
  auto r = Fraction{.numer = 1, .denom = 0};
  while (true) {
    const auto m =
        Fraction{.numer = l.numer + r.numer, .denom = l.denom + r.denom};
    if (ratio * m.denom < m.numer) {  // ratio < c
      if (m.numer >= 1000 || m.denom >= 1000) {
        return l;
      }
      r = m;
    } else {  // ratio >= c
      if (m.numer >= 1000 || m.denom >= 1000) {
        return r;
      }
      l = m;
    }
  }
}

class Buffer {
  int siz_;
  std::vector<float> data_;

 public:
  Buffer() = default;
  explicit Buffer(const int siz) { SetSize(siz); }

  void SetSize(const int new_siz) {
    siz_ = new_siz;
    data_.resize(new_siz);
  }

  void Push(const float value) {
    if (static_cast<int>(data_.size()) >= siz_ * 2) {
      data_.erase(data_.begin(), data_.end() - (siz_ - 1));
    }
    data_.push_back(value);
  }

  auto operator[](const int idx) const {
    assert(-siz_ <= idx && idx < 0);
    return *(data_.end() + idx);
  }
};

// Downsample と Upsample は必ず交互に呼ぶこと
class DownUpSamplerImpl {
  double sample_rate_high_, sample_rate_low_;
  int filter_size_;  // 出力周波数で何サンプル分か
  double normalized_cutoff_freq_down_;
  double normalized_cutoff_freq_up_;
  int ratio_high_, ratio_low_;  // 互いに素
  int fraction_clock_down_;
  int fraction_clock_up_;
  std::vector<float> filter_coef_down_;
  std::vector<float> filter_coef_up_;
  Buffer sample_buffer_high_;
  Buffer sample_buffer_low_;
  bool down_first_;
  bool ready_;

 public:
  DownUpSamplerImpl(const double sample_rate_outer,
                    const double sample_rate_inner, const int filter_size = 64,
                    const double normalized_cutoff_freq_in = 1.0,
                    const double normalized_cutoff_freq_out = 1.0)
      : filter_size_(filter_size) {
    SetSampleRates(sample_rate_outer, sample_rate_inner,
                   normalized_cutoff_freq_in, normalized_cutoff_freq_out);
  }

  [[nodiscard]] auto IsReady() const -> bool { return ready_; }

  void ResampleIn(const std::vector<float>& input, std::vector<float>& output) {
    if (!IsReady()) {
      output.resize(0);
      return;
    }
    if (down_first_) {
      Downsample(input, output);
    } else {
      Upsample(input, output);
    }
  }
  void ResampleOut(const std::vector<float>& input,
                   std::vector<float>& output) {
    if (!IsReady()) {
      output.resize(0);
      return;
    }
    if (down_first_) {
      Upsample(input, output);
    } else {
      Downsample(input, output);
    }
  }

  // 入力を受け取ると、その時刻分だけ正確にクロックを進める
  // 新しく出力できたサンプルを返す
  // 返すサンプル数は呼ばれるたびに異なる場合がある
  void Downsample(const std::vector<float>& input, std::vector<float>& output) {
    if (down_first_) {
      assert(fraction_clock_down_ == fraction_clock_up_);
    } else {
      assert(fraction_clock_up_ >= ratio_high_ - ratio_low_);
    }

    const auto gain =
        static_cast<float>(ratio_low_) / static_cast<float>(ratio_high_);

    output.resize(
        (static_cast<int>(input.size()) * ratio_low_ + fraction_clock_down_) /
        ratio_high_);
    auto output_itr = output.begin();
    for (const auto in_sample : input) {
      sample_buffer_high_.Push(in_sample);
      fraction_clock_down_ += ratio_low_;
      if (fraction_clock_down_ >= ratio_high_) {
        fraction_clock_down_ -= ratio_high_;
        auto out_sample = 0.0F;
        auto idx_buffer = -1;
        for (auto idx_filter = ratio_low_ - fraction_clock_down_;
             idx_filter < static_cast<int>(filter_coef_down_.size()) - 1;
             idx_filter += ratio_low_) {
          out_sample +=
              sample_buffer_high_[idx_buffer--] * filter_coef_down_[idx_filter];
        }
        *output_itr++ = out_sample * gain;
      }
    }
    assert(output_itr == output.end());
    if (!down_first_) {
      assert(fraction_clock_down_ == fraction_clock_up_);
    }
  }

  // input は Downsample の output と同じ長さであることを仮定
  // output は Downsample の input と同じ長さであることを仮定
  void Upsample(const std::vector<float>& input, std::vector<float>& output) {
    if (!down_first_) {
      assert(fraction_clock_down_ == fraction_clock_up_);
    }

    if (down_first_) {
      assert((static_cast<int>(input.size()) * ratio_high_ +
              fraction_clock_down_ - fraction_clock_up_) %
                 ratio_low_ ==
             0);
      output.resize((static_cast<int>(input.size()) * ratio_high_ +
                     fraction_clock_down_ - fraction_clock_up_) /
                    ratio_low_);
    } else {
      output.resize(((static_cast<int>(input.size()) + 1) * ratio_high_ -
                     fraction_clock_up_ - 1) /
                    ratio_low_);
    }
    auto input_itr = input.begin();
    for (auto&& out_sample : output) {
      fraction_clock_up_ += ratio_low_;
      if (fraction_clock_up_ >= ratio_high_) {
        fraction_clock_up_ -= ratio_high_;
        sample_buffer_low_.Push(*input_itr++);
      }
      out_sample = 0.0F;
      auto idx_buffer = -1;
      for (auto idx_filter = fraction_clock_up_;
           idx_filter < static_cast<int>(filter_coef_up_.size()) - 1;
           idx_filter += ratio_high_) {
        out_sample +=
            sample_buffer_low_[idx_buffer--] * filter_coef_up_[idx_filter];
      }
    }
    assert(input_itr == input.end());
    if (down_first_) {
      assert(fraction_clock_down_ == fraction_clock_up_);
    }
  }

  // テーブルの構築など
  void Reset() {
    const auto coef_length = filter_size_ * ratio_high_ + 1;
    const auto center_idx = coef_length / 2;
    filter_coef_down_.resize(coef_length);
    filter_coef_up_.resize(coef_length);

    const auto gain_down = normalized_cutoff_freq_down_;
    const auto gain_up = normalized_cutoff_freq_up_;
    for (auto i = 0; i < coef_length; ++i) {
      const auto sinc_down = NormalizedSinc(
          static_cast<double>(i - center_idx) /
          static_cast<double>(ratio_high_) * normalized_cutoff_freq_down_);
      const auto sinc_up = NormalizedSinc(static_cast<double>(i - center_idx) /
                                          static_cast<double>(ratio_high_) *
                                          normalized_cutoff_freq_up_);
      const auto window =
          0.5 - 0.5 * std::cos(std::numbers::pi * 2.0 /
                               static_cast<double>(coef_length - 1) *
                               static_cast<double>(i));
      filter_coef_down_[i] = static_cast<float>(gain_down * sinc_down * window);
      filter_coef_up_[i] = static_cast<float>(gain_up * sinc_up * window);
    }

    fraction_clock_down_ = ratio_high_ - 1;
    fraction_clock_up_ = ratio_high_ - 1;

    sample_buffer_high_.SetSize(filter_size_ * ratio_high_ / ratio_low_ + 1);
    sample_buffer_low_.SetSize(filter_size_ + 1);
  }

  void SetSampleRates(const double sample_rate_outer,
                      const double sample_rate_inner,
                      const double normalized_cutoff_freq_in,
                      const double normalized_cutoff_freq_out) {
    if (sample_rate_outer <= 0.0 || sample_rate_inner <= 0.0) {
      ready_ = false;
      return;
    }
    down_first_ = sample_rate_outer >= sample_rate_inner;
    if (down_first_) {
      sample_rate_high_ = sample_rate_outer;
      sample_rate_low_ = sample_rate_inner;
      normalized_cutoff_freq_down_ = normalized_cutoff_freq_in;
      normalized_cutoff_freq_up_ = normalized_cutoff_freq_out;
    } else {
      sample_rate_high_ = sample_rate_inner;
      sample_rate_low_ = sample_rate_outer;
      normalized_cutoff_freq_down_ = normalized_cutoff_freq_out;
      normalized_cutoff_freq_up_ = normalized_cutoff_freq_in;
    }
    const auto [numer, denom] =
        ComputeSimpleFraction(sample_rate_high_ / sample_rate_low_);
    if (numer == 0 || denom == 0) {
      ready_ = false;
      return;
    }
    ratio_high_ = numer;
    ratio_low_ = denom;
    assert(ratio_high_ >= ratio_low_);
    Reset();
    ready_ = true;
  }
};

// n サンプル受け取って n サンプルを返すような関数をラップして、
// 別のサンプリング周波数 H で m サンプル受け取って
// m サンプル返すオブジェクトにする
template <class Func>
class ConvertStreamFunctionFrequency {
  Func function_;
  double original_frequency_;
  double target_frequency_;
  DownUpSamplerImpl down_up_sampler_;

 public:
  ConvertStreamFunctionFrequency(Func&& function,
                                 const double original_frequency,
                                 const double target_frequency,
                                 const int filter_size = 32,
                                 const double normalized_cutoff_freq_in = 1.0,
                                 const double normalized_cutoff_freq_out = 1.0)
      : function_(function),
        original_frequency_(original_frequency),
        target_frequency_(target_frequency),
        down_up_sampler_(target_frequency, original_frequency, filter_size,
                         normalized_cutoff_freq_in,
                         normalized_cutoff_freq_out) {}

  // input == output であってもよい
  template <class... Context>
  auto operator()(const float* const input, float* const output, const int m,
                  Context&&... context) {
    auto tmp_vector = std::vector<float>(input, input + m);
    auto converted_input = std::vector<float>();
    down_up_sampler_.ResampleIn(tmp_vector, converted_input);

    const auto n = static_cast<int>(converted_input.size());
    auto converted_output = std::vector<float>(n);
    function_(std::to_address(converted_input.begin()),
              std::to_address(converted_output.begin()), n,
              std::forward<Context>(context)...);

    down_up_sampler_.ResampleOut(converted_output, tmp_vector);
    std::memcpy(output, std::to_address(tmp_vector.begin()), m * sizeof(float));
  }

  [[nodiscard]] auto IsReady() const -> bool {
    return down_up_sampler_.IsReady();
  }

  [[nodiscard]] auto GetTargetFrequency() const -> double {
    return target_frequency_;
  }
};

// n サンプル受け取って n サンプルを返す関数をラップして、
// 任意のサンプル数受け取って同じ長さを返すオブジェクトにする
template <int n, class Func>
class ConvertStreamFunctionBlockSize {
  alignas(64) std::array<float, n> buffer_;
  Func function_;
  int idx_buffer_ = 0;

 public:
  explicit ConvertStreamFunctionBlockSize(Func function)
      : buffer_(), function_(function) {}

  // input != output でなければならない
  template <class... Context>
  auto operator()(const float* const input, float* const output, const int n_io,
                  Context&&... context) {
    assert(input != output);
    for (auto idx_io = 0; idx_io < n_io;) {
      const auto n_samples_process = std::min(n - idx_buffer_, n_io - idx_io);
      std::memcpy(&output[idx_io], &buffer_[idx_buffer_],
                  sizeof(float) * n_samples_process);
      std::memcpy(&buffer_[idx_buffer_], &input[idx_io],
                  sizeof(float) * n_samples_process);
      idx_buffer_ += n_samples_process;
      idx_io += n_samples_process;
      if (idx_buffer_ == n) {
        idx_buffer_ = 0;
        alignas(64) std::array<float, n> processed_buffer;
        function_(std::to_address(buffer_.begin()),
                  std::to_address(processed_buffer.begin()),
                  std::forward<Context>(context)...);
        buffer_ = processed_buffer;
      }
    }
  }
};

// 2n サンプル受け取って 3n サンプル返す関数をラップして、
// 6n サンプル受け取って 6n サンプル返すオブジェクトにする。
// 入力は適切に LPF をかけたものである必要がある。
// 出力はエイリアシングしているので、適切に LPF をかける必要がある。
template <int n, class Func>
class ConvertStreamFunctionFrom2In3OutTo6InOut {
  Func function_;

 public:
  explicit ConvertStreamFunctionFrom2In3OutTo6InOut(Func function)
      : function_(function) {}

  // input == output であってもよい
  template <class... Context>
  auto operator()(const float* const input, float* const output,
                  Context&&... context) {
    alignas(64) auto function_in = std::array<float, 2 * n>();
    alignas(64) auto function_out = std::array<float, 3 * n>();
    for (auto i = 0; i < 2 * n; ++i) {
      function_in[i] = input[(i + 1) * 3 - 1];
    }
    function_(std::to_address(function_in.begin()),
              std::to_address(function_out.begin()),
              std::forward<Context>(context)...);
    std::memset(output, 0, 6 * n * sizeof(float));
    for (auto i = 0; i < 3 * n; ++i) {
      output[i * 2] = function_out[i];
    }
  }
};

// ↑ 3 つの組み合わせ
// 16kHz で 160 サンプル受け取って 24kHz で 240 サンプル返す関数をラップして、
// 任意のサンプリング周波数で m サンプル受け取って
// m サンプル返すオブジェクトにする
template <class ProcessWithModelBlockSize>
class AnyFreqInOut {
  using ProcessWith6n =
      ConvertStreamFunctionFrom2In3OutTo6InOut<80, ProcessWithModelBlockSize>;
  using ProcessWithAnyBlockSize =
      resampler::ConvertStreamFunctionBlockSize<80 * 6, ProcessWith6n>;
  using ProcessWithAnyFrequency =
      resampler::ConvertStreamFunctionFrequency<ProcessWithAnyBlockSize>;
  ProcessWithAnyFrequency process_;

 public:
  explicit AnyFreqInOut(const double sample_rate)
      : process_(ProcessWithAnyFrequency(
            ProcessWithAnyBlockSize(ProcessWith6n(ProcessWithModelBlockSize())),
            48000.0, sample_rate, 32,
            0.99 * 16000.0 / std::clamp(sample_rate, 16000.0, 48000.0),
            0.99 * 24000.0 / std::clamp(sample_rate, 24000.0, 48000.0))) {}

  template <class... Context>
  auto operator()(const float* const input, float* const output, const int m,
                  Context&&... context) {
    process_(input, output, m, std::forward<Context>(context)...);
  }

  void SetSampleRate(const double sample_rate) {
    process_ = ProcessWithAnyFrequency(
        ProcessWithAnyBlockSize(ProcessWith6n(ProcessWithModelBlockSize())),
        48000.0, sample_rate, 32,
        0.99 * 16000.0 / std::clamp(sample_rate, 16000.0, 48000.0),
        0.99 * 24000.0 / std::clamp(sample_rate, 24000.0, 48000.0));
  }

  [[nodiscard]] auto GetSampleRate() const -> double {
    return process_.GetTargetFrequency();
  }

  [[nodiscard]] auto IsReady() const -> bool { return process_.IsReady(); }
};

}  // namespace beatrice::resampler

#endif  // BEATRICE_COMMON_RESAMPLE_H_
