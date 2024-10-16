// Copyright (c) 2024 Project Beatrice

#include "common/processor_core_1.h"

#include <cassert>
#include <cstring>

namespace beatrice::common {

auto ProcessorCore1::GetVersion() const -> int { return 1; }
auto ProcessorCore1::Process(const float* const input, float* const output,
                             const int n_samples) -> int {
  if (!IsLoaded()) {
    goto fail;
  }
  if (!any_freq_in_out_.IsReady()) {
    goto fail;
  }
  if (!input_gain_context_.IsReady()) {
    goto fail;
  }
  if (!output_gain_context_.IsReady()) {
    goto fail;
  }
  if (target_speaker_ < 0) {
    goto fail;
  }
  if (target_speaker_ >= static_cast<int>(speaker_embeddings_.size()) /
                             BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS) {
    goto fail;
  }
  if (static_cast<int>(formant_shift_embeddings_.size()) !=
      9 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS) {
    goto fail;
  }
  gain_.Process(input, output, n_samples, input_gain_context_);
  any_freq_in_out_(output, output, n_samples, *this);
  gain_.Process(output, output, n_samples, output_gain_context_);
  return 0;
fail:
  std::memset(output, 0, sizeof(float) * n_samples);
  return 1;
}

void ProcessorCore1::Process1(const float* const input, float* const output) {
  std::array<float, BEATRICE_PHONE_CHANNELS> phone;
  Beatrice20b1_ExtractPhone1(phone_extractor_, input, phone.data(),
                             phone_context_);
  int quantized_pitch;
  std::array<float, 4> pitch_feature;
  Beatrice20b1_EstimatePitch1(pitch_estimator_, input, &quantized_pitch,
                              pitch_feature.data(), pitch_context_);
  quantized_pitch += static_cast<int>(
      std::round(BEATRICE_PITCH_BINS_PER_OCTAVE * pitch_shift_ / 12.0));
  quantized_pitch = std::clamp(quantized_pitch, 1, BEATRICE_PITCH_BINS - 1);
  std::array<float, BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS> speaker;
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
  Beatrice20b1_GenerateWaveform1(waveform_generator_, phone.data(),
                                 &quantized_pitch, pitch_feature.data(),
                                 speaker.data(), output, waveform_context_);
}

auto ProcessorCore1::ResetContext() -> int {
  Beatrice20b1_DestroyPhoneContext1(phone_context_);
  Beatrice20b1_DestroyPitchContext1(pitch_context_);
  Beatrice20b1_DestroyWaveformContext1(waveform_context_);
  phone_context_ = Beatrice20b1_CreatePhoneContext1();
  pitch_context_ = Beatrice20b1_CreatePitchContext1();
  waveform_context_ = Beatrice20b1_CreateWaveformContext1();
  return 0;
}

auto ProcessorCore1::LoadModel(const ModelConfig& /*config*/,
                               const std::filesystem::path& new_model_file)
    -> int {
  try {
    const auto d = new_model_file.parent_path();
    if (const auto err = Beatrice20b1_ReadPhoneExtractorParameters(
            phone_extractor_,
            reinterpret_cast<const char*>(
                (d / "phone_extractor.bin").u8string().c_str()))) {
      return static_cast<int>(err);
    }
    if (const auto err = Beatrice20b1_ReadPitchEstimatorParameters(
            pitch_estimator_,
            reinterpret_cast<const char*>(
                (d / "pitch_estimator.bin").u8string().c_str()))) {
      return static_cast<int>(err);
    }
    if (const auto err = Beatrice20b1_ReadWaveformGeneratorParameters(
            waveform_generator_,
            reinterpret_cast<const char*>(
                (d / "waveform_generator.bin").u8string().c_str()))) {
      return static_cast<int>(err);
    }
    int n_speakers;
    if (const auto err = Beatrice20b1_ReadNSpeakers(
            reinterpret_cast<const char*>(
                (d / "speaker_embeddings.bin").u8string().c_str()),
            &n_speakers)) {
      return static_cast<int>(err);
    }
    speaker_embeddings_.resize(n_speakers *
                               BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
    if (const auto err = Beatrice20b1_ReadSpeakerEmbeddings(
            reinterpret_cast<const char*>(
                (d / "speaker_embeddings.bin").u8string().c_str()),
            speaker_embeddings_.data())) {
      return static_cast<int>(err);
    }
    formant_shift_embeddings_.resize(
        9 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
    if (const auto err = Beatrice20b1_ReadSpeakerEmbeddings(
            reinterpret_cast<const char*>(
                (d / "formant_shift_embeddings.bin").u8string().c_str()),
            formant_shift_embeddings_.data())) {
      return static_cast<int>(err);
    }
    model_file_ = new_model_file;
  } catch (const std::exception& e) {
    return 1;
  }
  return 0;
}

auto ProcessorCore1::SetSampleRate(const double new_sample_rate) -> int {
  if (new_sample_rate == any_freq_in_out_.GetSampleRate()) {
    return 0;
  }
  any_freq_in_out_.SetSampleRate(new_sample_rate);
  input_gain_context_.SetSampleRate(new_sample_rate);
  output_gain_context_.SetSampleRate(new_sample_rate);
  return 0;
}

auto ProcessorCore1::SetTargetSpeaker(const int new_target_speaker_id) -> int {
  if (new_target_speaker_id < 0) {
    return 1;
  }
  target_speaker_ = new_target_speaker_id;
  return 0;
}

auto ProcessorCore1::SetFormantShift(const double new_formant_shift) -> int {
  formant_shift_ = std::clamp(new_formant_shift, -2.0, 2.0);
  return 0;
}

auto ProcessorCore1::SetPitchShift(const double new_pitch_shift) -> int {
  pitch_shift_ = std::clamp(new_pitch_shift, -24.0, 24.0);
  return 0;
}

auto ProcessorCore1::SetInputGain(const double new_input_gain) -> int {
  input_gain_context_.SetTargetGain(new_input_gain);
  return 0;
}

auto ProcessorCore1::SetOutputGain(const double new_output_gain) -> int {
  output_gain_context_.SetTargetGain(new_output_gain);
  return 0;
}
}  // namespace beatrice::common
