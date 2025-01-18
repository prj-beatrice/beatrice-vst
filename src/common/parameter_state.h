// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_PARAMETER_STATE_H_
#define BEATRICE_COMMON_PARAMETER_STATE_H_

#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <variant>

#include "common/error.h"
#include "common/parameter_schema.h"

namespace beatrice::common {

// 信号処理オブジェクトの状態の保存や復元を可能にするために、
// モデルのファイルパス等も含め信号処理オブジェクトに対して
// 外部から操作されたあらゆるパラメータの状態を保持するクラス。
class ParameterState {
 public:
  using Value = std::variant<int, double, std::unique_ptr<std::u8string>>;
  ParameterState() = default;
  ParameterState(const ParameterState&);
  auto operator=(const ParameterState&) -> ParameterState&;

  void SetDefaultValues(const ParameterSchema& schema);
  template <typename T>
  void SetValue(const ParameterID param_id, const T value)
    requires(sizeof(T) <= 8)
  {  // NOLINT(whitespace/braces)
    states_.insert_or_assign(param_id, value);
  }
  template <typename T>
  void SetValue(const ParameterID param_id, const T& value)
    requires(sizeof(T) > 8)
  {  // NOLINT(whitespace/braces)
    states_.insert_or_assign(param_id, std::make_unique<T>(value));
  }
  [[nodiscard]] auto GetValue(ParameterID param_id) const
      -> const ParameterState::Value&;
  auto Read(std::istream& is) -> ErrorCode;
  auto ReadOrSetDefault(std::istream& is, const ParameterSchema& schema)
      -> ErrorCode;
  auto Write(std::ostream& os) const -> ErrorCode;

 private:
  std::map<ParameterID, Value> states_;
};

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PARAMETER_STATE_H_
