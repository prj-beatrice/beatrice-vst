// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "common/processor_proxy.h"

namespace beatrice::common {

auto ProcessorProxy::GetParameter(const ParameterID param_id) const -> const
    auto& {
  return parameter_state_.GetValue(param_id);
}

auto ProcessorProxy::SyncParameter(const ParameterID param_id) -> ErrorCode {
  const auto& parameter = kSchema.GetParameter(param_id);
  if (const auto* const number_parameter =
          std::get_if<NumberParameter>(&parameter)) {
    return number_parameter->ProcessorSetValue(
        *this, std::get<double>(GetParameter(param_id)));
  } else if (const auto* const list_parameter =
                 std::get_if<ListParameter>(&parameter)) {
    return list_parameter->ProcessorSetValue(
        *this, std::get<int>(GetParameter(param_id)));
  } else if (const auto* const string_parameter =
                 std::get_if<StringParameter>(&parameter)) {
    return string_parameter->ProcessorSetValue(
        *this,
        *std::get<std::unique_ptr<std::u8string>>(GetParameter(param_id)));
  } else {
    assert(false);
    return ErrorCode::kUnknownError;
  }
}

auto ProcessorProxy::SyncAllParameters(const ParameterID ignore_param_id)
    -> ErrorCode {
  auto error_code = ErrorCode::kSuccess;
  for (const auto& [param_id, param] : kSchema) {
    if (param_id == ignore_param_id) {
      continue;
    }
    if (const auto err = SyncParameter(param_id); err != ErrorCode::kSuccess) {
      error_code = err;
    }
  }
  return error_code;
}

auto ProcessorProxy::Read(std::istream& is) -> ErrorCode {
  const auto error_code_read = parameter_state_.ReadOrSetDefault(is, kSchema);
  const auto error_code_sync = SyncAllParameters();
  return error_code_read == ErrorCode::kSuccess ? error_code_sync
                                                : error_code_read;
}

auto ProcessorProxy::Write(std::ostream& os) const -> ErrorCode {
  return parameter_state_.Write(os);
}

auto ProcessorProxy::GetParameterState() const -> const ParameterState& {
  return parameter_state_;
}

}  // namespace beatrice::common
