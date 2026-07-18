// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "vst/controller.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

#include "vst3sdk/pluginterfaces/base/fplatform.h"
#include "vst3sdk/pluginterfaces/base/funknown.h"
#include "vst3sdk/pluginterfaces/vst/ivstunits.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/public.sdk/source/vst/vsteditcontroller.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"

// Beatrice
#include "common/parameter_schema.h"
#include "common/parameter_state.h"
#include "vst/parameter.h"

namespace beatrice::vst {

using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::Vst::kRootUnitId;
using Steinberg::Vst::StringListParameter;

Controller::~Controller() = default;

auto PLUGIN_API Controller::initialize(FUnknown* const context) -> tresult {
  const tresult result = EditController::initialize(context);
  if (result != kResultTrue) {
    return kResultTrue;  // ???
  }

  for (const auto& [param_id, param] : common::kSchema) {
    const auto vst_param_id = static_cast<ParamID>(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      parameters.addParameter(new LinearParameter(
          Steinberg::Vst::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetName().c_str()))
              .c_str(),
          vst_param_id,
          Steinberg::Vst::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetUnits().c_str()))
              .c_str(),
          num_param->GetMinValue(), num_param->GetMaxValue(),
          num_param->GetDefaultValue(), num_param->GetDivisions(),
          num_param->GetFlags(), kRootUnitId,
          Steinberg::Vst::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetShortName().c_str()))
              .c_str()));
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      auto* const param = new StringListParameter(
          Steinberg::Vst::StringConvert::convert(
              reinterpret_cast<const char*>(list_param->GetName().c_str()))
              .c_str(),
          vst_param_id, nullptr, list_param->GetFlags(), kRootUnitId,
          Steinberg::Vst::StringConvert::convert(
              reinterpret_cast<const char*>(list_param->GetShortName().c_str()))
              .c_str());
      for (const auto& value : list_param->GetValues()) {
        param->appendString(Steinberg::Vst::StringConvert::convert(
                                reinterpret_cast<const char*>(value.c_str()))
                                .c_str());
      }
      parameters.addParameter(param);
    } else if (std::get_if<common::StringParameter>(&param)) {
    } else {
      assert(false);
    }
  }

  return kResultTrue;
}

// 状態を読み出す。
// Host 側から初期化時やプリセットロード時に呼ばれる。
// 不正なファイルパスなども構わず読み込む。
auto PLUGIN_API Controller::setComponentState(IBStream* const state)
    -> tresult {
  if (state == nullptr) {
    return kResultFalse;
  }
  int siz;
  if (state->read(&siz, sizeof(siz)) != kResultTrue) {
    return kResultFalse;
  }
  auto state_string = std::string();
  state_string.resize(siz);
  if (state->read(std::to_address(state_string.begin()), siz) != kResultTrue) {
    return kResultFalse;
  }
  auto iss = std::istringstream(state_string, std::ios::binary);
  common::ParameterState tmp_parameter_state;
  // Processor 側との整合性を維持するため、
  // Host から与えられた値はなるべくそのまま保持する。
  [[maybe_unused]] const auto error_code =
      tmp_parameter_state.ReadOrSetDefault(iss, common::kSchema);
  for (const auto& [param_id, param] : common::kSchema) {
    const auto vst_param_id = static_cast<ParamID>(param_id);
    const auto& value = tmp_parameter_state.GetValue(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      const auto normalized_value =
          Normalize(*num_param, std::get<double>(value));
      setParamNormalized(vst_param_id, normalized_value);
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto normalized_value =
          Normalize(*list_param, std::get<int>(value));
      setParamNormalized(vst_param_id, normalized_value);
    } else if (std::get_if<common::StringParameter>(&param)) {
      SetStringParameter(vst_param_id,
                         *std::get<std::unique_ptr<std::u8string>>(value));
    } else {
      assert(false);
    }
  }
  state->seek(0, IBStream::IStreamSeekMode::kIBSeekSet);
  return kResultTrue;
}

auto PLUGIN_API Controller::createView(const char* const name) -> IPlugView* {
  if (std::strcmp(name, "editor") == 0) {
    auto* const editor = new Editor(this);
    editors_.push_back(editor);
    return editor;
  }
  return nullptr;
}

void Controller::editorDestroyed(EditorView* const editor) {
  const auto itr = std::ranges::find(editors_, editor);
  if (itr == editors_.end()) {
    return;
  }
  *itr = editors_.back();
  editors_.pop_back();
}

// Host や Editor から呼ばれる
// ここで performEdit を呼ぶと param_id が異なっても
// DAW に拒否されるようなので、DAW からオートメーションなどで
// パラメータが操作された場合の他のパラメータとの連携には
// 定期的な同期が必要になりそうで、面倒なので諦める
auto PLUGIN_API Controller::setParamNormalized(
    const ParamID vst_param_id, const ParamValue normalized_value) -> tresult {
  const auto param_id = static_cast<common::ParameterID>(vst_param_id);
  const auto& param = common::kSchema.GetParameter(param_id);
  float plain_value_for_editor;
  if (const auto* const num_param =
          std::get_if<common::NumberParameter>(&param)) {
    const auto plain_value = Denormalize(*num_param, normalized_value);
    core_.parameter_state_.SetValue(param_id, plain_value);
    plain_value_for_editor = static_cast<float>(plain_value);
  } else if (const auto* const list_param =
                 std::get_if<common::ListParameter>(&param)) {
    const auto plain_value = Denormalize(*list_param, normalized_value);
    core_.parameter_state_.SetValue(param_id, plain_value);
    plain_value_for_editor = static_cast<float>(plain_value);
  } else if (std::get_if<common::StringParameter>(&param)) {
    return kResultFalse;
  } else {
    assert(false);
    return kResultFalse;
  }

  const auto result =
      EditController::setParamNormalized(vst_param_id, normalized_value);
  if (result != kResultTrue) {
    return result;
  }
  for (auto&& editor : editors_) {
    editor->SyncValue(vst_param_id, plain_value_for_editor);
  }

  return kResultTrue;
}

// setParamNormalized の文字列パラメータ版で、Editor から呼ばれる他、
// Host 側からも初期化時やプリセットロード時に
// setComponentState を通して呼ばれる。
void Controller::SetStringParameter(const ParamID vst_param_id,
                                    const std::u8string& value) {
  const auto param_id = static_cast<common::ParameterID>(vst_param_id);
  core_.parameter_state_.SetValue(param_id, value);

  for (auto&& editor : editors_) {
    editor->SyncStringValue(vst_param_id, value);
  }
}

}  // namespace beatrice::vst
