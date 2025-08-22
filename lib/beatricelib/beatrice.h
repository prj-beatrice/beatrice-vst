// Copyright (c) 2025 Project Beatrice

#ifndef BEATRICE_H_
#define BEATRICE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define BEATRICE_IN_HOP_LENGTH 160
#define BEATRICE_OUT_HOP_LENGTH 240
#define BEATRICE_PITCH_BINS_PER_OCTAVE 96
#define BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS 256
#define BEATRICE_IN_SAMPLE_RATE 16000
#define BEATRICE_OUT_SAMPLE_RATE 24000

#define BEATRICE_20A2_PHONE_CHANNELS 256
#define BEATRICE_20A2_PITCH_BINS 384

#define BEATRICE_20B1_PHONE_CHANNELS 256
#define BEATRICE_20B1_PITCH_BINS 384

#define BEATRICE_20RC0_PHONE_CHANNELS 128
#define BEATRICE_20RC0_PITCH_BINS 448
#define BEATRICE_20RC0_CODEBOOK_SIZE 512
#define BEATRICE_20RC0_KV_LENGTH 384
#define BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS 128
#define BEATRICE_20RC0_N_BLOCKS 4

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
    float* output,       // BEATRICE_20A2_PHONE_CHANNELS
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
void Beatrice20a2_SetMinQuantizedPitch(
    Beatrice20a2_PitchContext1* ctx,
    int min_quantized_pitch  // 1 to BEATRICE_20A2_PITCH_BINS - 1
);
void Beatrice20a2_SetMaxQuantizedPitch(
    Beatrice20a2_PitchContext1* ctx,
    int max_quantized_pitch  // 1 to BEATRICE_20A2_PITCH_BINS - 1
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
    const float* input_phone,           // BEATRICE_20A2_PHONE_CHANNELS
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
    float* output,       // BEATRICE_20B1_PHONE_CHANNELS
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
void Beatrice20b1_SetMinQuantizedPitch(
    Beatrice20b1_PitchContext1* ctx,
    int min_quantized_pitch  // 1 to BEATRICE_20B1_PITCH_BINS - 1
);
void Beatrice20b1_SetMaxQuantizedPitch(
    Beatrice20b1_PitchContext1* ctx,
    int max_quantized_pitch  // 1 to BEATRICE_20B1_PITCH_BINS - 1
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
    const float* input_phone,           // BEATRICE_20B1_PHONE_CHANNELS
    const int* input_quantized_pitch,   // 1
    const float* input_pitch_features,  // 4
    const float*
        input_speaker_embedding,  // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    float* output,                // BEATRICE_OUT_HOP_LENGTH
    Beatrice20b1_WaveformContext1* ctx);

// -------- 20rc0 --------

// 20rc0 では EmbeddingSetter が追加された。
// GenerateWaveform の前に各種セット関数を必ず呼び出さなければならない。
// 各種セット関数は、ReadParameters の後に呼び出す必要がある。

struct Beatrice20rc0_PhoneExtractor;
struct Beatrice20rc0_PhoneContext1;
struct Beatrice20rc0_PitchEstimator;
struct Beatrice20rc0_PitchContext1;
struct Beatrice20rc0_WaveformGenerator;
struct Beatrice20rc0_WaveformContext1;
struct Beatrice20rc0_EmbeddingSetter;
struct Beatrice20rc0_EmbeddingContext;

typedef struct Beatrice20rc0_PhoneExtractor Beatrice20rc0_PhoneExtractor;
typedef struct Beatrice20rc0_PhoneContext1 Beatrice20rc0_PhoneContext1;
typedef struct Beatrice20rc0_PitchEstimator Beatrice20rc0_PitchEstimator;
typedef struct Beatrice20rc0_PitchContext1 Beatrice20rc0_PitchContext1;
typedef struct Beatrice20rc0_WaveformGenerator Beatrice20rc0_WaveformGenerator;
typedef struct Beatrice20rc0_WaveformContext1 Beatrice20rc0_WaveformContext1;
typedef struct Beatrice20rc0_EmbeddingSetter Beatrice20rc0_EmbeddingSetter;
typedef struct Beatrice20rc0_EmbeddingContext Beatrice20rc0_EmbeddingContext;

// Phone Extractor
Beatrice20rc0_PhoneExtractor* Beatrice20rc0_CreatePhoneExtractor(void);
void Beatrice20rc0_DestroyPhoneExtractor(
    Beatrice20rc0_PhoneExtractor* phone_extractor);
Beatrice20rc0_PhoneContext1* Beatrice20rc0_CreatePhoneContext1(void);
void Beatrice20rc0_DestroyPhoneContext1(Beatrice20rc0_PhoneContext1* ctx);
Beatrice_ErrorCode Beatrice20rc0_ReadPhoneExtractorParameters(
    Beatrice20rc0_PhoneExtractor* phone_extractor,
    const char* filename  // UTF-8
);
void Beatrice20rc0_SetVQNumNeighbors(
    Beatrice20rc0_PhoneContext1* const ctx,
    const int num_neighbors  // 0 (disable) to BEATRICE_20RC0_CODEBOOK_SIZE
);
void Beatrice20rc0_ExtractPhone1(
    const Beatrice20rc0_PhoneExtractor* phone_extractor,
    const float* input,  // BEATRICE_IN_HOP_LENGTH
    float* output,       // BEATRICE_20RC0_PHONE_CHANNELS
    Beatrice20rc0_PhoneContext1* ctx);
// Pitch Estimator
Beatrice20rc0_PitchEstimator* Beatrice20rc0_CreatePitchEstimator(void);
void Beatrice20rc0_DestroyPitchEstimator(
    Beatrice20rc0_PitchEstimator* pitch_estimator);
Beatrice20rc0_PitchContext1* Beatrice20rc0_CreatePitchContext1(void);
void Beatrice20rc0_DestroyPitchContext1(Beatrice20rc0_PitchContext1* ctx);
Beatrice_ErrorCode Beatrice20rc0_ReadPitchEstimatorParameters(
    Beatrice20rc0_PitchEstimator* pitch_estimator,
    const char* filename  // UTF-8
);
void Beatrice20rc0_SetMinQuantizedPitch(
    Beatrice20rc0_PitchContext1* ctx,
    int min_quantized_pitch  // 1 to BEATRICE_20RC0_PITCH_BINS - 1
);
void Beatrice20rc0_SetMaxQuantizedPitch(
    Beatrice20rc0_PitchContext1* ctx,
    int max_quantized_pitch  // 1 to BEATRICE_20RC0_PITCH_BINS - 1
);
void Beatrice20rc0_EstimatePitch1(
    const Beatrice20rc0_PitchEstimator* const pitch_estimator,
    const float* input,           // BEATRICE_IN_HOP_LENGTH
    int* output_quantized_pitch,  // 1
    float* output_pitch_feature,  // 4
    Beatrice20rc0_PitchContext1* ctx);
// Speaker Embeddings
Beatrice_ErrorCode Beatrice20rc0_ReadNSpeakers(const char* filename,  // UTF-8
                                               int* output            // 1
);
Beatrice_ErrorCode Beatrice20rc0_ReadSpeakerEmbeddings(
    const char* filename,    // UTF-8
    float* output_codebook,  // n_speakers * BEATRICE_20RC0_CODEBOOK_SIZE *
                             // BEATRICE_20RC0_PHONE_CHANNELS
    float*
        output_additive_speaker_embedding,  // n_speakers *
                                            // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    float*
        output_formant_shift_embedding,  // 9 *
                                         // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    float*
        output_key_value_speaker_embedding  // n_speakers *
                                            // BEATRICE_20RC0_KV_LENGTH *
                                            // BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS
);
// Waveform Generator
Beatrice20rc0_WaveformGenerator* Beatrice20rc0_CreateWaveformGenerator(void);
void Beatrice20rc0_DestroyWaveformGenerator(
    Beatrice20rc0_WaveformGenerator* waveform_generator);
Beatrice20rc0_WaveformContext1* Beatrice20rc0_CreateWaveformContext1(void);
void Beatrice20rc0_DestroyWaveformContext1(Beatrice20rc0_WaveformContext1* ctx);
Beatrice_ErrorCode Beatrice20rc0_ReadWaveformGeneratorParameters(
    Beatrice20rc0_WaveformGenerator* waveform_generator,
    const char* filename  // UTF-8
);
void Beatrice20rc0_GenerateWaveform1(
    const Beatrice20rc0_WaveformGenerator* waveform_generator,
    const float* input_phone,           // BEATRICE_20RC0_PHONE_CHANNELS
    const int* input_quantized_pitch,   // 1
    const float* input_pitch_features,  // 4
    float* output,                      // BEATRICE_OUT_HOP_LENGTH
    Beatrice20rc0_WaveformContext1* ctx);
// Embedding Setter
Beatrice20rc0_EmbeddingSetter* Beatrice20rc0_CreateEmbeddingSetter(void);
void Beatrice20rc0_DestroyEmbeddingSetter(
    Beatrice20rc0_EmbeddingSetter* embedding_setter);
Beatrice20rc0_EmbeddingContext* Beatrice20rc0_CreateEmbeddingContext(void);
void Beatrice20rc0_DestroyEmbeddingContext(Beatrice20rc0_EmbeddingContext* ctx);
Beatrice_ErrorCode Beatrice20rc0_ReadEmbeddingSetterParameters(
    Beatrice20rc0_EmbeddingSetter* embedding_setter,
    const char* filename  // UTF-8
);
void Beatrice20rc0_SetCodebook(
    Beatrice20rc0_PhoneContext1* phone_ctx,
    const float* codebook  // n_speakers * BEATRICE_20RC0_CODEBOOK_SIZE *
                           // BEATRICE_20RC0_PHONE_CHANNELS
);
void Beatrice20rc0_SetAdditiveSpeakerEmbedding(
    const Beatrice20rc0_EmbeddingSetter* embedding_setter,
    const float* embedding,  // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    Beatrice20rc0_EmbeddingContext* embedding_ctx,
    Beatrice20rc0_WaveformContext1* waveform_ctx);
void Beatrice20rc0_SetFormantShiftEmbedding(
    const Beatrice20rc0_EmbeddingSetter* embedding_setter,
    const float* embedding,  // BEATRICE_WAVEFORM_GENERATOR_HIDDEN_CHANNELS
    Beatrice20rc0_EmbeddingContext* embedding_ctx,
    Beatrice20rc0_WaveformContext1* waveform_ctx);
void Beatrice20rc0_RegisterKeyValueSpeakerEmbedding(
    const Beatrice20rc0_EmbeddingSetter* embedding_setter,
    const float*
        kv_speaker_embedding,  // BEATRICE_20RC0_KV_LENGTH *
                               // BEATRICE_20RC0_KV_SPEAKER_EMBEDDING_CHANNELS
    Beatrice20rc0_EmbeddingContext* embedding_ctx);
void Beatrice20rc0_SetKeyValueSpeakerEmbedding(
    const Beatrice20rc0_EmbeddingSetter* embedding_setter,
    int block,  // 0 to BEATRICE_20RC0_N_BLOCKS - 1
    Beatrice20rc0_EmbeddingContext* embedding_ctx,
    Beatrice20rc0_WaveformContext1* waveform_ctx);

#ifdef __cplusplus
}
#endif

#endif  // BEATRICE_H_
