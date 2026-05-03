// Copyright (c) 2026 Project Beatrice and Contributors

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "beatricelib/beatrice.h"
#include "common/error.h"
#include "common/parameter_schema.h"
#include "common/processor_proxy.h"
#include "common/resample.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::filesystem::path model_path;
  double sample_rate = 48000.0;
  int block_size = 480;
  int iterations = 1000;
  int warmup = 20;
  std::string mode = "all";
};

struct Summary {
  double avg_ms = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
};

void PrintUsage(const char* const argv0) {
  std::cerr
      << "Usage: " << argv0
      << " /path/to/model.toml [--sample-rate 48000] [--block-size 480]\n"
      << "       [--iterations 1000] [--warmup 20]\n"
      << "       [--mode all|full|runtime|resampler]\n";
}

auto ParseOptions(const int argc, char** argv, Options& options) -> bool {
  if (argc < 2) {
    return false;
  }
  options.model_path = argv[1];
  for (int i = 2; i < argc; ++i) {
    const auto arg = std::string_view(argv[i]);
    const auto need_value = [&](const char* const name) -> const char* {
      if (++i >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[i];
    };
    if (arg == "--sample-rate") {
      const char* const value = need_value("--sample-rate");
      if (value == nullptr) return false;
      options.sample_rate = std::stod(value);
    } else if (arg == "--block-size") {
      const char* const value = need_value("--block-size");
      if (value == nullptr) return false;
      options.block_size = std::stoi(value);
    } else if (arg == "--iterations") {
      const char* const value = need_value("--iterations");
      if (value == nullptr) return false;
      options.iterations = std::stoi(value);
    } else if (arg == "--warmup") {
      const char* const value = need_value("--warmup");
      if (value == nullptr) return false;
      options.warmup = std::stoi(value);
    } else if (arg == "--mode") {
      const char* const value = need_value("--mode");
      if (value == nullptr) return false;
      options.mode = value;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
  }
  return !options.model_path.empty() && options.sample_rate > 0.0 &&
         options.block_size > 0 && options.iterations > 0 &&
         options.warmup >= 0 &&
         (options.mode == "all" || options.mode == "full" ||
          options.mode == "runtime" || options.mode == "resampler");
}

auto Summarize(std::vector<double> samples_ms) -> Summary {
  Summary summary;
  if (samples_ms.empty()) {
    return summary;
  }
  summary.avg_ms =
      std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0) /
      static_cast<double>(samples_ms.size());
  std::sort(samples_ms.begin(), samples_ms.end());
  const auto pick = [&](const double percentile) {
    const auto idx = static_cast<size_t>(
        std::clamp(percentile, 0.0, 1.0) *
        static_cast<double>(samples_ms.size() - 1));
    return samples_ms[idx];
  };
  summary.p50_ms = pick(0.50);
  summary.p95_ms = pick(0.95);
  summary.p99_ms = pick(0.99);
  summary.max_ms = samples_ms.back();
  return summary;
}

void PrintSummary(const std::string_view name, const Summary& summary,
                  const double budget_ms) {
  std::cout << name << ": avg=" << summary.avg_ms << "ms"
            << " p50=" << summary.p50_ms << "ms"
            << " p95=" << summary.p95_ms << "ms"
            << " p99=" << summary.p99_ms << "ms"
            << " max=" << summary.max_ms << "ms"
            << " budget=" << budget_ms << "ms"
            << " rt_ratio=" << (summary.avg_ms / budget_ms) << "\n";
}

void FillInput(std::vector<float>& input, const double sample_rate) {
  constexpr double kPi = 3.14159265358979323846264338327950288;
  for (size_t i = 0; i < input.size(); ++i) {
    const auto t = static_cast<double>(i) / sample_rate;
    input[i] = static_cast<float>(0.08 * std::sin(2.0 * kPi * 220.0 * t) +
                                  0.02 * std::sin(2.0 * kPi * 997.0 * t));
  }
}

template <class Fn>
auto MeasureMs(Fn&& fn) -> double {
  const auto start = Clock::now();
  fn();
  const auto end = Clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

class RuntimeModel {
 public:
  RuntimeModel() = default;
  RuntimeModel(const RuntimeModel&) = delete;
  auto operator=(const RuntimeModel&) -> RuntimeModel& = delete;

  ~RuntimeModel() {
    if (embedding_context_ != nullptr) {
      Beatrice20rc0_DestroyEmbeddingContext(embedding_context_);
    }
    if (waveform_context_ != nullptr) {
      Beatrice20rc0_DestroyWaveformContext1(waveform_context_);
    }
    if (pitch_context_ != nullptr) {
      Beatrice20rc0_DestroyPitchContext1(pitch_context_);
    }
    if (phone_context_ != nullptr) {
      Beatrice20rc0_DestroyPhoneContext1(phone_context_);
    }
    if (embedding_setter_ != nullptr) {
      Beatrice20rc0_DestroyEmbeddingSetter(embedding_setter_);
    }
    if (waveform_generator_ != nullptr) {
      Beatrice20rc0_DestroyWaveformGenerator(waveform_generator_);
    }
    if (pitch_estimator_ != nullptr) {
      Beatrice20rc0_DestroyPitchEstimator(pitch_estimator_);
    }
    if (phone_extractor_ != nullptr) {
      Beatrice20rc0_DestroyPhoneExtractor(phone_extractor_);
    }
  }

  auto Load(const std::filesystem::path& model_path) -> bool {
    phone_extractor_ = Beatrice20rc0_CreatePhoneExtractor();
    pitch_estimator_ = Beatrice20rc0_CreatePitchEstimator();
    waveform_generator_ = Beatrice20rc0_CreateWaveformGenerator();
    embedding_setter_ = Beatrice20rc0_CreateEmbeddingSetter();
    phone_context_ = Beatrice20rc0_CreatePhoneContext1();
    pitch_context_ = Beatrice20rc0_CreatePitchContext1();
    waveform_context_ = Beatrice20rc0_CreateWaveformContext1();
    embedding_context_ = Beatrice20rc0_CreateEmbeddingContext();
    if (phone_extractor_ == nullptr || pitch_estimator_ == nullptr ||
        waveform_generator_ == nullptr || embedding_setter_ == nullptr ||
        phone_context_ == nullptr || pitch_context_ == nullptr ||
        waveform_context_ == nullptr || embedding_context_ == nullptr) {
      std::cerr << "Runtime create failed\n";
      return false;
    }

    const auto dir = model_path.parent_path();
    const auto as_utf8 = [](const std::filesystem::path& path) {
      return path.u8string();
    };
    const auto phone_bin = as_utf8(dir / "phone_extractor.bin");
    const auto pitch_bin = as_utf8(dir / "pitch_estimator.bin");
    const auto waveform_bin = as_utf8(dir / "waveform_generator.bin");
    const auto embedding_bin = as_utf8(dir / "embedding_setter.bin");
    const auto speaker_bin = as_utf8(dir / "speaker_embeddings.bin");

    const auto cstr = [](const std::u8string& value) {
      return reinterpret_cast<const char*>(value.c_str());
    };
    if (Beatrice20rc0_ReadPhoneExtractorParameters(phone_extractor_,
                                                   cstr(phone_bin)) !=
        Beatrice_kSuccess) {
      std::cerr << "ReadPhoneExtractorParameters failed\n";
      return false;
    }
    if (Beatrice20rc0_ReadPitchEstimatorParameters(pitch_estimator_,
                                                   cstr(pitch_bin)) !=
        Beatrice_kSuccess) {
      std::cerr << "ReadPitchEstimatorParameters failed\n";
      return false;
    }
    if (Beatrice20rc0_ReadWaveformGeneratorParameters(waveform_generator_,
                                                      cstr(waveform_bin)) !=
        Beatrice_kSuccess) {
      std::cerr << "ReadWaveformGeneratorParameters failed\n";
      return false;
    }
    if (Beatrice20rc0_ReadEmbeddingSetterParameters(embedding_setter_,
                                                    cstr(embedding_bin)) !=
        Beatrice_kSuccess) {
      std::cerr << "ReadEmbeddingSetterParameters failed\n";
      return false;
    }
    if (Beatrice20rc0_ReadNSpeakers(cstr(speaker_bin), &n_speakers_) !=
        Beatrice_kSuccess) {
      std::cerr << "ReadNSpeakers failed\n";
      return false;
    }

    codebooks_.resize((n_speakers_ + 1) * BEATRICE_20RC0_CODEBOOK_SIZE *
                      BEATRICE_20RC0_PHONE_CHANNELS);
    additive_speaker_embeddings_.resize(
        (n_speakers_ + 1) * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
    formant_shift_embeddings_.resize(
        9 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS);
    key_value_speaker_embeddings_.resize(
        (n_speakers_ + 1) * BEATRICE_20RC0_KV_LENGTH *
        BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS);
    if (Beatrice20rc0_ReadSpeakerEmbeddings(
            cstr(speaker_bin), codebooks_.data(),
            additive_speaker_embeddings_.data(),
            formant_shift_embeddings_.data(),
            key_value_speaker_embeddings_.data()) != Beatrice_kSuccess) {
      std::cerr << "ReadSpeakerEmbeddings failed\n";
      return false;
    }

    Beatrice20rc0_SetCodebook(phone_context_, codebooks_.data());
    Beatrice20rc0_SetAdditiveSpeakerEmbedding(
        embedding_setter_, additive_speaker_embeddings_.data(),
        embedding_context_, waveform_context_);
    Beatrice20rc0_SetFormantShiftEmbedding(
        embedding_setter_,
        formant_shift_embeddings_.data() +
            4 * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS,
        embedding_context_, waveform_context_);
    Beatrice20rc0_RegisterKeyValueSpeakerEmbedding(
        embedding_setter_, key_value_speaker_embeddings_.data(),
        embedding_context_);
    for (int block = 0; block < BEATRICE_20RC0_N_BLOCKS; ++block) {
      Beatrice20rc0_SetKeyValueSpeakerEmbedding(
          embedding_setter_, block, embedding_context_, waveform_context_);
    }
    return true;
  }

  void Process(const float* const input, float* const output,
               double* const phone_ms, double* const pitch_ms,
               double* const waveform_ms) {
    *phone_ms = MeasureMs([&] {
      Beatrice20rc0_ExtractPhone1(phone_extractor_, input, phone_.data(),
                                  phone_context_);
    });
    *pitch_ms = MeasureMs([&] {
      Beatrice20rc0_EstimatePitch1(pitch_estimator_, input, &quantized_pitch_,
                                   pitch_feature_.data(), pitch_context_);
    });
    *waveform_ms = MeasureMs([&] {
      Beatrice20rc0_GenerateWaveform1(
          waveform_generator_, phone_.data(), &quantized_pitch_,
          pitch_feature_.data(), output, waveform_context_);
    });
  }

 private:
  Beatrice20rc0_PhoneExtractor* phone_extractor_ = nullptr;
  Beatrice20rc0_PitchEstimator* pitch_estimator_ = nullptr;
  Beatrice20rc0_WaveformGenerator* waveform_generator_ = nullptr;
  Beatrice20rc0_EmbeddingSetter* embedding_setter_ = nullptr;
  Beatrice20rc0_PhoneContext1* phone_context_ = nullptr;
  Beatrice20rc0_PitchContext1* pitch_context_ = nullptr;
  Beatrice20rc0_WaveformContext1* waveform_context_ = nullptr;
  Beatrice20rc0_EmbeddingContext* embedding_context_ = nullptr;
  int n_speakers_ = 0;
  std::vector<float> codebooks_;
  std::vector<float> additive_speaker_embeddings_;
  std::vector<float> formant_shift_embeddings_;
  std::vector<float> key_value_speaker_embeddings_;
  std::array<float, BEATRICE_20RC0_PHONE_CHANNELS> phone_{};
  std::array<float, 4> pitch_feature_{};
  int quantized_pitch_ = 0;
};

auto RunFullProcessorBench(const Options& options) -> bool {
  beatrice::common::ProcessorProxy processor(beatrice::common::kSchema);
  if (const auto err = processor.SetSampleRate(options.sample_rate);
      err != beatrice::common::ErrorCode::kSuccess) {
    std::cerr << "SetSampleRate failed: " << static_cast<int>(err) << "\n";
    return false;
  }
  if (const auto err = processor.LoadModel(options.model_path);
      err != beatrice::common::ErrorCode::kSuccess) {
    std::cerr << "LoadModel failed: " << static_cast<int>(err) << "\n";
    return false;
  }

  std::vector<float> input(options.block_size);
  std::vector<float> buffer(options.block_size);
  FillInput(input, options.sample_rate);

  for (int i = 0; i < options.warmup; ++i) {
    std::copy(input.begin(), input.end(), buffer.begin());
    [[maybe_unused]] const auto err =
        processor.GetCore()->Process(buffer.data(), buffer.data(),
                                     options.block_size);
  }

  std::vector<double> samples_ms;
  samples_ms.reserve(options.iterations);
  for (int i = 0; i < options.iterations; ++i) {
    std::copy(input.begin(), input.end(), buffer.begin());
    samples_ms.push_back(MeasureMs([&] {
      [[maybe_unused]] const auto err =
          processor.GetCore()->Process(buffer.data(), buffer.data(),
                                       options.block_size);
    }));
  }

  const auto budget_ms =
      static_cast<double>(options.block_size) / options.sample_rate * 1000.0;
  PrintSummary("full_processor", Summarize(std::move(samples_ms)), budget_ms);
  return true;
}

struct FakeModelBlock {
  void operator()(const float* const input, float* const output) const {
    for (int i = 0; i < BEATRICE_OUT_HOP_LENGTH; ++i) {
      output[i] =
          input[std::min(BEATRICE_IN_HOP_LENGTH - 1,
                         i * BEATRICE_IN_HOP_LENGTH / BEATRICE_OUT_HOP_LENGTH)];
    }
  }
};

auto RunResamplerBench(const Options& options) -> bool {
  beatrice::resampler::AnyFreqInOut<FakeModelBlock> processor(
      options.sample_rate);
  if (!processor.IsReady()) {
    std::cerr << "Resampler is not ready\n";
    return false;
  }

  std::vector<float> input(options.block_size);
  std::vector<float> output(options.block_size);
  FillInput(input, options.sample_rate);

  for (int i = 0; i < options.warmup; ++i) {
    processor(input.data(), output.data(), options.block_size);
  }

  std::vector<double> samples_ms;
  samples_ms.reserve(options.iterations);
  for (int i = 0; i < options.iterations; ++i) {
    samples_ms.push_back(MeasureMs([&] {
      processor(input.data(), output.data(), options.block_size);
    }));
  }

  const auto budget_ms =
      static_cast<double>(options.block_size) / options.sample_rate * 1000.0;
  PrintSummary("resampler_adapter", Summarize(std::move(samples_ms)),
               budget_ms);
  return true;
}

auto RunRuntimeBench(const Options& options) -> bool {
  RuntimeModel runtime;
  if (!runtime.Load(options.model_path)) {
    return false;
  }

  std::array<float, BEATRICE_IN_HOP_LENGTH> input{};
  std::array<float, BEATRICE_OUT_HOP_LENGTH> output{};
  std::vector<float> input_vector(input.size());
  FillInput(input_vector, 16000.0);
  std::copy(input_vector.begin(), input_vector.end(), input.begin());

  for (int i = 0; i < options.warmup; ++i) {
    double phone_ms = 0.0;
    double pitch_ms = 0.0;
    double waveform_ms = 0.0;
    runtime.Process(input.data(), output.data(), &phone_ms, &pitch_ms,
                    &waveform_ms);
  }

  std::vector<double> phone_samples_ms;
  std::vector<double> pitch_samples_ms;
  std::vector<double> waveform_samples_ms;
  std::vector<double> total_samples_ms;
  phone_samples_ms.reserve(options.iterations);
  pitch_samples_ms.reserve(options.iterations);
  waveform_samples_ms.reserve(options.iterations);
  total_samples_ms.reserve(options.iterations);
  for (int i = 0; i < options.iterations; ++i) {
    double phone_ms = 0.0;
    double pitch_ms = 0.0;
    double waveform_ms = 0.0;
    runtime.Process(input.data(), output.data(), &phone_ms, &pitch_ms,
                    &waveform_ms);
    phone_samples_ms.push_back(phone_ms);
    pitch_samples_ms.push_back(pitch_ms);
    waveform_samples_ms.push_back(waveform_ms);
    total_samples_ms.push_back(phone_ms + pitch_ms + waveform_ms);
  }

  constexpr double kRuntimeBudgetMs = 10.0;
  PrintSummary("runtime_phone", Summarize(std::move(phone_samples_ms)),
               kRuntimeBudgetMs);
  PrintSummary("runtime_pitch", Summarize(std::move(pitch_samples_ms)),
               kRuntimeBudgetMs);
  PrintSummary("runtime_waveform", Summarize(std::move(waveform_samples_ms)),
               kRuntimeBudgetMs);
  PrintSummary("runtime_total", Summarize(std::move(total_samples_ms)),
               kRuntimeBudgetMs);
  return true;
}

}  // namespace

auto main(const int argc, char** argv) -> int {
  Options options;
  if (!ParseOptions(argc, argv, options)) {
    PrintUsage(argv[0]);
    return 64;
  }
  if (!std::filesystem::exists(options.model_path)) {
    std::cerr << "Model file does not exist: " << options.model_path << "\n";
    return 66;
  }

  std::cout << "model=" << options.model_path << "\n"
            << "sample_rate=" << options.sample_rate << "\n"
            << "block_size=" << options.block_size << "\n"
            << "iterations=" << options.iterations << "\n"
            << "warmup=" << options.warmup << "\n";

  if (options.mode == "all" || options.mode == "runtime") {
    if (!RunRuntimeBench(options)) {
      return 1;
    }
  }
  if (options.mode == "all" || options.mode == "resampler") {
    if (!RunResamplerBench(options)) {
      return 1;
    }
  }
  if (options.mode == "all" || options.mode == "full") {
    if (!RunFullProcessorBench(options)) {
      return 1;
    }
  }
  return 0;
}
