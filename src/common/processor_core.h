// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PROCESSOR_CORE_H_
#define BEATRICE_COMMON_PROCESSOR_CORE_H_

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
                       int n_samples) -> int = 0;
  virtual auto ResetContext() -> int { return 0; }
  virtual auto LoadModel(const ModelConfig& /*config*/,
                         const std::filesystem::path& /*file*/) -> int {
    return 0;
  }

 protected:
  virtual auto SetSampleRate(double /*sample_rate*/) -> int { return 0; }

 public:
  virtual auto SetTargetSpeaker(int /*target_speaker*/) -> int { return 0; }
  virtual auto SetFormantShift(double /*formant_shift*/) -> int { return 0; }
  virtual auto SetPitchShift(double /*pitch_shift*/) -> int { return 0; }
  virtual auto SetInputGain(double /*input_gain*/) -> int { return 0; }
  virtual auto SetOutputGain(double /*output_gain*/) -> int { return 0; }

  friend class ProcessorProxy;
};

// 初期状態やエラー時に使用される ProcessorCore
class ProcessorCoreUnloaded : public ProcessorCoreBase {
 public:
  using ProcessorCoreBase::ProcessorCoreBase;
  [[nodiscard]] inline auto GetVersion() const -> int override { return -1; }
  inline auto Process(const float* const /*input*/, float* const output,
                      const int n_samples) -> int override {
    std::memset(output, 0, sizeof(float) * n_samples);
    return 0;
  }
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_CORE_H_
