// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_H_
#define BEATRICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define BEATRICE_IN_HOP_LENGTH 160
#define BEATRICE_OUT_HOP_LENGTH 240
#define BEATRICE_PHONE_CHANNELS 256
#define BEATRICE_PITCH_BINS 384
#define BEATRICE_PITCH_BINS_PER_OCTAVE 96
#define BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS 256
#define BEATRICE_IN_SAMPLE_RATE 16000
#define BEATRICE_OUT_SAMPLE_RATE 24000

enum Beatrice_ErrorCode {
  Beatrice_kSuccess = 0,
  Beatrice_kFileOpenError,
  Beatrice_kFileTooSmall,
  Beatrice_kFileTooLarge,
  Beatrice_kInvalidFileSize,
};
typedef enum Beatrice_ErrorCode Beatrice_ErrorCode;

// -------- 20a2 --------

struct Beatrice20a2_PhoneExtractor;
struct Beatrice20a2_PhoneContext1;
struct Beatrice20a2_PitchEstimator;
struct Beatrice20a2_PitchContext1;
struct Beatrice20a2_WaveformGenerator;
struct Beatrice20a2_WaveformContext1;

typedef struct Beatrice20a2_PhoneExtractor Beatrice20a2_PhoneExtractor;
typedef struct Beatrice20a2_PhoneContext1 Beatrice20a2_PhoneContext1;
typedef struct Beatrice20a2_PitchEstimator Beatrice20a2_PitchEstimator;
typedef struct Beatrice20a2_PitchContext1 Beatrice20a2_PitchContext1;
typedef struct Beatrice20a2_WaveformGenerator Beatrice20a2_WaveformGenerator;
typedef struct Beatrice20a2_WaveformContext1 Beatrice20a2_WaveformContext1;

// Phone Extractor
Beatrice20a2_PhoneExtractor* Beatrice20a2_CreatePhoneExtractor(void);
void Beatrice20a2_DestroyPhoneExtractor(
    Beatrice20a2_PhoneExtractor* phone_extractor);
Beatrice20a2_PhoneContext1* Beatrice20a2_CreatePhoneContext1(void);
void Beatrice20a2_DestroyPhoneContext1(Beatrice20a2_PhoneContext1* ctx);
Beatrice_ErrorCode Beatrice20a2_ReadPhoneExtractorParameters(
    Beatrice20a2_PhoneExtractor* phone_extractor,
    const char* filename  // UTF-8
);
void Beatrice20a2_ExtractPhone1(
    const Beatrice20a2_PhoneExtractor* phone_extractor,
    const float* input,  // BEATRICE_IN_HOP_LENGTH
    float* output,       // BEATRICE_PHONE_CHANNELS
    Beatrice20a2_PhoneContext1* ctx);
// Pitch Estimator
Beatrice20a2_PitchEstimator* Beatrice20a2_CreatePitchEstimator(void);
void Beatrice20a2_DestroyPitchEstimator(
    Beatrice20a2_PitchEstimator* pitch_estimator);
Beatrice20a2_PitchContext1* Beatrice20a2_CreatePitchContext1(void);
void Beatrice20a2_DestroyPitchContext1(Beatrice20a2_PitchContext1* ctx);
Beatrice_ErrorCode Beatrice20a2_ReadPitchEstimatorParameters(
    Beatrice20a2_PitchEstimator* pitch_estimator,
    const char* filename  // UTF-8
);
void Beatrice20a2_EstimatePitch1(
    const Beatrice20a2_PitchEstimator* const pitch_estimator,
    const float* input,           // BEATRICE_IN_HOP_LENGTH
    int* output_quantized_pitch,  // 1
    float* output_pitch_feature,  // 4
    Beatrice20a2_PitchContext1* ctx);
// Speaker Embeddings
Beatrice_ErrorCode Beatrice20a2_ReadNSpeakers(const char* filename,  // UTF-8
                                              int* output            // 1
);
Beatrice_ErrorCode Beatrice20a2_ReadSpeakerEmbeddings(
    const char* filename,  // UTF-8
    float* output  // n_speakers * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
);
// Waveform Generator
Beatrice20a2_WaveformGenerator* Beatrice20a2_CreateWaveformGenerator(void);
void Beatrice20a2_DestroyWaveformGenerator(
    Beatrice20a2_WaveformGenerator* waveform_generator);
Beatrice20a2_WaveformContext1* Beatrice20a2_CreateWaveformContext1(void);
void Beatrice20a2_DestroyWaveformContext1(Beatrice20a2_WaveformContext1* ctx);
Beatrice_ErrorCode Beatrice20a2_ReadWaveformGeneratorParameters(
    Beatrice20a2_WaveformGenerator* waveform_generator,
    const char* filename  // UTF-8
);
void Beatrice20a2_GenerateWaveform1(
    const Beatrice20a2_WaveformGenerator* waveform_generator,
    const float* input_phone,           // BEATRICE_PHONE_CHANNELS
    const int* input_quantized_pitch,   // 1
    const float* input_pitch_features,  // 4
    const float*
        input_speaker_embedding,  // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    float* output,                // BEATRICE_OUT_HOP_LENGTH
    Beatrice20a2_WaveformContext1* ctx);

// -------- 20b1 --------

struct Beatrice20b1_PhoneExtractor;
struct Beatrice20b1_PhoneContext1;
struct Beatrice20b1_PitchEstimator;
struct Beatrice20b1_PitchContext1;
struct Beatrice20b1_WaveformGenerator;
struct Beatrice20b1_WaveformContext1;

typedef struct Beatrice20b1_PhoneExtractor Beatrice20b1_PhoneExtractor;
typedef struct Beatrice20b1_PhoneContext1 Beatrice20b1_PhoneContext1;
typedef struct Beatrice20b1_PitchEstimator Beatrice20b1_PitchEstimator;
typedef struct Beatrice20b1_PitchContext1 Beatrice20b1_PitchContext1;
typedef struct Beatrice20b1_WaveformGenerator Beatrice20b1_WaveformGenerator;
typedef struct Beatrice20b1_WaveformContext1 Beatrice20b1_WaveformContext1;

// Phone Extractor
Beatrice20b1_PhoneExtractor* Beatrice20b1_CreatePhoneExtractor(void);
void Beatrice20b1_DestroyPhoneExtractor(
    Beatrice20b1_PhoneExtractor* phone_extractor);
Beatrice20b1_PhoneContext1* Beatrice20b1_CreatePhoneContext1(void);
void Beatrice20b1_DestroyPhoneContext1(Beatrice20b1_PhoneContext1* ctx);
Beatrice_ErrorCode Beatrice20b1_ReadPhoneExtractorParameters(
    Beatrice20b1_PhoneExtractor* phone_extractor,
    const char* filename  // UTF-8
);
void Beatrice20b1_ExtractPhone1(
    const Beatrice20b1_PhoneExtractor* phone_extractor,
    const float* input,  // BEATRICE_IN_HOP_LENGTH
    float* output,       // BEATRICE_PHONE_CHANNELS
    Beatrice20b1_PhoneContext1* ctx);
// Pitch Estimator
Beatrice20b1_PitchEstimator* Beatrice20b1_CreatePitchEstimator(void);
void Beatrice20b1_DestroyPitchEstimator(
    Beatrice20b1_PitchEstimator* pitch_estimator);
Beatrice20b1_PitchContext1* Beatrice20b1_CreatePitchContext1(void);
void Beatrice20b1_DestroyPitchContext1(Beatrice20b1_PitchContext1* ctx);
Beatrice_ErrorCode Beatrice20b1_ReadPitchEstimatorParameters(
    Beatrice20b1_PitchEstimator* pitch_estimator,
    const char* filename  // UTF-8
);
void Beatrice20b1_EstimatePitch1(
    const Beatrice20b1_PitchEstimator* const pitch_estimator,
    const float* input,           // BEATRICE_IN_HOP_LENGTH
    int* output_quantized_pitch,  // 1
    float* output_pitch_feature,  // 4
    Beatrice20b1_PitchContext1* ctx);
// Speaker Embeddings
Beatrice_ErrorCode Beatrice20b1_ReadNSpeakers(const char* filename,  // UTF-8
                                              int* output            // 1
);
Beatrice_ErrorCode Beatrice20b1_ReadSpeakerEmbeddings(
    const char* filename,  // UTF-8
    float* output  // n_speakers * BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
);
// Waveform Generator
Beatrice20b1_WaveformGenerator* Beatrice20b1_CreateWaveformGenerator(void);
void Beatrice20b1_DestroyWaveformGenerator(
    Beatrice20b1_WaveformGenerator* waveform_generator);
Beatrice20b1_WaveformContext1* Beatrice20b1_CreateWaveformContext1(void);
void Beatrice20b1_DestroyWaveformContext1(Beatrice20b1_WaveformContext1* ctx);
Beatrice_ErrorCode Beatrice20b1_ReadWaveformGeneratorParameters(
    Beatrice20b1_WaveformGenerator* waveform_generator,
    const char* filename  // UTF-8
);
void Beatrice20b1_GenerateWaveform1(
    const Beatrice20b1_WaveformGenerator* waveform_generator,
    const float* input_phone,           // BEATRICE_PHONE_CHANNELS
    const int* input_quantized_pitch,   // 1
    const float* input_pitch_features,  // 4
    const float*
        input_speaker_embedding,  // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    float* output,                // BEATRICE_OUT_HOP_LENGTH
    Beatrice20b1_WaveformContext1* ctx);

#ifdef __cplusplus
}
#endif

#endif  // BEATRICE_H_
