// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "common/processor_core_0.h"

#include <cassert>
#include <cstring>
#include <filesystem>

#include "common/model_config.h"

namespace beatrice::common {

auto ProcessorCore0::GetVersion() const -> int { return 0; }
auto ProcessorCore0::Process(const float* const input, float* const output,
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
  if (target_speaker_ < 0) {
    return fill_zero(), ErrorCode::kSpeakerIDOutOfRange;
  }
  if (target_speaker_ > n_speakers_) {
    return fill_zero(), ErrorCode::kSpeakerIDOutOfRange;
  }
  if (pitch_correction_type_ < 0 || pitch_correction_type_ > 1) {
    return fill_zero(), ErrorCode::kInvalidPitchCorrectionType;
  }
  assert(static_cast<int>(formant_shift_embeddings_.size()) ==
         9 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  gain_.Process(input, output, n_samples, input_gain_context_);
  any_freq_in_out_(output, output, n_samples, *this);
  gain_.Process(output, output, n_samples, output_gain_context_);
  return ErrorCode::kSuccess;
}

void ProcessorCore0::Process1(const float* const input, float* const output) {
  std::array<float, BEATRICE_PHONE_CHANNELS> phone;
  Beatrice20a2_ExtractPhone1(phone_extractor_, input, phone.data(),
                             phone_context_);
  int quantized_pitch;
  std::array<float, 4> pitch_feature;
  Beatrice20a2_EstimatePitch1(pitch_estimator_, input, &quantized_pitch,
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
                 BEATRICE_PITCH_BINS - 1);
  std::array<float, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS> speaker;
  if (target_speaker_ == n_speakers_) {
    if (!sph_avg_.Update()) {
      sph_avg_.ApplyWeights(
          n_speakers_, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
          speaker_embeddings_.data(),
          &speaker_embeddings_[n_speakers_ *
                               BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS]);
    }
  }
  std::memcpy(speaker.data(),
              &speaker_embeddings_[target_speaker_ *
                                   BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS],
              sizeof(float) * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  for (auto i = 0; i < BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS; ++i) {
    speaker[i] += formant_shift_embeddings_
        [static_cast<int>(std::round(formant_shift_ * 2 + 4)) *
             BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS +
         i];
  }
  Beatrice20a2_GenerateWaveform1(waveform_generator_, phone.data(),
                                 &quantized_pitch, pitch_feature.data(),
                                 speaker.data(), output, waveform_context_);
}

auto ProcessorCore0::ResetContext() -> ErrorCode {
  Beatrice20a2_DestroyPhoneContext1(phone_context_);
  Beatrice20a2_DestroyPitchContext1(pitch_context_);
  Beatrice20a2_DestroyWaveformContext1(waveform_context_);
  phone_context_ = Beatrice20a2_CreatePhoneContext1();
  pitch_context_ = Beatrice20a2_CreatePitchContext1();
  waveform_context_ = Beatrice20a2_CreateWaveformContext1();
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::LoadModel(const ModelConfig& /*config*/,
                               const std::filesystem::path& new_model_file)
    -> ErrorCode {
  model_file_.clear();  // IsLoaded() が false を返すようにする

  const auto d = new_model_file.parent_path();
  if (const auto err = Beatrice20a2_ReadPhoneExtractorParameters(
          phone_extractor_,
          reinterpret_cast<const char*>(
              (d / "phone_extractor.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20a2_ReadPitchEstimatorParameters(
          pitch_estimator_,
          reinterpret_cast<const char*>(
              (d / "pitch_estimator.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20a2_ReadWaveformGeneratorParameters(
          waveform_generator_,
          reinterpret_cast<const char*>(
              (d / "waveform_generator.bin").u8string().c_str()))) {
    return static_cast<ErrorCode>(err);
  }
  if (const auto err = Beatrice20a2_ReadNSpeakers(
          reinterpret_cast<const char*>(
              (d / "speaker_embeddings.bin").u8string().c_str()),
          &n_speakers_)) {
    return static_cast<ErrorCode>(err);
  }
  speaker_embeddings_.resize(
      (n_speakers_ + 1) * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS, 0.0f);
  if (const auto err = Beatrice20a2_ReadSpeakerEmbeddings(
          reinterpret_cast<const char*>(
              (d / "speaker_embeddings.bin").u8string().c_str()),
          speaker_embeddings_.data())) {
    return static_cast<ErrorCode>(err);
  }
  sph_avg_.Initialize(n_speakers_, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
                      speaker_embeddings_.data());

  formant_shift_embeddings_.resize(9 *
                                   BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
  if (const auto err = Beatrice20a2_ReadSpeakerEmbeddings(
          reinterpret_cast<const char*>(
              (d / "formant_shift_embeddings.bin").u8string().c_str()),
          formant_shift_embeddings_.data())) {
    return static_cast<ErrorCode>(err);
  }

  model_file_ = new_model_file;

  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetSampleRate(const double new_sample_rate) -> ErrorCode {
  if (new_sample_rate == any_freq_in_out_.GetSampleRate()) {
    return ErrorCode::kSuccess;
  }
  any_freq_in_out_.SetSampleRate(new_sample_rate);
  input_gain_context_.SetSampleRate(new_sample_rate);
  output_gain_context_.SetSampleRate(new_sample_rate);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetTargetSpeaker(const int new_target_speaker_id)
    -> ErrorCode {
  if (new_target_speaker_id < 0) {
    return ErrorCode::kSpeakerIDOutOfRange;
  }
  target_speaker_ = new_target_speaker_id;
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetFormantShift(const double new_formant_shift)
    -> ErrorCode {
  formant_shift_ = std::clamp(new_formant_shift, -2.0, 2.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetPitchShift(const double new_pitch_shift) -> ErrorCode {
  pitch_shift_ = std::clamp(new_pitch_shift, -24.0, 24.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetInputGain(const double new_input_gain) -> ErrorCode {
  input_gain_context_.SetTargetGain(new_input_gain);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetOutputGain(const double new_output_gain) -> ErrorCode {
  output_gain_context_.SetTargetGain(new_output_gain);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetSpeakerMorphingWeight(int target_speaker_id,
                                              double morphing_weight)
    -> ErrorCode {
  if (target_speaker_id < 0 || target_speaker_id >= kMaxNSpeakers) {
    return ErrorCode::kSpeakerIDOutOfRange;
  }
  speaker_morphing_weights_[target_speaker_id] = morphing_weight;
  sph_avg_.SetWeights(n_speakers_, speaker_morphing_weights_.data());
  sph_avg_.ApplyWeights(
      n_speakers_, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
      speaker_embeddings_.data(),
      &speaker_embeddings_[n_speakers_ *
                           BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS]);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetAverageSourcePitch(const double new_average_pitch)
    -> ErrorCode {
  average_source_pitch_ = std::clamp(new_average_pitch, 0.0, 128.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetIntonationIntensity(
    const double new_intonation_intensity) -> ErrorCode {
  intonation_intensity_ = new_intonation_intensity;
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetPitchCorrection(const double new_pitch_correction)
    -> ErrorCode {
  pitch_correction_ = std::clamp(new_pitch_correction, 0.0, 1.0);
  return ErrorCode::kSuccess;
}

auto ProcessorCore0::SetPitchCorrectionType(const int new_pitch_correction_type)
    -> ErrorCode {
  if (new_pitch_correction_type < 0 || new_pitch_correction_type > 1) {
    return ErrorCode::kInvalidPitchCorrectionType;
  }
  pitch_correction_type_ = new_pitch_correction_type;
  return ErrorCode::kSuccess;
}

}  // namespace beatrice::common
