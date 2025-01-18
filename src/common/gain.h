// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_GAIN_H_
#define BEATRICE_COMMON_GAIN_H_

#include <algorithm>
#include <cmath>

namespace beatrice::common {

inline static auto DbToAmp(const double db) -> double {
  return std::pow(10.0, db * 0.05);
}
inline static auto AmpToDb(const double amp) -> double {
  return 20.0 * std::log10(amp);
}

// 音量を変化させるエフェクト
class Gain {
 public:
  class Context {
   public:
    explicit Context(const double sample_rate,
                     const double target_gain_db = 0.0)
        : sample_rate_(sample_rate),
          target_gain_db_(target_gain_db),
          current_gain_db_(target_gain_db) {}
    void SetTargetGain(const double gain_db) { target_gain_db_ = gain_db; }
    void SetSampleRate(const double sr) { sample_rate_ = sr; }
    [[nodiscard]] auto IsReady() const -> bool { return sample_rate_ > 1e-5; }

   private:
    // 設定
    double sample_rate_;
    double target_gain_db_;
    // 状態
    double current_gain_db_;
    friend Gain;
  };

  void Process(const float* const input, float* const output, int n_samples,
               Gain::Context& context) const {
    const auto target_amplitude = DbToAmp(context.target_gain_db_);
    auto current_amplitude = DbToAmp(context.current_gain_db_);

    static constexpr auto kDbPerMs = 2.0;

    auto i = 0;
    if (current_amplitude < target_amplitude) {
      const auto ratio = DbToAmp(kDbPerMs / (context.sample_rate_ * 0.001));
      while (i < n_samples && current_amplitude < target_amplitude) {
        current_amplitude =
            std::min(current_amplitude * ratio, target_amplitude);
        output[i] = static_cast<float>(input[i] * current_amplitude);
        ++i;
      }
    } else if (current_amplitude > target_amplitude) {
      const auto ratio = DbToAmp(-kDbPerMs / (context.sample_rate_ * 0.001));
      while (i < n_samples && current_amplitude > target_amplitude) {
        current_amplitude =
            std::max(current_amplitude * ratio, target_amplitude);
        output[i] = static_cast<float>(input[i] * current_amplitude);
        ++i;
      }
    }
    while (i < n_samples) {
      output[i] = static_cast<float>(input[i] * current_amplitude);
      ++i;
    }
    context.current_gain_db_ = AmpToDb(current_amplitude);
  }
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_GAIN_H_
