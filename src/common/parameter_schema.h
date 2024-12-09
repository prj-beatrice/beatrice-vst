// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_COMMON_PARAMETER_SCHEMA_H_
#define BEATRICE_COMMON_PARAMETER_SCHEMA_H_

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <functional>

#include "common/error.h"
#include "common/model_config.h"

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

enum class ParameterID : int {
  kNull = -1,
  // kByPass = 0,
  kModel = 1,
  kVoice = 2,
  kFormantShift = 3,
  kPitchShift = 4,
  kAverageSourcePitch = 5,
  kLock = 6,
  kInputGain = 7,
  kOutputGain = 8,
  kIntonationIntensity = 9,
  kPitchCorrection = 10,
  kPitchCorrectionType = 11,
  kAverageTargetPitchBase = 100, // Voice Morphing Mode の分も格納するため、要素数は ( kMaxNSpeakers + 1 ) となる
  kVoiceMorphWeights = kAverageTargetPitchBase + ( kMaxNSpeakers + 1 ), // Voice Morphing Mode の重みを保存するのに使う
  kSentinel = kVoiceMorphWeights + kMaxNSpeakers,
};

class NumberParameter {
 public:
  inline NumberParameter(
      std::u8string name, const double default_value, const double min_value,
      const double max_value, std::u8string units, const int divisions,
      std::u8string short_name, const int flags,
      const std::function<ErrorCode (ControllerCore&, double)> controller_set_value,
      const std::function<ErrorCode (ProcessorProxy&, double)> processor_set_value) 
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
                                 const double value) const -> ErrorCode {
    return controller_set_value_(ctx, value);
  }
  // 音声処理クラスに値を設定するのに使う
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const double value) const -> ErrorCode {
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
  // キャプチャ付きのラムダ式も格納できるようにするため、関数ポインタから std::function に変更
  const std::function<ErrorCode (ControllerCore&, double)> controller_set_value_;
  const std::function<ErrorCode (ProcessorProxy&, double)> processor_set_value_; 
};

class ListParameter {
 public:
  inline ListParameter(
      std::u8string name, const std::vector<std::u8string>& values,
      const int default_value, std::u8string short_name, int flags,
      const std::function<ErrorCode (ControllerCore&, int)> controller_set_value,
      const std::function<ErrorCode (ProcessorProxy&, int)> processor_set_value) 
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
                                 const int value) const -> ErrorCode {
    return controller_set_value_(ctx, value);
  }
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const int value) const -> ErrorCode {
    return processor_set_value_(ctx, value);
  }

 private:
  std::u8string name_;
  std::vector<std::u8string> values_;
  int default_value_;
  std::u8string short_name_;
  int flags_;
  std::function<ErrorCode (ControllerCore&, int)> controller_set_value_;
  const std::function<ErrorCode (ProcessorProxy&, int)> processor_set_value_;
};

class StringParameter {
 public:
  inline StringParameter(
      std::u8string name, std::u8string default_value,
      const bool reset_when_model_load,
      const std::function<ErrorCode (ControllerCore&, const std::u8string&)> controller_set_value,
      const std::function<ErrorCode (ProcessorProxy&, const std::u8string&)> processor_set_value) 
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
  inline auto ControllerSetValue(
      ControllerCore& ctx, const std::u8string& value) const -> ErrorCode {
    return controller_set_value_(ctx, value);
  }
  inline auto ProcessorSetValue(ProcessorProxy& ctx,
                                const std::u8string& value) const -> ErrorCode {
    return processor_set_value_(ctx, value);
  }

 private:
  std::u8string name_;
  std::u8string default_value_;
  bool reset_when_model_load_;
  std::function<ErrorCode (ControllerCore&, const std::u8string&)> controller_set_value_;
  const std::function<ErrorCode (ProcessorProxy&, const std::u8string&)> processor_set_value_;
};

using ParameterVariant =
    std::variant<NumberParameter, ListParameter, StringParameter>;

// 直接コントロールできるパラメータは少なくともすべて含む
class ParameterSchema {
 public:
  inline ParameterSchema() = default;
  inline explicit ParameterSchema(
      const std::vector<std::tuple<ParameterID, ParameterVariant>>&
          parameters) {
    for (const auto& [param_id, param] : parameters) {
      AddParameter(param_id, param);
    }
  }
  inline void AddParameter(const ParameterID param_id,
                           const ParameterVariant& parameter) {
    parameters_.insert({param_id, parameter});
  }
  [[nodiscard]] inline auto GetParameter(const ParameterID param_id) const
      -> const ParameterVariant& {
    return parameters_.at(param_id);
  }
  // NOLINTBEGIN(readability-identifier-naming)
  [[nodiscard]] inline auto begin() const { return parameters_.begin(); }
  [[nodiscard]] inline auto end() const { return parameters_.end(); }
  [[nodiscard]] inline auto cbegin() const { return parameters_.cbegin(); }
  [[nodiscard]] inline auto cend() const { return parameters_.cend(); }
  // NOLINTEND(readability-identifier-naming)

 private:
  std::map<ParameterID, ParameterVariant> parameters_;
};

extern const ParameterSchema kSchema;

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_PARAMETER_SCHEMA_H_
