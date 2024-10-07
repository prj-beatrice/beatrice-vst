// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PARAMETER_SCHEMA_H_
#define BEATRICE_COMMON_PARAMETER_SCHEMA_H_

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace beatrice::common {

class ProcessorProxy;
class ControllerCore;

// VST と互換性のあるフラグ
namespace parameter_flag {
enum : int {
  kNoFlags = 0,
  // オートメーション可能
  kCanAutomate = 1 << 0,
  // プラグイン外部から変更不可, kCanAutomate であってはならない
  kIsReadOnly = 1 << 1,
  // 周期的
  kIsWrapAround = 1 << 2,
  // 複数通りから選ぶタイプ
  kIsList = 1 << 3,
  // 非表示, kCanAutomate でなく、kIsReadOnly である必要がある
  kIsHidden = 1 << 4,

  // バイパスは一旦使わない
  // kIsBypass = 1 << 16
};
}  // namespace parameter_flag

class NumberParameter {
 public:
  inline NumberParameter(
      std::u8string name, const double default_value, const double min_value,
      const double max_value, std::u8string units, const int divisions,
      std::u8string short_name, const int flags,
      int (*const controller_set_value)(ControllerCore&, double),
      int (*const processor_set_value)(ProcessorProxy&, double))
      : name_(std::move(name)),
        default_value_(default_value),
        min_value_(min_value),
        max_value_(max_value),
        units_(std::move(units)),
        divisions_(divisions),
        short_name_(std::move(short_name)),
        flags_(flags),
        controller_set_value_(controller_set_value),
        processor_set_value_(processor_set_value) {}
  [[nodiscard]] inline auto GetName() const -> const std::u8string& {
    return name_;
  }
  [[nodiscard]] inline auto GetDefaultValue() const -> double {
    return default_value_;
  }
  [[nodiscard]] inline auto GetMinValue() const -> double { return min_value_; }
  [[nodiscard]] inline auto GetMaxValue() const -> double { return max_value_; }
  [[nodiscard]] inline auto GetUnits() const -> const std::u8string& {
    return units_;
  }
  [[nodiscard]] inline auto GetDivisions() const -> int { return divisions_; }
  [[nodiscard]] inline auto GetShortName() const -> const std::u8string& {
    return short_name_;
  }
  [[nodiscard]] inline auto GetFlags() const -> int { return flags_; }
  // 他のパラメータとの同期を取ったりするのに使う
  inline auto ControllerSetValue(ControllerCore& ctx,
                                 const double value) const -> int {
    return controller_set_value_(ctx, value);
  }
  // 音声処理クラスに値を設定するのに使う
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const double value) const -> int {
    return processor_set_value_(ctx, value);
  }

 private:
  std::u8string name_;
  double default_value_;
  double min_value_;
  double max_value_;
  std::u8string units_;
  int divisions_;
  std::u8string short_name_;
  int flags_;
  int (*controller_set_value_)(ControllerCore&, double);
  int (*const processor_set_value_)(ProcessorProxy&, double);
};

class ListParameter {
 public:
  inline ListParameter(std::u8string name,
                       const std::vector<std::u8string>& values,
                       const int default_value, std::u8string short_name,
                       int flags,
                       int (*const controller_set_value)(ControllerCore&, int),
                       int (*const processor_set_value)(ProcessorProxy&, int))
      : name_(std::move(name)),
        values_(values),
        default_value_(default_value),
        short_name_(std::move(short_name)),
        flags_(flags),
        controller_set_value_(controller_set_value),
        processor_set_value_(processor_set_value) {}
  [[nodiscard]] inline auto GetName() const -> const std::u8string& {
    return name_;
  }
  [[nodiscard]] inline auto GetValues() const
      -> const std::vector<std::u8string>& {
    return values_;
  }
  [[nodiscard]] inline auto GetDefaultValue() const -> int {
    return default_value_;
  }
  [[nodiscard]] inline auto GetDivisions() const -> int {
    return static_cast<int>(values_.size()) - 1;
  }
  [[nodiscard]] inline auto GetShortName() const -> const std::u8string& {
    return short_name_;
  }
  [[nodiscard]] inline auto GetFlags() const -> int { return flags_; }
  inline auto ControllerSetValue(ControllerCore& ctx,
                                 const int value) const -> int {
    return controller_set_value_(ctx, value);
  }
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const int value) const -> int {
    return processor_set_value_(ctx, value);
  }

 private:
  std::u8string name_;
  std::vector<std::u8string> values_;
  int default_value_;
  std::u8string short_name_;
  int flags_;
  int (*controller_set_value_)(ControllerCore&, int);
  int (*const processor_set_value_)(ProcessorProxy&, int);
};

class StringParameter {
 public:
  inline StringParameter(
      std::u8string name, std::u8string default_value,
      const bool reset_when_model_load,
      int (*const controller_set_value)(ControllerCore&, const std::u8string&),
      int (*const processor_set_value)(ProcessorProxy&, const std::u8string&))
      : name_(std::move(name)),
        default_value_(std::move(default_value)),
        reset_when_model_load_(reset_when_model_load),
        controller_set_value_(controller_set_value),
        processor_set_value_(processor_set_value) {}
  [[nodiscard]] inline auto GetName() const -> const std::u8string& {
    return name_;
  }
  [[nodiscard]] inline auto GetDefaultValue() const -> const std::u8string& {
    return default_value_;
  }
  [[nodiscard]] inline auto GetResetWhenModelLoad() const -> bool {
    return reset_when_model_load_;
  }
  inline auto ControllerSetValue(ControllerCore& ctx,
                                 const std::u8string& value) const -> int {
    return controller_set_value_(ctx, value);
  }
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const std::u8string& value) const -> int {
    return processor_set_value_(ctx, value);
  }

 private:
  std::u8string name_;
  std::u8string default_value_;
  bool reset_when_model_load_;
  int (*controller_set_value_)(ControllerCore&, const std::u8string&);
  int (*const processor_set_value_)(ProcessorProxy&, const std::u8string&);
};

using ParameterVariant =
    std::variant<NumberParameter, ListParameter, StringParameter>;

class ParameterGroup {
 public:
  inline explicit ParameterGroup(std::string name) : name_(std::move(name)) {}
  inline ParameterGroup(
      std::string name,
      const std::vector<std::tuple<int, ParameterVariant>>& parameters)
      : name_(std::move(name)) {
    for (const auto& [param_id, param] : parameters) {
      AddParameter(param_id, param);
    }
  }
  inline void AddParameter(const int param_id,
                           const ParameterVariant& parameter) {
    parameters_.insert({param_id, parameter});
  }
  [[nodiscard]] inline auto GetParameter(const int param_id) const
      -> const ParameterVariant& {
    return parameters_.at(param_id);
  }
  // NOLINTBEGIN(readability-identifier-naming)
  [[nodiscard]] inline auto begin() const { return parameters_.begin(); }
  [[nodiscard]] inline auto end() const { return parameters_.end(); }
  // NOLINTEND(readability-identifier-naming)

 private:
  std::string name_;
  std::map<int, ParameterVariant> parameters_;
};

// 直接コントロールできるパラメータは少なくともすべて含む
class ParameterSchema {
 public:
  inline ParameterSchema() = default;
  inline explicit ParameterSchema(
      const std::vector<std::tuple<int, ParameterGroup>>& groups) {
    for (const auto& [group_id, group] : groups) {
      AddParameterGroup(group_id, group);
    }
  }
  inline void AddParameterGroup(const int group_id,
                                const ParameterGroup& group) {
    groups_.insert({group_id, group});
  }
  [[nodiscard]] inline auto GetParameterGroup(const int group_id) const
      -> const ParameterGroup& {
    return groups_.at(group_id);
  }
  [[nodiscard]] inline auto GetParameter(
      const int group_id, const int param_id) const -> const ParameterVariant& {
    return groups_.at(group_id).GetParameter(param_id);
  }
  // NOLINTBEGIN(readability-identifier-naming)
  [[nodiscard]] inline auto begin() const { return groups_.begin(); }
  [[nodiscard]] inline auto end() const { return groups_.end(); }
  [[nodiscard]] inline auto cbegin() const { return groups_.cbegin(); }
  [[nodiscard]] inline auto cend() const { return groups_.cend(); }
  // NOLINTEND(readability-identifier-naming)

 private:
  std::map<int, ParameterGroup> groups_;
};

extern const ParameterSchema kSchema;

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PARAMETER_SCHEMA_H_
