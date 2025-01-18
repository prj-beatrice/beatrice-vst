// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "vst/parameter.h"

#include "vst3sdk/pluginterfaces/base/futils.h"
#include "vst3sdk/pluginterfaces/base/ustring.h"
#include "vst3sdk/pluginterfaces/vst/ivsteditcontroller.h"

namespace beatrice::vst {

using Steinberg::FromNormalized;
using Steinberg::tstrlen;
using Steinberg::UString;

LinearParameter::LinearParameter(const ParameterInfo& paramInfo, ParamValue min,
                                 ParamValue max)
    : Parameter(paramInfo), minPlain(min), maxPlain(max) {}

LinearParameter::LinearParameter(const TChar* title, ParamID tag,
                                 const TChar* units, ParamValue minPlain,
                                 ParamValue maxPlain,
                                 ParamValue defaultValuePlain, int32 divisions,
                                 int32 flags, UnitID unitID,
                                 const TChar* shortTitle)
    : minPlain(minPlain), maxPlain(maxPlain) {
  UString(info.title, str16BufferSize(String128)).assign(title);
  if (units) {
    UString(info.units, str16BufferSize(String128)).assign(units);
  }
  if (shortTitle) {
    UString(info.shortTitle, str16BufferSize(String128)).assign(shortTitle);
  }

  info.stepCount = divisions;
  info.defaultNormalizedValue = valueNormalized =
      LinearParameter::toNormalized(defaultValuePlain);
  info.flags = flags;
  info.id = tag;
  info.unitId = unitID;
}

void LinearParameter::toString(ParamValue _valueNormalized,
                               String128 string) const {
  Parameter::toString(toPlain(_valueNormalized), string);
}

auto LinearParameter::fromString(const TChar* string,
                                 ParamValue& _valueNormalized) const -> bool {
  // NOLINTNEXTLINE(build/include_what_you_use)
  UString wrapper(const_cast<TChar*>(string), tstrlen(string));
  if (wrapper.scanFloat(_valueNormalized)) {
    _valueNormalized = toNormalized(_valueNormalized);
    return true;
  }
  return false;
}

auto LinearParameter::toPlain(ParamValue _valueNormalized) const -> ParamValue {
  if (_valueNormalized <= 0.0) {
    return minPlain;
  }
  if (_valueNormalized >= 1.0) {
    return maxPlain;
  }
  if (info.stepCount <= 0) {
    return _valueNormalized * (maxPlain - minPlain) + minPlain;
  }
  return static_cast<double>(
             FromNormalized<ParamValue>(_valueNormalized, info.stepCount)) /
             static_cast<double>(info.stepCount) * (maxPlain - minPlain) +
         minPlain;
}

auto LinearParameter::toNormalized(ParamValue plainValue) const -> ParamValue {
  SMTG_ASSERT(maxPlain - minPlain != 0);
  if (plainValue <= minPlain) {
    return 0.0;
  }
  if (plainValue >= maxPlain) {
    return 1.0;
  }
  return (plainValue - minPlain) / (maxPlain - minPlain);
}

}  // namespace beatrice::vst
