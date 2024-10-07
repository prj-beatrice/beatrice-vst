// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PROCESSOR_PROXY_H_
#define BEATRICE_COMMON_PROCESSOR_PROXY_H_

#include <memory>

#include "common/parameter_state.h"
#include "common/processor_core.h"
#include "common/processor_core_0.h"
#include "common/processor_core_1.h"

namespace beatrice::common {

// 異なるバージョンの ProcessorCore を、
// また異なる種類のパラメータの変更を統一的に扱うためのクラス。
// パラメータの変更は kSchema で定められた ID を介して行う。
class ProcessorProxy {
 public:
  inline explicit ProcessorProxy(const ParameterSchema& schema)
      : sample_rate_() {
    parameter_state_.SetDefaultValues(schema);
    core_ = std::make_unique<ProcessorCoreUnloaded>();
  }
  inline explicit ProcessorProxy(const ParameterState& parameter_state)
      : sample_rate_(), parameter_state_(parameter_state) {
    SyncAllParameters();
  }
  [[nodiscard]] inline auto GetSampleRate() const -> double {
    return sample_rate_;
  }
  inline void SetSampleRate(const double new_sample_rate) {
    sample_rate_ = new_sample_rate;
    core_->SetSampleRate(sample_rate_);
  }
  [[nodiscard]] auto GetParameter(int group_id, int param_id) const -> const
      auto&;
  template <typename T>
  inline void SetParameter(const int group_id, const int param_id,
                           const T& value) {
    parameter_state_.SetValue(group_id, param_id, value);
    SyncParameter(group_id, param_id);
  }
  inline auto LoadModel(const std::filesystem::path& file) -> int {
    if (!std::filesystem::exists(file)) {
      return 1;
    }
    try {
      const auto toml_data = toml::parse(file);
      const auto model_config = toml::get<ModelConfig>(toml_data);
      switch (model_config.model.VersionInt()) {
        case 0:
          core_ = std::make_unique<ProcessorCore0>(sample_rate_);
          break;
        case 1:
          core_ = std::make_unique<ProcessorCore1>(sample_rate_);
          break;
        default:
          core_ = std::make_unique<ProcessorCoreUnloaded>();
          break;
      }
      core_->LoadModel(model_config, file);
    } catch (const std::exception& e) {
      return 1;
    }
    SyncAllParameters(0, 1);
    return 0;
  }
  auto Read(std::istream& is) -> int;
  auto Write(std::ostream& os) const -> int;
  [[nodiscard]] auto GetParameterState() const -> const ParameterState&;
  [[nodiscard]] inline auto GetCore() const
      -> const std::unique_ptr<ProcessorCoreBase>& {
    return core_;
  }

 private:
  double sample_rate_;
  ParameterState parameter_state_;
  std::unique_ptr<ProcessorCoreBase> core_;

  // parameter_state_ の値を core_ に反映させる。
  // 原則として state と core は同期されており、
  // 外部から Sync を行う必要はない。
  void SyncParameter(int group_id, int param_id);
  void SyncAllParameters(int ignore_group_id = -1, int ignore_param_id = -1);
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_PROXY_H_
