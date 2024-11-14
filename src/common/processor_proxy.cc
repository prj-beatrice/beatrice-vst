// Copyright (c) 2024 Project Beatrice

#include "common/processor_proxy.h"

namespace beatrice::common {

auto ProcessorProxy::GetParameter(const ParameterID param_id) const -> const
    auto& {
  return parameter_state_.GetValue(param_id);
}

void ProcessorProxy::SyncParameter(const ParameterID param_id) {
  const auto& parameter = kSchema.GetParameter(param_id);
  if (const auto* const number_parameter =
          std::get_if<NumberParameter>(&parameter)) {
    number_parameter->ProcessorSetValue(
        *this, std::get<double>(GetParameter(param_id)));
  } else if (const auto* const list_parameter =
                 std::get_if<ListParameter>(&parameter)) {
    list_parameter->ProcessorSetValue(*this,
                                      std::get<int>(GetParameter(param_id)));
  } else if (const auto* const string_parameter =
                 std::get_if<StringParameter>(&parameter)) {
    string_parameter->ProcessorSetValue(
        *this,
        *std::get<std::unique_ptr<std::u8string>>(GetParameter(param_id)));
  } else {
    assert(false);
  }
}

void ProcessorProxy::SyncAllParameters(const ParameterID ignore_param_id) {
  for (const auto& [param_id, param] : kSchema) {
    if (param_id == ignore_param_id) {
      continue;
    }
    SyncParameter(param_id);
  }
}

auto ProcessorProxy::Read(std::istream& is) -> int {
  const auto t = parameter_state_.ReadOrSetDefault(is, kSchema);
  SyncAllParameters();
  return t;
}

auto ProcessorProxy::Write(std::ostream& os) const -> int {
  return parameter_state_.Write(os);
}

auto ProcessorProxy::GetParameterState() const -> const ParameterState& {
  return parameter_state_;
}

}  // namespace beatrice::common
