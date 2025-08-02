// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_PROCESSOR_PROXY_H_
#define BEATRICE_COMMON_PROCESSOR_PROXY_H_

#include <memory>

#include "common/error.h"
#include "common/model_config.h"
#include "common/parameter_schema.h"
#include "common/parameter_state.h"
#include "common/processor_core.h"
#include "common/processor_core_0.h"
#include "common/processor_core_1.h"
#include "common/processor_core_2.h"

namespace beatrice::common {

// 異なるバージョンの ProcessorCore を、
// また異なる種類のパラメータの変更を統一的に扱うためのクラス。
// パラメータの変更は kSchema で定められた ID を介して行う。
class ProcessorProxy {
 public:
  explicit ProcessorProxy(const ParameterSchema& schema) : sample_rate_() {
    parameter_state_.SetDefaultValues(schema);
    core_ = std::make_unique<ProcessorCoreUnloaded>();
  }
  explicit ProcessorProxy(const ParameterState& parameter_state)
      : sample_rate_(), parameter_state_(parameter_state) {
    auto error_code = SyncAllParameters();
    assert(error_code == ErrorCode::kSuccess);
  }
  [[nodiscard]] auto GetSampleRate() const -> double { return sample_rate_; }
  auto SetSampleRate(const double new_sample_rate) -> ErrorCode {
    sample_rate_ = new_sample_rate;
    return core_->SetSampleRate(sample_rate_);
  }
  [[nodiscard]] auto GetParameter(ParameterID param_id) const -> const auto&;
  template <typename T>
  auto SetParameter(const ParameterID param_id, const T& value) -> ErrorCode {
    parameter_state_.SetValue(param_id, value);
    return SyncParameter(param_id);
  }
  auto LoadModel(const std::filesystem::path& file) -> ErrorCode {
    auto error_code = ErrorCode::kSuccess;
    if (!std::filesystem::exists(file)) {
      error_code = ErrorCode::kFileOpenError;
      goto fail;
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
        case 2:
          core_ = std::make_unique<ProcessorCore2>(sample_rate_);
          break;
        default:
          goto fail;
      }
      if (const auto err = core_->LoadModel(model_config, file);
          err != ErrorCode::kSuccess) {
        return err;
      }
    } catch (const toml::file_io_error) {
      error_code = ErrorCode::kFileOpenError;
      goto fail;
    } catch (const toml::syntax_error) {
      error_code = ErrorCode::kTOMLSyntaxError;
      goto fail;
    } catch (const std::exception& e) {
      error_code = ErrorCode::kUnknownError;
      goto fail;
    }
    return SyncAllParameters(ParameterID::kModel);

  fail:
    core_ = std::make_unique<ProcessorCoreUnloaded>();
    return error_code;
  }
  auto Read(std::istream& is) -> ErrorCode;
  auto Write(std::ostream& os) const -> ErrorCode;
  [[nodiscard]] auto GetParameterState() const -> const ParameterState&;
  [[nodiscard]] auto GetCore() const
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
  auto SyncParameter(ParameterID param_id) -> ErrorCode;
  auto SyncAllParameters(ParameterID ignore_param_id = ParameterID::kNull)
      -> ErrorCode;
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PROCESSOR_PROXY_H_
