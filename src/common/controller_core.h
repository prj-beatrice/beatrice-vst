// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_CONTROLLER_CORE_H_
#define BEATRICE_COMMON_CONTROLLER_CORE_H_

#include <vector>

#include "common/parameter_state.h"

namespace beatrice::common {

// パラメータ変化の追従に必要な情報を持つクラス
class ControllerCore {
 public:
  ParameterState parameter_state_;
  std::vector<ParameterID> updated_parameters_;

  ControllerCore() { parameter_state_.SetDefaultValues(kSchema); }
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_CONTROLLER_CORE_H_
