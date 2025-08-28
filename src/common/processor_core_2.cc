// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "common/processor_core_2.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <numeric>

#include "common/error.h"
#include "common/model_config.h"

namespace beatrice::common {

auto ProcessorCore2::GetVersion() const -> int { return 1; }
auto ProcessorCore2::Process(const float* const input, float* const output,
                             const int n_samples) -> ErrorCode {
  const auto fill_zero = [output, n_samples] {
    std::memset(output, 0, sizeof(float) * n_samples);
  };
  if (!IsLoaded()) {
    return fill_zero(), ErrorCode::kModelNotLoaded;
  }
  if (!any_freq_in_out_.IsReady()) {
    return fill_zero(), ErrorCode::kResamplerNotReady;
  }
  if (!input_gain_context_.IsReady()) {
    return fill_zero(), ErrorCode::kGainNotReady;
  }
  if (!output_gain_context_.IsReady()) {
    return fill_zero(), ErrorCode::kGainNotReady;
  }
  if (pitch_correction_type_ < 0 || pitch_correction_type_ > 1) {
    return fill_zero(), ErrorCode::kInvalidPitchCorrectionType;
  }
  gain_.Process(input, output, n_samples, input_gain_context_);
  any_freq_in_out_(output, output, n_samples, *this);
  gain_.Process(output, output, n_samples, output_gain_context_);
  return ErrorCode::kSuccess;
}

void ProcessorCore2::Process1(const float* const input, float* const output) {
  if (target_speaker_ == n_speakers_) {
    // モーフィング処理
    // codebookについては色々処理の候補があるのでマクロで分岐

#if 0
    if (speaker_morphing_state_counter_ < kSphAvgMaxNState) {
      // spherical average を使う場合
      // 数が多くて計算量が重いので、時分割処理を行う。
      auto start_idx = BEATRICE_20RC0_KV_LENGTH *
                       speaker_morphing_state_counter_ / kSphAvgMaxNState;
      auto end_idx = BEATRICE_20RC0_KV_LENGTH *
                     (speaker_morphing_state_counter_ + 1) / kSphAvgMaxNState;
      for (size_t i = start_idx; i < end_idx; ++i) {
        sph_avgs_c_[i].SetWeights(
            n_speakers_, speaker_morphing_weights_pruned_.data(),
            speaker_morphing_weights_argsort_indices_.data());
        for (size_t j = 0; j < kSphAvgMaxNUpdates; ++j) {
          if (sph_avgs_c_[i].Update()) break;
        }
        sph_avgs_c_[i].GetResult(
            BEATRICE_20RC0_PHONE_CHANNELS,
            codebooks_.data() +
                (n_speakers_ * BEATRICE_20RC0_CODEBOOK_SIZE + i) *
                    BEATRICE_20RC0_PHONE_CHANNELS);
      }
    } else if (speaker_morphing_state_counter_ == kSphAvgMaxNState) {
      Beatrice20rc0_SetCodebook(
          phone_context_,
          codebooks_.data() + n_speakers_ * (BEATRICE_20RC0_CODEBOOK_SIZE *
                                             BEATRICE_20RC0_PHONE_CHANNELS));
    }
#elif 0
    if (speaker_morphing_state_counter_ == 0) {
      // 最大重みを持つ話者の情報をそのまま採用する場合
      // この場合 codebook のサイズは
      // (n_speaker_+1)ではなくて(n_speaker_)で十分
      Beatrice20rc0_SetCodebook(
          phone_context_,
          codebooks_.data() + speaker_morphing_weights_argsort_indices_[0] *
                                  (BEATRICE_20RC0_CODEBOOK_SIZE *
                                   BEATRICE_20RC0_PHONE_CHANNELS));
    }
#else
    // 重みを抽選確率として用いて毎フレームランダムな話者ののものを抽選で選ぶ場合
    // この場合も codebook のサイズは (n_speaker_+1)ではなくて(n_speaker_)で十分
    speaker_morphing_codebook_lottery_.param(
        std::discrete_distribution<int>::param_type(
            speaker_morphing_weights_pruned_.begin(),
            speaker_morphing_weights_pruned_.end()));
    auto idx = speaker_morphing_codebook_lottery_(
        speaker_morphing_codebook_lottery_engine_);
    Beatrice20rc0_SetCodebook(
        phone_context_,
        codebooks_.data() + idx * (BEATRICE_20RC0_CODEBOOK_SIZE *
                                   BEATRICE_20RC0_PHONE_CHANNELS));
#endif

    if (speaker_morphing_state_counter_ == 0) {
      // additive_speaker_embeddings については
      // 重みの更新があった次のフレームで一気に更新する
      sph_avg_a_.SetWeights(n_speakers_,
                            speaker_morphing_weights_pruned_.data(),
                            speaker_morphing_weights_argsort_indices_.data());
      for (size_t j = 0; j < kSphAvgMaxNUpdates; ++j) {
        if (sph_avg_a_.Update()) break;
      }
      sph_avg_a_.GetResult(
          BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
          additive_speaker_embeddings_.data() +
              n_speakers_ * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
      Beatrice20rc0_SetAdditiveSpeakerEmbedding(
          embedding_setter_,
          additive_speaker_embeddings_.data() +
              n_speakers_ * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
          embedding_context_, waveform_context_);
    }

    if (speaker_morphing_state_counter_ < kSphAvgMaxNState) {
      // key_value_speaker_embeddings_ の spherical average については
      // 重めの処理なので数フレームに分けて計算する
      auto start_idx = BEATRICE_20RC0_KV_LENGTH *
                       speaker_morphing_state_counter_ / kSphAvgMaxNState;
      auto end_idx = BEATRICE_20RC0_KV_LENGTH *
                     (speaker_morphing_state_counter_ + 1) / kSphAvgMaxNState;
      for (size_t i = start_idx; i < end_idx; ++i) {
        sph_avgs_k_[i].SetWeights(
            n_speakers_, speaker_morphing_weights_pruned_.data(),
            speaker_morphing_weights_argsort_indices_.data());
        for (size_t j = 0; j < kSphAvgMaxNUpdates; ++j) {
          if (sph_avgs_k_[i].Update()) break;
        }
        sph_avgs_k_[i].GetResult(
            BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS,
            key_value_speaker_embeddings_.data() +
                (n_speakers_ * BEATRICE_20RC0_KV_LENGTH + i) *
                    BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS);
      }
    } else if (speaker_morphing_state_counter_ == kSphAvgMaxNState) {
      Beatrice20rc0_RegisterKeyValueSpeakerEmbedding(
          embedding_setter_,
          key_value_speaker_embeddings_.data() +
              n_speakers_ * (BEATRICE_20RC0_KV_LENGTH *
                             BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS),
          embedding_context_);
      key_value_speaker_embedding_set_count_ = 0;
    }

    if (speaker_morphing_state_counter_ <= kSphAvgMaxNState) {
      ++speaker_morphing_state_counter_;
    }
  }

  // Beatrice20rc0_SetKeyValueSpeakerEmbedding は重めの処理なので
  // 4 フレームかけて処理する
  SetKeyValueSpeakerEmbedding();

  std::array<float, BEATRICE_20RC0_PHONE_CHANNELS> phone;
  Beatrice20rc0_ExtractPhone1(phone_extractor_, input, phone.data(),
                              phone_context_);
  int quantized_pitch;
  std::array<float, 4> pitch_feature;
  Beatrice20rc0_EstimatePitch1(pitch_estimator_, input, &quantized_pitch,
                               pitch_feature.data(), pitch_context_);
  constexpr auto kPitchBinsPerSemitone =
      static_cast<double>(BEATRICE_PITCH_BINS_PER_OCTAVE) / 12.0;
  // PitchShift, IntonationIntensity
  auto tmp_quantized_pitch =
      average_source_pitch_ +
      (static_cast<double>(quantized_pitch) - average_source_pitch_) *
          intonation_intensity_ +
      kPitchBinsPerSemitone * pitch_shift_;
  // PitchCorrection
  if (pitch_correction_ != 0.0) {
    const auto before_pitch_correction = tmp_quantized_pitch;
    if (pitch_correction_type_ == 0) {
      // x|x|^{-p}
      const auto nearest_pitch =
          (std::floor(tmp_quantized_pitch / kPitchBinsPerSemitone) + 0.5) *
          kPitchBinsPerSemitone;
      const auto normalized_delta =
          (tmp_quantized_pitch - nearest_pitch) * (2.0 / kPitchBinsPerSemitone);
      if (std::abs(normalized_delta) < 1e-4) {
        tmp_quantized_pitch = nearest_pitch;
      } else {
        tmp_quantized_pitch =
            nearest_pitch +
            normalized_delta *
                std::pow(std::abs(normalized_delta), -pitch_correction_) *
                (kPitchBinsPerSemitone / 2.0);
      }
      assert(std::abs(tmp_quantized_pitch -
                      std::round(tmp_quantized_pitch / kPitchBinsPerSemitone) *
                          kPitchBinsPerSemitone) <=
             std::abs(before_pitch_correction -
                      std::round(tmp_quantized_pitch / kPitchBinsPerSemitone) *
                          kPitchBinsPerSemitone) +
                 1e-4);
    } else if (pitch_correction_type_ == 1) {
      // sgn(x)|x|^{1/(1-p)}
      const auto nearest_pitch =
          std::round(tmp_quantized_pitch / kPitchBinsPerSemitone) *
          kPitchBinsPerSemitone;
      const auto normalized_delta =
          (tmp_quantized_pitch - nearest_pitch) * (2.0 / kPitchBinsPerSemitone);
      if (pitch_correction_ > 1 - 1e-4) {
        tmp_quantized_pitch = nearest_pitch;
      } else if (normalized_delta >= 0.0) {
        tmp_quantized_pitch =
            nearest_pitch +
            std::pow(normalized_delta, 1.0 / (1.0 - pitch_correction_)) *
                (kPitchBinsPerSemitone / 2.0);
      } else {
        tmp_quantized_pitch =
            nearest_pitch -
            std::pow(-normalized_delta, 1.0 / (1.0 - pitch_correction_)) *
                (kPitchBinsPerSemitone / 2.0);
      }
      assert(std::abs(tmp_quantized_pitch - nearest_pitch) <=
             std::abs(before_pitch_correction - nearest_pitch) + 1e-4);
    } else {
      assert(false);
    }
  }
  quantized_pitch =
      std::clamp(static_cast<int>(std::round(tmp_quantized_pitch)), 1,
                 BEATRICE_20RC0_PITCH_BINS - 1);
  Beatrice20rc0_GenerateWaveform1(waveform_generator_, phone.data(),
                                  &quantized_pitch, pitch_feature.data(),
                                  output, waveform_context_);
}

auto ProcessorCore2::ResetContext() -> ErrorCode {
  Beatrice20rc0_DestroyPhoneContext1(phone_context_);
  Beatrice20rc0_DestroyPitchContext1(pitch_context_);
  Beatrice20rc0_DestroyWaveformContext1(waveform_context_);
  Beatrice20rc0_DestroyEmbeddingContext(embedding_context_);
  phone_context_ = Beatrice20rc0_CreatePhoneContext1();
  pitch_context_ = Beatrice20rc0_CreatePitchContext1();
  waveform_context_ = Beatrice20rc0_CreateWaveformContext1();
  embedding_context_ = Beatrice20rc0_CreateEmbeddingContext();

  // 目標話者を再設定
  auto error = SetTargetSpeaker(target_speaker_);
  while (SetKeyValueSpeakerEmbedding());

  // 各種パラメータを再設定
  if (const auto err = SetFormantShift(formant_shift_);
      error == ErrorCode::kSuccess) {
    error = err;
  }
  if (const auto err = SetMinSourcePitch(min_source_pitch_);
      error == ErrorCode::kSuccess) {
    error = err;
  }
  if (const auto err = SetMaxSourcePitch(max_source_pitch_);
      error == ErrorCode::kSuccess) {
    error = err;
  }
  if (const auto err = SetVQNumNeighbors(vq_num_neighbors_);
      error == ErrorCode::kSuccess) {
    error = err;
  }

  return error;
}

auto ProcessorCore2::LoadModel(const ModelConfig& /*config*/,
                               const std::filesystem::path& new_model_file)
    -> ErrorCode {
  // IsLoaded() が false を返すようにする
  model_file_.clear();
  is_ready_to_set_speaker_ = false;

  // 各種パラメータを読み込む
  const auto d = new_model_file.parent_path();
  if (const auto err = Beatrice20rc0_ReadPhoneExtractorParameters(
          phone_extractor_,
          reinterpret_cast<const char*>(
              (d / "phone_extractor.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20rc0_ReadPitchEstimatorParameters(
          pitch_estimator_,
          reinterpret_cast<const char*>(
              (d / "pitch_estimator.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20rc0_ReadWaveformGeneratorParameters(
          waveform_generator_,
          reinterpret_cast<const char*>(
              (d / "waveform_generator.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20rc0_ReadEmbeddingSetterParameters(
          embedding_setter_,
          reinterpret_cast<const char*>(
              (d / "embedding_setter.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }

  // 話者埋め込みを読み込む
  if (const auto err = Beatrice20rc0_ReadNSpeakers(
          reinterpret_cast<const char*>(
              (d / "speaker_embeddings.bin").u8string().c_str()),
          &n_speakers_)) {
    return static_cast<ErrorCode>(err);
  }
  // ここで (n_speakers_ + 1) なのは、末尾にモーフィング結果を格納するため
  codebooks_.resize((n_speakers_ + 1) * (BEATRICE_20RC0_CODEBOOK_SIZE *
                                         BEATRICE_20RC0_PHONE_CHANNELS));
  additive_speaker_embeddings_.resize(
      (n_speakers_ + 1) * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  formant_shift_embeddings_.resize(9 *
                                   BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  key_value_speaker_embeddings_.resize(
      (n_speakers_ + 1) * (BEATRICE_20RC0_KV_LENGTH *
                           BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS));
  if (const auto err = Beatrice20rc0_ReadSpeakerEmbeddings(
          reinterpret_cast<const char*>(
              (d / "speaker_embeddings.bin").u8string().c_str()),
          codebooks_.data(), additive_speaker_embeddings_.data(),
          formant_shift_embeddings_.data(),
          key_value_speaker_embeddings_.data())) {
    return static_cast<ErrorCode>(err);
  }

  // モーフィング結果格納用の領域はファイルからの読み込み時に初期化されないので念の為ここで初期化しておく
  std::fill_n(codebooks_.data() + n_speakers_ * (BEATRICE_20RC0_CODEBOOK_SIZE *
                                                 BEATRICE_20RC0_PHONE_CHANNELS),
              BEATRICE_20RC0_CODEBOOK_SIZE * BEATRICE_20RC0_PHONE_CHANNELS,
              0.0f);
  std::fill_n(additive_speaker_embeddings_.data() +
                  n_speakers_ * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
              BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS, 0.0f);
  std::fill_n(
      key_value_speaker_embeddings_.data() +
          n_speakers_ * (BEATRICE_20RC0_KV_LENGTH *
                         BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS),
      BEATRICE_20RC0_KV_LENGTH * BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS,
      0.0f);

#if 0
  // codebook モーフィング用に sph_avg を初期化する
  std::vector<float> codebook_block(n_speakers_ *
                                    BEATRICE_20RC0_PHONE_CHANNELS);
  for (size_t i = 0; i < BEATRICE_20RC0_CODEBOOK_SIZE; ++i) {
    for (size_t j = 0; j < n_speakers_; ++j) {
      std::copy_n(codebooks_.data() + (j * BEATRICE_20RC0_CODEBOOK_SIZE + i) *
                                          BEATRICE_20RC0_PHONE_CHANNELS,
                  BEATRICE_20RC0_PHONE_CHANNELS,
                  codebook_block.data() + j * BEATRICE_20RC0_PHONE_CHANNELS);
    }
    sph_avgs_c_[i].Initialize(n_speakers_, BEATRICE_20RC0_PHONE_CHANNELS,
                              codebook_block.data(), kSphAvgMaxNSpeakers);
  }
#endif

  // additive_speaker_embeddings モーフィング用の sph_avg を初期化する
  sph_avg_a_.Initialize(n_speakers_,
                        BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
                        additive_speaker_embeddings_.data(),
                        std::min(n_speakers_, kSphAvgMaxNSpeakers));

  // key-value モーフィング用に sph_avg を初期化する
  std::vector<float> key_value_block(
      n_speakers_ * BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS);
  for (size_t i = 0; i < BEATRICE_20RC0_KV_LENGTH; ++i) {
    for (size_t j = 0; j < n_speakers_; ++j) {
      std::copy_n(key_value_speaker_embeddings_.data() +
                      (j * BEATRICE_20RC0_KV_LENGTH + i) *
                          BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS,
                  BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS,
                  key_value_block.data() +
                      j * BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS);
    }
    sph_avgs_k_[i].Initialize(
        n_speakers_, BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS,
        key_value_block.data(), std::min(n_speakers_, kSphAvgMaxNSpeakers));
  }
  speaker_morphing_state_counter_ = INT_MAX;

  is_ready_to_set_speaker_ = true;

  // 目標話者を 0 に設定する
  if (const auto err = SetTargetSpeaker(0); err != ErrorCode::kSuccess) {
    return err;
  }
  while (SetKeyValueSpeakerEmbedding());

  model_file_ = new_model_file;

  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetSampleRate(const double new_sample_rate) -> ErrorCode {
  if (new_sample_rate == any_freq_in_out_.GetSampleRate()) {
    return ErrorCode::kSuccess;
  }
  any_freq_in_out_.SetSampleRate(new_sample_rate);
  input_gain_context_.SetSampleRate(new_sample_rate);
  output_gain_context_.SetSampleRate(new_sample_rate);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetTargetSpeaker(const int new_target_speaker_id)
    -> ErrorCode {
  if (!is_ready_to_set_speaker_) {
    return ErrorCode::kModelNotLoaded;
  }
  if (new_target_speaker_id < 0 || n_speakers_ + 1 <= new_target_speaker_id) {
    return ErrorCode::kSpeakerIDOutOfRange;
  }
  assert(static_cast<int>(codebooks_.size()) ==
         (n_speakers_ + 1) *
             (BEATRICE_20RC0_CODEBOOK_SIZE * BEATRICE_20RC0_PHONE_CHANNELS));
  assert(static_cast<int>(additive_speaker_embeddings_.size()) ==
         (n_speakers_ + 1) * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  assert(static_cast<int>(key_value_speaker_embeddings_.size()) ==
         (n_speakers_ + 1) * (BEATRICE_20RC0_KV_LENGTH *
                              BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS));
  Beatrice20rc0_SetCodebook(
      phone_context_, codebooks_.data() + new_target_speaker_id *
                                              (BEATRICE_20RC0_CODEBOOK_SIZE *
                                               BEATRICE_20RC0_PHONE_CHANNELS));
  Beatrice20rc0_SetAdditiveSpeakerEmbedding(
      embedding_setter_,
      additive_speaker_embeddings_.data() +
          new_target_speaker_id * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
      embedding_context_, waveform_context_);
  Beatrice20rc0_RegisterKeyValueSpeakerEmbedding(
      embedding_setter_,
      key_value_speaker_embeddings_.data() +
          new_target_speaker_id *
              (BEATRICE_20RC0_KV_LENGTH *
               BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS),
      embedding_context_);
  target_speaker_ = new_target_speaker_id;
  key_value_speaker_embedding_set_count_ = 0;
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetFormantShift(const double new_formant_shift)
    -> ErrorCode {
  formant_shift_ = std::clamp(new_formant_shift, -2.0, 2.0);
  const auto index = static_cast<int>(std::round(formant_shift_ * 2.0 + 4.0));
  assert(0 <= index && index < 9);
  assert(static_cast<int>(formant_shift_embeddings_.size()) ==
         9 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  Beatrice20rc0_SetFormantShiftEmbedding(
      embedding_setter_,
      formant_shift_embeddings_.data() +
          index * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
      embedding_context_, waveform_context_);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetPitchShift(const double new_pitch_shift) -> ErrorCode {
  pitch_shift_ = std::clamp(new_pitch_shift, -24.0, 24.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetInputGain(const double new_input_gain) -> ErrorCode {
  input_gain_context_.SetTargetGain(new_input_gain);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetOutputGain(const double new_output_gain) -> ErrorCode {
  output_gain_context_.SetTargetGain(new_output_gain);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetSpeakerMorphingWeight(int target_speaker_id,
                                              double morphing_weight)
    -> ErrorCode {
  if (!is_ready_to_set_speaker_) {
    return ErrorCode::kModelNotLoaded;
  }
  if (target_speaker_id < 0 || target_speaker_id >= kMaxNSpeakers) {
    return ErrorCode::kSpeakerIDOutOfRange;
  }
  speaker_morphing_weights_[target_speaker_id] = morphing_weight;

  if (target_speaker_id < n_speakers_) {
    /* 非ゼロ weight の個数が設定値を超えないように、大きい方から順番に残す */
    auto& indices = speaker_morphing_weights_argsort_indices_;
    std::iota(indices.data(), indices.data() + n_speakers_, 0);
    std::sort(indices.data(), indices.data() + n_speakers_,
              [this](size_t a, size_t b) {
                return speaker_morphing_weights_[a] >
                       speaker_morphing_weights_[b];
              });
    for (size_t i = 0; i < kSphAvgMaxNSpeakers; ++i) {
      speaker_morphing_weights_pruned_[indices[i]] =
          speaker_morphing_weights_[indices[i]];
    }
    for (size_t i = kSphAvgMaxNSpeakers; i < n_speakers_; ++i) {
      speaker_morphing_weights_pruned_[indices[i]] = 0.0;
    }

    // ここでsph_avg_a_などの重みを更新(sph_avg_.SetWeights())してしまうと、
    // モデル読み込み時に一気にkMaxNSpeakersの数だけ重みが設定されるため処理が重くなるので、
    // フラグだけ立てて次のフレームから更新するようにする。
    speaker_morphing_state_counter_ = 0;
  }
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetAverageSourcePitch(const double new_average_pitch)
    -> ErrorCode {
  average_source_pitch_ = std::clamp(new_average_pitch, 0.0, 128.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetIntonationIntensity(
    const double new_intonation_intensity) -> ErrorCode {
  intonation_intensity_ = new_intonation_intensity;
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetPitchCorrection(const double new_pitch_correction)
    -> ErrorCode {
  pitch_correction_ = std::clamp(new_pitch_correction, 0.0, 1.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetPitchCorrectionType(const int new_pitch_correction_type)
    -> ErrorCode {
  if (new_pitch_correction_type < 0 || new_pitch_correction_type > 1) {
    return ErrorCode::kInvalidPitchCorrectionType;
  }
  pitch_correction_type_ = new_pitch_correction_type;
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetMinSourcePitch(const double new_min_source_pitch)
    -> ErrorCode {
  min_source_pitch_ = std::clamp(new_min_source_pitch, 0.0, 128.0);
  Beatrice20rc0_SetMinQuantizedPitch(
      pitch_context_,
      std::clamp(
          static_cast<int>(std::round((min_source_pitch_ - 33.0) *
                                      (BEATRICE_PITCH_BINS_PER_OCTAVE / 12.0))),
          1, BEATRICE_20RC0_PITCH_BINS - 1));
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetMaxSourcePitch(const double new_max_source_pitch)
    -> ErrorCode {
  max_source_pitch_ = std::clamp(new_max_source_pitch, 0.0, 128.0);
  Beatrice20rc0_SetMaxQuantizedPitch(
      pitch_context_,
      std::clamp(
          static_cast<int>(std::round((max_source_pitch_ - 33.0) *
                                      (BEATRICE_PITCH_BINS_PER_OCTAVE / 12.0))),
          1, BEATRICE_20RC0_PITCH_BINS - 1));
  return ErrorCode::kSuccess;
}

auto ProcessorCore2::SetVQNumNeighbors(const int new_vq_num_neighbors)
    -> ErrorCode {
  vq_num_neighbors_ = std::clamp(new_vq_num_neighbors, 0, 8);
  Beatrice20rc0_SetVQNumNeighbors(phone_context_, vq_num_neighbors_);
  return ErrorCode::kSuccess;
}

}  // namespace beatrice::common
