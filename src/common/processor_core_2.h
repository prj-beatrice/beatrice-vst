// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_PROCESSOR_CORE_2_H_
#define BEATRICE_COMMON_PROCESSOR_CORE_2_H_

#include <array>
#include <filesystem>
#include <random>
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

// 2.0.0-rc.0 用の信号処理クラス
class ProcessorCore2 : public ProcessorCoreBase {
 public:
  static constexpr int kSphAvgMaxNSpeakers = 8;

  explicit ProcessorCore2(const double sample_rate)
      : ProcessorCoreBase(),
        any_freq_in_out_(sample_rate),
        phone_extractor_(Beatrice20rc0_CreatePhoneExtractor()),
        pitch_estimator_(Beatrice20rc0_CreatePitchEstimator()),
        waveform_generator_(Beatrice20rc0_CreateWaveformGenerator()),
        embedding_setter_(Beatrice20rc0_CreateEmbeddingSetter()),
        gain_(),
        phone_context_(Beatrice20rc0_CreatePhoneContext1()),
        pitch_context_(Beatrice20rc0_CreatePitchContext1()),
        waveform_context_(Beatrice20rc0_CreateWaveformContext1()),
        embedding_context_(Beatrice20rc0_CreateEmbeddingContext()),
        input_gain_context_(sample_rate),
        output_gain_context_(sample_rate),
        speaker_morphing_weights_{0.0f},
        speaker_morphing_weights_pruned_{0.0f},
        speaker_morphing_weights_argsort_indices_{0},
#if 0
        sph_avgs_c_(),
#else
        speaker_morphing_codebook_lottery_engine_(std::random_device{}()),
        speaker_morphing_codebook_lottery_(
            speaker_morphing_weights_pruned_.begin(),
            speaker_morphing_weights_pruned_.end()),
#endif
        sph_avg_a_(),
        sph_avgs_k_() {
  }
  ~ProcessorCore2() override {
    Beatrice20rc0_DestroyPhoneExtractor(phone_extractor_);
    Beatrice20rc0_DestroyPitchEstimator(pitch_estimator_);
    Beatrice20rc0_DestroyWaveformGenerator(waveform_generator_);
    Beatrice20rc0_DestroyEmbeddingSetter(embedding_setter_);
    Beatrice20rc0_DestroyPhoneContext1(phone_context_);
    Beatrice20rc0_DestroyPitchContext1(pitch_context_);
    Beatrice20rc0_DestroyWaveformContext1(waveform_context_);
    Beatrice20rc0_DestroyEmbeddingContext(embedding_context_);
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
  auto SetVQNumNeighbors(int /*vq_num_neighbors*/) -> ErrorCode override;
  auto SetSpeakerMorphingWeight(int /*target_speaker*/,
                                double /*morphing weight*/
                                )      // NOLINT(whitespace/parens)
      -> ErrorCode override;

 private:
  static constexpr int kSphAvgMaxNUpdates = 4;
  static constexpr int kSphAvgMaxNState = 4;

  class ConvertWithModelBlockSize {
   public:
    ConvertWithModelBlockSize() = default;
    void operator()(const float* const input, float* const output,
                    ProcessorCore2& processor_core) const {
      processor_core.Process1(input, output);
    }
  };

  std::filesystem::path model_file_;
  int target_speaker_ = 0;
  int formant_shift_ = 0;
  double pitch_shift_ = 0.0;
  int n_speakers_ = 0;
  double average_source_pitch_ = 52.0;
  double intonation_intensity_ = 1.0;
  double pitch_correction_ = 0.0;
  int pitch_correction_type_ = 0;
  double min_source_pitch_ = 33.125;
  double max_source_pitch_ = 80.875;
  int vq_num_neighbors_ = 0;

  resampler::AnyFreqInOut<ConvertWithModelBlockSize> any_freq_in_out_;

  // モデル
  Beatrice20rc0_PhoneExtractor* phone_extractor_;
  Beatrice20rc0_PitchEstimator* pitch_estimator_;
  Beatrice20rc0_WaveformGenerator* waveform_generator_;
  Beatrice20rc0_EmbeddingSetter* embedding_setter_;
  AlignedVector<float, 64> codebooks_;
  AlignedVector<float, 64> additive_speaker_embeddings_;
  AlignedVector<float, 64> formant_shift_embeddings_;
  AlignedVector<float, 64> key_value_speaker_embeddings_;
  Gain gain_;
  // 状態
  Beatrice20rc0_PhoneContext1* phone_context_;
  Beatrice20rc0_PitchContext1* pitch_context_;
  Beatrice20rc0_WaveformContext1* waveform_context_;
  Beatrice20rc0_EmbeddingContext* embedding_context_;
  Gain::Context input_gain_context_;
  Gain::Context output_gain_context_;
  int key_value_speaker_embedding_set_count_ = 0;
  bool is_ready_to_set_speaker_ = false;

  // モデルマージ
  std::array<float, kMaxNSpeakers> speaker_morphing_weights_;
  std::array<float, kMaxNSpeakers> speaker_morphing_weights_pruned_;
  std::array<int, kMaxNSpeakers> speaker_morphing_weights_argsort_indices_;
  int speaker_morphing_state_counter_ = INT_MAX;
#if 0
  std::array<SphericalAverage<float>, BEATRICE_20RC0_CODEBOOK_SIZE> sph_avgs_c_;
#elif 1
  std::mt19937 speaker_morphing_codebook_lottery_engine_;
  std::discrete_distribution<int> speaker_morphing_codebook_lottery_;
#endif
  SphericalAverage<float, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS>
      sph_avg_a_;
  std::array<
      SphericalAverage<float, BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS>,
      BEATRICE_20RC0_KV_LENGTH>
      sph_avgs_k_;

  auto IsLoaded() -> bool { return !model_file_.empty(); }
  void Process1(const float* input, float* output);

  // Key-value speaker embedding を 1 ブロック設定する。
  // 既に全ブロック設定済みであれば何も処理を行わず false を返す。
  // モデルが読み込まれているかなどの前提条件はチェックしないので、使う側で管理する。
  auto SetKeyValueSpeakerEmbedding() -> bool {
    if (key_value_speaker_embedding_set_count_ < BEATRICE_20RC0_N_BLOCKS) {
      Beatrice20rc0_SetKeyValueSpeakerEmbedding(
          embedding_setter_, key_value_speaker_embedding_set_count_++,
          embedding_context_, waveform_context_);
      return true;
    }
    return false;
  }
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_CORE_2_H_
