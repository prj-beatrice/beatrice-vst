// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_PARAMETER_H_
#define BEATRICE_VST_PARAMETER_H_

#include <algorithm>
#include <cassert>
#include <cmath>

#include "vst3sdk/pluginterfaces/vst/ivstunits.h"
#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"

// Beatrice
#include "common/parameter_schema.h"

namespace beatrice::vst {

inline auto Normalize(const common::NumberParameter& param,
                      const double plain_value) -> double {
  auto normalized = (plain_value - param.GetMinValue()) /
                    (param.GetMaxValue() - param.GetMinValue());
  if (param.GetDivisions() > 0) {
    normalized =
        std::floor(normalized * static_cast<double>(param.GetDivisions() + 1)) /
        static_cast<double>(param.GetDivisions());
  }
  normalized = std::clamp(normalized, 0.0, 1.0);
  return normalized;
}

inline auto Normalize(const common::ListParameter& param, const int plain_value)
    -> double {
  return static_cast<double>(std::clamp(plain_value, 0, param.GetDivisions())) /
         static_cast<double>(param.GetDivisions());
}

inline auto Denormalize(const common::NumberParameter& param,
                        const double normalized_value) -> double {
  auto plain = normalized_value;
  if (param.GetDivisions() > 0) {
    plain = std::floor(plain * static_cast<double>(param.GetDivisions() + 1)) /
            static_cast<double>(param.GetDivisions());
  }
  plain =
      plain * (param.GetMaxValue() - param.GetMinValue()) + param.GetMinValue();
  plain = std::clamp(plain, param.GetMinValue(), param.GetMaxValue());
  return plain;
}

inline auto Denormalize(const common::ListParameter& param,
                        const double normalized_value) -> int {
  return std::clamp(
      static_cast<int>(normalized_value *
                       static_cast<double>(param.GetDivisions() + 1)),
      0, param.GetDivisions());
}

class LinearParameter : public Steinberg::Vst::Parameter {
  using ParameterInfo = Steinberg::Vst::ParameterInfo;
  using ParamValue = Steinberg::Vst::ParamValue;
  using TChar = Steinberg::Vst::TChar;
  using ParamID = Steinberg::Vst::ParamID;
  using int32 = Steinberg::int32;
  using UnitID = Steinberg::Vst::UnitID;
  using String128 = Steinberg::Vst::String128;

 public:
  LinearParameter(const ParameterInfo& paramInfo, ParamValue min,
                  ParamValue max);
  LinearParameter(const TChar* title, ParamID tag, const TChar* units = nullptr,
                  ParamValue minPlain = 0., ParamValue maxPlain = 1.,
                  ParamValue defaultValuePlain = 0., int32 divisions = 0,
                  int32 flags = ParameterInfo::kCanAutomate,
                  UnitID unitID = Steinberg::Vst::kRootUnitId,
                  const TChar* shortTitle = nullptr);

  void toString(ParamValue _valueNormalized,
                String128 string) const SMTG_OVERRIDE;
  auto fromString(const TChar* string,  // NOLINT(build/include_what_you_use)
                  ParamValue& _valueNormalized) const -> bool SMTG_OVERRIDE;
  [[nodiscard]] auto toPlain(ParamValue _valueNormalized) const
      -> ParamValue SMTG_OVERRIDE;
  [[nodiscard]] auto toNormalized(ParamValue plainValue) const
      -> ParamValue SMTG_OVERRIDE;

  // NOLINTNEXTLINE(modernize-use-trailing-return-type,google-default-arguments)
  OBJ_METHODS(LinearParameter, Parameter)

 private:
  // NOLINTBEGIN(readability-identifier-naming)
  ParamValue minPlain;
  ParamValue maxPlain;
  // NOLINTEND(readability-identifier-naming)
};
}  // namespace beatrice::vst

#endif  // BEATRICE_VST_PARAMETER_H_
