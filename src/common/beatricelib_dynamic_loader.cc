// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "beatricelib/beatrice.h"

#ifndef __APPLE__
#error "beatricelib_dynamic_loader.cc is only intended for macOS builds"
#endif

#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#ifndef BEATRICE_MAC_RUNTIME_FILENAME
#define BEATRICE_MAC_RUNTIME_FILENAME "beatrice_v20rc0.abi3.so"
#endif

#ifndef BEATRICE_MAC_PYTHON_RUNTIME
#define BEATRICE_MAC_PYTHON_RUNTIME ""
#endif

#ifndef BEATRICE_ENABLE_LOADER_LOG
#define BEATRICE_ENABLE_LOADER_LOG 0
#endif

namespace {

void Log(const char* const format, ...) {
#if BEATRICE_ENABLE_LOADER_LOG
  if (FILE* const file = std::fopen("/tmp/beatrice-vst-loader.log", "a")) {
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
  }
#else
  (void)format;
#endif
}

[[noreturn]] void Fail(const std::string& message) {
  Log("fatal: %s", message.c_str());
  std::fprintf(stderr, "Beatrice runtime loader error: %s\n", message.c_str());
  std::abort();
}

bool TryOpen(const char* path, const int flags, void** handle,
             std::string& last_error) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  Log("dlopen try flags=0x%x path=%s", flags, path);
  *handle = dlopen(path, flags);
  if (*handle != nullptr) {
    Log("dlopen ok path=%s handle=%p", path, *handle);
    return true;
  }
  if (const char* error = dlerror()) {
    last_error = error;
    Log("dlopen fail path=%s error=%s", path, error);
  } else {
    Log("dlopen fail path=%s without dlerror", path);
  }
  return false;
}

bool LooksLikePythonSymbolError(const std::string_view error) {
  return error.find("_Py") != std::string_view::npos ||
         error.find("Py") != std::string_view::npos;
}

void* OpenPythonRuntime() {
  const char* env_path = std::getenv("BEATRICE_MAC_PYTHON_RUNTIME");
  const char* candidates[] = {
      env_path,
      BEATRICE_MAC_PYTHON_RUNTIME,
      "@loader_path/../Resources/python/Python.framework/Python",
      "@loader_path/../Resources/beatrice/Python.framework/Python",
      "/opt/homebrew/Frameworks/Python.framework/Python",
      "/Library/Frameworks/Python.framework/Python",
      "/usr/local/Frameworks/Python.framework/Python",
  };

  std::string last_error;
  void* handle = nullptr;
  Log("opening Python runtime");
  for (const char* path : candidates) {
    if (TryOpen(path, RTLD_NOW | RTLD_GLOBAL, &handle, last_error)) {
      return handle;
    }
  }
  return nullptr;
}

void* OpenRuntime() {
  Log("opening Beatrice runtime filename=%s", BEATRICE_MAC_RUNTIME_FILENAME);
  std::string loader_path =
      std::string("@loader_path/../Resources/beatrice/") +
      BEATRICE_MAC_RUNTIME_FILENAME;
  std::string sibling_path =
      std::string("@loader_path/") + BEATRICE_MAC_RUNTIME_FILENAME;

  const char* env_path = std::getenv("BEATRICE_MAC_RUNTIME");
  const char* candidates[] = {
      env_path,
      loader_path.c_str(),
      sibling_path.c_str(),
  };

  auto try_open_beatrice = [&candidates](std::string& last_error,
                                         bool* python_symbol_error) -> void* {
    void* handle = nullptr;
    for (const char* path : candidates) {
      if (TryOpen(path, RTLD_NOW | RTLD_LOCAL, &handle, last_error)) {
        return handle;
      }
      if (LooksLikePythonSymbolError(last_error)) {
        *python_symbol_error = true;
      }
    }
    return nullptr;
  };

  std::string last_error;
  bool python_symbol_error = false;
  if (void* handle = try_open_beatrice(last_error, &python_symbol_error)) {
    return handle;
  }

  Log("initial Beatrice load failed, python_symbol_error=%d last_error=%s",
      python_symbol_error, last_error.c_str());
  if (python_symbol_error) {
    if (OpenPythonRuntime() != nullptr) {
      last_error.clear();
      python_symbol_error = false;
      if (void* handle = try_open_beatrice(last_error, &python_symbol_error)) {
        return handle;
      }
    }
  }

  Fail(std::string("failed to load ") + BEATRICE_MAC_RUNTIME_FILENAME +
       (last_error.empty() ? std::string() : ": " + last_error));
}

void* Runtime() {
  static void* handle = OpenRuntime();
  return handle;
}

void* Resolve(const char* name) {
  dlerror();
  void* symbol = dlsym(Runtime(), name);
  if (symbol == nullptr) {
    const char* error = dlerror();
    Fail(std::string("missing symbol ") + name +
         (error == nullptr ? std::string() : std::string(": ") + error));
  }
  return symbol;
}

}  // namespace

#define BEATRICE_FORWARD(return_type, name, args, call_args) \
  return_type name args {                                  \
    using Fn = return_type(*) args;                         \
    static auto fn = reinterpret_cast<Fn>(Resolve(#name));  \
    return fn call_args;                                    \
  }

extern "C" {

// -------- 20a2 --------

BEATRICE_FORWARD(Beatrice20a2_PhoneExtractor*,
                 Beatrice20a2_CreatePhoneExtractor, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyPhoneExtractor,
                 (Beatrice20a2_PhoneExtractor * phone_extractor),
                 (phone_extractor))
BEATRICE_FORWARD(Beatrice20a2_PhoneContext1*,
                 Beatrice20a2_CreatePhoneContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyPhoneContext1,
                 (Beatrice20a2_PhoneContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20a2_ReadPhoneExtractorParameters,
                 (Beatrice20a2_PhoneExtractor * phone_extractor,
                  const char* filename),
                 (phone_extractor, filename))
BEATRICE_FORWARD(void, Beatrice20a2_ExtractPhone1,
                 (const Beatrice20a2_PhoneExtractor* phone_extractor,
                  const float* input, float* output,
                  Beatrice20a2_PhoneContext1* ctx),
                 (phone_extractor, input, output, ctx))
BEATRICE_FORWARD(Beatrice20a2_PitchEstimator*,
                 Beatrice20a2_CreatePitchEstimator, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyPitchEstimator,
                 (Beatrice20a2_PitchEstimator * pitch_estimator),
                 (pitch_estimator))
BEATRICE_FORWARD(Beatrice20a2_PitchContext1*,
                 Beatrice20a2_CreatePitchContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyPitchContext1,
                 (Beatrice20a2_PitchContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20a2_ReadPitchEstimatorParameters,
                 (Beatrice20a2_PitchEstimator * pitch_estimator,
                  const char* filename),
                 (pitch_estimator, filename))
BEATRICE_FORWARD(void, Beatrice20a2_SetMinQuantizedPitch,
                 (Beatrice20a2_PitchContext1 * ctx, int min_quantized_pitch),
                 (ctx, min_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20a2_SetMaxQuantizedPitch,
                 (Beatrice20a2_PitchContext1 * ctx, int max_quantized_pitch),
                 (ctx, max_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20a2_EstimatePitch1,
                 (const Beatrice20a2_PitchEstimator* pitch_estimator,
                  const float* input, int* output_quantized_pitch,
                  float* output_pitch_feature, Beatrice20a2_PitchContext1* ctx),
                 (pitch_estimator, input, output_quantized_pitch,
                  output_pitch_feature, ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20a2_ReadNSpeakers,
                 (const char* filename, int* output), (filename, output))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20a2_ReadSpeakerEmbeddings,
                 (const char* filename, float* output), (filename, output))
BEATRICE_FORWARD(Beatrice20a2_WaveformGenerator*,
                 Beatrice20a2_CreateWaveformGenerator, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyWaveformGenerator,
                 (Beatrice20a2_WaveformGenerator * waveform_generator),
                 (waveform_generator))
BEATRICE_FORWARD(Beatrice20a2_WaveformContext1*,
                 Beatrice20a2_CreateWaveformContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20a2_DestroyWaveformContext1,
                 (Beatrice20a2_WaveformContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20a2_ReadWaveformGeneratorParameters,
                 (Beatrice20a2_WaveformGenerator * waveform_generator,
                  const char* filename),
                 (waveform_generator, filename))
BEATRICE_FORWARD(void, Beatrice20a2_GenerateWaveform1,
                 (const Beatrice20a2_WaveformGenerator* waveform_generator,
                  const float* input_phone, const int* input_quantized_pitch,
                  const float* input_pitch_features,
                  const float* input_speaker_embedding, float* output,
                  Beatrice20a2_WaveformContext1* ctx),
                 (waveform_generator, input_phone, input_quantized_pitch,
                  input_pitch_features, input_speaker_embedding, output, ctx))

// -------- 20b1 --------

BEATRICE_FORWARD(Beatrice20b1_PhoneExtractor*,
                 Beatrice20b1_CreatePhoneExtractor, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyPhoneExtractor,
                 (Beatrice20b1_PhoneExtractor * phone_extractor),
                 (phone_extractor))
BEATRICE_FORWARD(Beatrice20b1_PhoneContext1*,
                 Beatrice20b1_CreatePhoneContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyPhoneContext1,
                 (Beatrice20b1_PhoneContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20b1_ReadPhoneExtractorParameters,
                 (Beatrice20b1_PhoneExtractor * phone_extractor,
                  const char* filename),
                 (phone_extractor, filename))
BEATRICE_FORWARD(void, Beatrice20b1_ExtractPhone1,
                 (const Beatrice20b1_PhoneExtractor* phone_extractor,
                  const float* input, float* output,
                  Beatrice20b1_PhoneContext1* ctx),
                 (phone_extractor, input, output, ctx))
BEATRICE_FORWARD(Beatrice20b1_PitchEstimator*,
                 Beatrice20b1_CreatePitchEstimator, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyPitchEstimator,
                 (Beatrice20b1_PitchEstimator * pitch_estimator),
                 (pitch_estimator))
BEATRICE_FORWARD(Beatrice20b1_PitchContext1*,
                 Beatrice20b1_CreatePitchContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyPitchContext1,
                 (Beatrice20b1_PitchContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20b1_ReadPitchEstimatorParameters,
                 (Beatrice20b1_PitchEstimator * pitch_estimator,
                  const char* filename),
                 (pitch_estimator, filename))
BEATRICE_FORWARD(void, Beatrice20b1_SetMinQuantizedPitch,
                 (Beatrice20b1_PitchContext1 * ctx, int min_quantized_pitch),
                 (ctx, min_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20b1_SetMaxQuantizedPitch,
                 (Beatrice20b1_PitchContext1 * ctx, int max_quantized_pitch),
                 (ctx, max_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20b1_EstimatePitch1,
                 (const Beatrice20b1_PitchEstimator* pitch_estimator,
                  const float* input, int* output_quantized_pitch,
                  float* output_pitch_feature, Beatrice20b1_PitchContext1* ctx),
                 (pitch_estimator, input, output_quantized_pitch,
                  output_pitch_feature, ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20b1_ReadNSpeakers,
                 (const char* filename, int* output), (filename, output))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20b1_ReadSpeakerEmbeddings,
                 (const char* filename, float* output), (filename, output))
BEATRICE_FORWARD(Beatrice20b1_WaveformGenerator*,
                 Beatrice20b1_CreateWaveformGenerator, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyWaveformGenerator,
                 (Beatrice20b1_WaveformGenerator * waveform_generator),
                 (waveform_generator))
BEATRICE_FORWARD(Beatrice20b1_WaveformContext1*,
                 Beatrice20b1_CreateWaveformContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20b1_DestroyWaveformContext1,
                 (Beatrice20b1_WaveformContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20b1_ReadWaveformGeneratorParameters,
                 (Beatrice20b1_WaveformGenerator * waveform_generator,
                  const char* filename),
                 (waveform_generator, filename))
BEATRICE_FORWARD(void, Beatrice20b1_GenerateWaveform1,
                 (const Beatrice20b1_WaveformGenerator* waveform_generator,
                  const float* input_phone, const int* input_quantized_pitch,
                  const float* input_pitch_features,
                  const float* input_speaker_embedding, float* output,
                  Beatrice20b1_WaveformContext1* ctx),
                 (waveform_generator, input_phone, input_quantized_pitch,
                  input_pitch_features, input_speaker_embedding, output, ctx))

// -------- 20rc0 --------

BEATRICE_FORWARD(Beatrice20rc0_PhoneExtractor*,
                 Beatrice20rc0_CreatePhoneExtractor, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyPhoneExtractor,
                 (Beatrice20rc0_PhoneExtractor * phone_extractor),
                 (phone_extractor))
BEATRICE_FORWARD(Beatrice20rc0_PhoneContext1*,
                 Beatrice20rc0_CreatePhoneContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyPhoneContext1,
                 (Beatrice20rc0_PhoneContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20rc0_ReadPhoneExtractorParameters,
                 (Beatrice20rc0_PhoneExtractor * phone_extractor,
                  const char* filename),
                 (phone_extractor, filename))
BEATRICE_FORWARD(void, Beatrice20rc0_SetVQNumNeighbors,
                 (Beatrice20rc0_PhoneContext1 * ctx, int num_neighbors),
                 (ctx, num_neighbors))
BEATRICE_FORWARD(void, Beatrice20rc0_ExtractPhone1,
                 (const Beatrice20rc0_PhoneExtractor* phone_extractor,
                  const float* input, float* output,
                  Beatrice20rc0_PhoneContext1* ctx),
                 (phone_extractor, input, output, ctx))
BEATRICE_FORWARD(Beatrice20rc0_PitchEstimator*,
                 Beatrice20rc0_CreatePitchEstimator, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyPitchEstimator,
                 (Beatrice20rc0_PitchEstimator * pitch_estimator),
                 (pitch_estimator))
BEATRICE_FORWARD(Beatrice20rc0_PitchContext1*,
                 Beatrice20rc0_CreatePitchContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyPitchContext1,
                 (Beatrice20rc0_PitchContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20rc0_ReadPitchEstimatorParameters,
                 (Beatrice20rc0_PitchEstimator * pitch_estimator,
                  const char* filename),
                 (pitch_estimator, filename))
BEATRICE_FORWARD(void, Beatrice20rc0_SetMinQuantizedPitch,
                 (Beatrice20rc0_PitchContext1 * ctx, int min_quantized_pitch),
                 (ctx, min_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20rc0_SetMaxQuantizedPitch,
                 (Beatrice20rc0_PitchContext1 * ctx, int max_quantized_pitch),
                 (ctx, max_quantized_pitch))
BEATRICE_FORWARD(void, Beatrice20rc0_EstimatePitch1,
                 (const Beatrice20rc0_PitchEstimator* pitch_estimator,
                  const float* input, int* output_quantized_pitch,
                  float* output_pitch_feature, Beatrice20rc0_PitchContext1* ctx),
                 (pitch_estimator, input, output_quantized_pitch,
                  output_pitch_feature, ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20rc0_ReadNSpeakers,
                 (const char* filename, int* output), (filename, output))
BEATRICE_FORWARD(Beatrice_ErrorCode, Beatrice20rc0_ReadSpeakerEmbeddings,
                 (const char* filename, float* output_codebook,
                  float* output_additive_speaker_embedding,
                  float* output_formant_shift_embedding,
                  float* output_key_value_speaker_embedding),
                 (filename, output_codebook, output_additive_speaker_embedding,
                  output_formant_shift_embedding,
                  output_key_value_speaker_embedding))
BEATRICE_FORWARD(Beatrice20rc0_WaveformGenerator*,
                 Beatrice20rc0_CreateWaveformGenerator, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyWaveformGenerator,
                 (Beatrice20rc0_WaveformGenerator * waveform_generator),
                 (waveform_generator))
BEATRICE_FORWARD(Beatrice20rc0_WaveformContext1*,
                 Beatrice20rc0_CreateWaveformContext1, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyWaveformContext1,
                 (Beatrice20rc0_WaveformContext1 * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20rc0_ReadWaveformGeneratorParameters,
                 (Beatrice20rc0_WaveformGenerator * waveform_generator,
                  const char* filename),
                 (waveform_generator, filename))
BEATRICE_FORWARD(void, Beatrice20rc0_GenerateWaveform1,
                 (const Beatrice20rc0_WaveformGenerator* waveform_generator,
                  const float* input_phone, const int* input_quantized_pitch,
                  const float* input_pitch_features, float* output,
                  Beatrice20rc0_WaveformContext1* ctx),
                 (waveform_generator, input_phone, input_quantized_pitch,
                  input_pitch_features, output, ctx))
BEATRICE_FORWARD(Beatrice20rc0_EmbeddingSetter*,
                 Beatrice20rc0_CreateEmbeddingSetter, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyEmbeddingSetter,
                 (Beatrice20rc0_EmbeddingSetter * embedding_setter),
                 (embedding_setter))
BEATRICE_FORWARD(Beatrice20rc0_EmbeddingContext*,
                 Beatrice20rc0_CreateEmbeddingContext, (void), ())
BEATRICE_FORWARD(void, Beatrice20rc0_DestroyEmbeddingContext,
                 (Beatrice20rc0_EmbeddingContext * ctx), (ctx))
BEATRICE_FORWARD(Beatrice_ErrorCode,
                 Beatrice20rc0_ReadEmbeddingSetterParameters,
                 (Beatrice20rc0_EmbeddingSetter * embedding_setter,
                  const char* filename),
                 (embedding_setter, filename))
BEATRICE_FORWARD(void, Beatrice20rc0_SetCodebook,
                 (Beatrice20rc0_PhoneContext1 * phone_ctx,
                  const float* codebook),
                 (phone_ctx, codebook))
BEATRICE_FORWARD(void, Beatrice20rc0_SetAdditiveSpeakerEmbedding,
                 (const Beatrice20rc0_EmbeddingSetter* embedding_setter,
                  const float* embedding,
                  Beatrice20rc0_EmbeddingContext* embedding_ctx,
                  Beatrice20rc0_WaveformContext1* waveform_ctx),
                 (embedding_setter, embedding, embedding_ctx, waveform_ctx))
BEATRICE_FORWARD(void, Beatrice20rc0_SetFormantShiftEmbedding,
                 (const Beatrice20rc0_EmbeddingSetter* embedding_setter,
                  const float* embedding,
                  Beatrice20rc0_EmbeddingContext* embedding_ctx,
                  Beatrice20rc0_WaveformContext1* waveform_ctx),
                 (embedding_setter, embedding, embedding_ctx, waveform_ctx))
BEATRICE_FORWARD(void, Beatrice20rc0_RegisterKeyValueSpeakerEmbedding,
                 (const Beatrice20rc0_EmbeddingSetter* embedding_setter,
                  const float* kv_speaker_embedding,
                  Beatrice20rc0_EmbeddingContext* embedding_ctx),
                 (embedding_setter, kv_speaker_embedding, embedding_ctx))
BEATRICE_FORWARD(void, Beatrice20rc0_SetKeyValueSpeakerEmbedding,
                 (const Beatrice20rc0_EmbeddingSetter* embedding_setter,
                  int block, Beatrice20rc0_EmbeddingContext* embedding_ctx,
                  Beatrice20rc0_WaveformContext1* waveform_ctx),
                 (embedding_setter, block, embedding_ctx, waveform_ctx))

}  // extern "C"

#undef BEATRICE_FORWARD
