// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PROCESSOR_CORE_H_
#define BEATRICE_COMMON_PROCESSOR_CORE_H_

#include "common/error.h"
#include "common/model_config.h"

namespace beatrice::common {

// 任意のサンプリング周波数と任意のブロックサイズで
// Beatrice の推論を行う、ミニマルな信号処理クラス。
// 1 つの子クラスは 1 つのモデルバージョンに対応する。
// このクラスはピッチシフト量やモデルのファイルパスなどの設定の
// 保存と読み込みの機能を提供しないが、外部からそれを可能にするために、
// 状態を設定する異なる種類のメンバ関数の呼び出し順が前後したとしても
// 同じ結果が得られるように実装する必要がある。
// LoadModel() も含めメンバ関数は任意の順で呼ばれる可能性があるため、
// 不整合な状態では Process() 内で処理を行わないなどの対応が必要。
class ProcessorCoreBase {
 public:
  virtual ~ProcessorCoreBase() = default;
  [[nodiscard]] virtual auto GetVersion() const -> int = 0;
  virtual auto Process(const float* input, float* output,
                       int n_samples) -> ErrorCode = 0;
  virtual auto ResetContext() -> ErrorCode { return ErrorCode::kSuccess; }
  virtual auto LoadModel(const ModelConfig& /*config*/,
                         const std::filesystem::path& /*file*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }

 protected:
  virtual auto SetSampleRate(double /*sample_rate*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }

 public:
  virtual auto SetTargetSpeaker(int /*target_speaker*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetFormantShift(double /*formant_shift*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetPitchShift(double /*pitch_shift*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetInputGain(double /*input_gain*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetOutputGain(double /*output_gain*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetAverageSourcePitch(double /*average_pitch*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetIntonationIntensity(double /*intonation_intensity*/)
      -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetPitchCorrection(double /*pitch_correction*/) -> ErrorCode {
    return ErrorCode::kSuccess;
  }
  virtual auto SetPitchCorrectionType(int /*pitch_correction_type*/)
      -> ErrorCode {
    return ErrorCode::kSuccess;
  }

  virtual auto SetSpeakerMorphingWeight(
    int /*target_speaker*/, double /*morphing weight*/
  ) -> ErrorCode { return ErrorCode::kSuccess; }

  friend class ProcessorProxy;
};

// 初期状態やエラー時に使用される ProcessorCore
class ProcessorCoreUnloaded : public ProcessorCoreBase {
 public:
  using ProcessorCoreBase::ProcessorCoreBase;
  [[nodiscard]] inline auto GetVersion() const -> int override { return -1; }
  inline auto Process(const float* const /*input*/, float* const output,
                      const int n_samples) -> ErrorCode override {
    std::memset(output, 0, sizeof(float) * n_samples);
    return ErrorCode::kSuccess;
  }
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_CORE_H_
