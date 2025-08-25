// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_PROCESSOR_CORE_1_H_
#define BEATRICE_COMMON_PROCESSOR_CORE_1_H_

#include <array>
#include <filesystem>
#include <vector>

#include "beatricelib/beatrice.h"

// Beatrice
#include "common/error.h"
#include "common/gain.h"
#include "common/model_config.h"
#include "common/processor_core.h"
#include "common/resample.h"
#include "common/spherical_average.h"

namespace beatrice::common {

// 2.0.0-beta.1 用の信号処理クラス
class ProcessorCore1 : public ProcessorCoreBase {
 public:
  explicit ProcessorCore1(const double sample_rate)
      : ProcessorCoreBase(),
        any_freq_in_out_(sample_rate),
        phone_extractor_(Beatrice20b1_CreatePhoneExtractor()),
        pitch_estimator_(Beatrice20b1_CreatePitchEstimator()),
        waveform_generator_(Beatrice20b1_CreateWaveformGenerator()),
        gain_(),
        phone_context_(Beatrice20b1_CreatePhoneContext1()),
        pitch_context_(Beatrice20b1_CreatePitchContext1()),
        waveform_context_(Beatrice20b1_CreateWaveformContext1()),
        input_gain_context_(sample_rate),
        output_gain_context_(sample_rate),
        speaker_morphing_weights_() {}
  ~ProcessorCore1() override {
    Beatrice20b1_DestroyPhoneExtractor(phone_extractor_);
    Beatrice20b1_DestroyPitchEstimator(pitch_estimator_);
    Beatrice20b1_DestroyWaveformGenerator(waveform_generator_);
    Beatrice20b1_DestroyPhoneContext1(phone_context_);
    Beatrice20b1_DestroyPitchContext1(pitch_context_);
    Beatrice20b1_DestroyWaveformContext1(waveform_context_);
  }
  [[nodiscard]] auto GetVersion() const -> int override;
  auto Process(const float* input, float* output, int n_samples)
      -> ErrorCode override;
  auto ResetContext() -> ErrorCode override;
  auto LoadModel(const ModelConfig& /*config*/,
                 const std::filesystem::path& /*file*/) -> ErrorCode override;
  auto SetSampleRate(double /*sample_rate*/) -> ErrorCode override;
  auto SetTargetSpeaker(int /*target_speaker*/) -> ErrorCode override;
  auto SetFormantShift(double /*formant_shift*/) -> ErrorCode override;
  auto SetPitchShift(double /*pitch_shift*/) -> ErrorCode override;
  auto SetInputGain(double /*input_gain*/) -> ErrorCode override;
  auto SetOutputGain(double /*output_gain*/) -> ErrorCode override;
  auto SetAverageSourcePitch(double /*average_pitch*/) -> ErrorCode override;
  // NOLINTNEXTLINE(readability/casting)
  auto SetIntonationIntensity(double /*intonation_intensity*/)
      -> ErrorCode override;
  auto SetPitchCorrection(double /*pitch_correction*/) -> ErrorCode override;
  // NOLINTNEXTLINE(readability/casting)
  auto SetPitchCorrectionType(int /*pitch_correction_type*/)
      -> ErrorCode override;
  auto SetMinSourcePitch(double /*min_source_pitch*/) -> ErrorCode override;
  auto SetMaxSourcePitch(double /*max_source_pitch*/) -> ErrorCode override;
  auto SetSpeakerMorphingWeight(int /*target_speaker*/,
                                double /*morphing weight*/
                                )      // NOLINT(whitespace/parens)
      -> ErrorCode override;

 private:
  class ConvertWithModelBlockSize {
   public:
    ConvertWithModelBlockSize() = default;
    void operator()(const float* const input, float* const output,
                    ProcessorCore1& processor_core) const {
      processor_core.Process1(input, output);
    }
  };

  std::filesystem::path model_file_;
  int target_speaker_ = 0;
  double formant_shift_ = 0.0;
  double pitch_shift_ = 0.0;
  int n_speakers_ = 0;
  double average_source_pitch_ = 52.0;
  double intonation_intensity_ = 1.0;
  double pitch_correction_ = 0.0;
  int pitch_correction_type_ = 0;
  double min_source_pitch_ = 33.125;
  double max_source_pitch_ = 80.875;

  resampler::AnyFreqInOut<ConvertWithModelBlockSize> any_freq_in_out_;

  // モデル
  Beatrice20b1_PhoneExtractor* phone_extractor_;
  Beatrice20b1_PitchEstimator* pitch_estimator_;
  Beatrice20b1_WaveformGenerator* waveform_generator_;
  std::vector<float> speaker_embeddings_;
  std::vector<float> formant_shift_embeddings_;
  Gain gain_;
  // 状態
  Beatrice20b1_PhoneContext1* phone_context_;
  Beatrice20b1_PitchContext1* pitch_context_;
  Beatrice20b1_WaveformContext1* waveform_context_;
  Gain::Context input_gain_context_;
  Gain::Context output_gain_context_;

  // モデルマージ
  std::array<float, kMaxNSpeakers> speaker_morphing_weights_;
  SphericalAverage<float, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS> sph_avg_;

  auto IsLoaded() -> bool { return !model_file_.empty(); }
  void Process1(const float* input, float* output);
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_CORE_1_H_
