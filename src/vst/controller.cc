// Copyright (c) 2024 Project Beatrice

#include "vst/controller.h"

#include <sstream>
#include <string>

#include "vst3sdk/pluginterfaces/base/funknown.h"
#include "vst3sdk/pluginterfaces/vst/ivstunits.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"

// Beatrice
#include "vst/parameter.h"

namespace beatrice::vst {

using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::Vst::kRootUnitId;
using Steinberg::Vst::StringListParameter;

Controller::~Controller() {
  for (auto&& editor : editors_) {
    editor->forget();
  }
}

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
          VST3::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetName().c_str()))
              .c_str(),
          vst_param_id,
          VST3::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetUnits().c_str()))
              .c_str(),
          num_param->GetMinValue(), num_param->GetMaxValue(),
          num_param->GetDefaultValue(), num_param->GetDivisions(),
          num_param->GetFlags(), kRootUnitId,
          VST3::StringConvert::convert(
              reinterpret_cast<const char*>(num_param->GetShortName().c_str()))
              .c_str()));
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      auto* const param = new StringListParameter(
          VST3::StringConvert::convert(
              reinterpret_cast<const char*>(list_param->GetName().c_str()))
              .c_str(),
          vst_param_id, nullptr, list_param->GetFlags(), kRootUnitId,
          VST3::StringConvert::convert(
              reinterpret_cast<const char*>(list_param->GetShortName().c_str()))
              .c_str());
      for (const auto& value : list_param->GetValues()) {
        param->appendString(VST3::StringConvert::convert(
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

// 状態を読み出す
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
  if (core_.Read(iss) != common::ErrorCode::kSuccess) {
    return kResultFalse;
  }
  for (const auto& [param_id, param] : common::kSchema) {
    const auto vst_param_id = static_cast<ParamID>(param_id);
    const auto& value = core_.parameter_state_.GetValue(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      const auto normalized_value =
          plainParamToNormalized(vst_param_id, std::get<double>(value));
      EditController::setParamNormalized(vst_param_id, normalized_value);
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto normalized_value = plainParamToNormalized(
          vst_param_id, static_cast<double>(std::get<int>(value)));
      EditController::setParamNormalized(vst_param_id, normalized_value);
    } else if (std::get_if<common::StringParameter>(&param)) {
    } else {
      assert(false);
    }
  }
  state->seek(0, IBStream::IStreamSeekMode::kIBSeekSet);
  return kResultTrue;
}

auto PLUGIN_API Controller::createView(const char* const name) -> IPlugView* {
  if (strcmp(name, "editor") == 0) {
    auto* const editor = new Editor(this);
    editor->remember();
    editors_.push_back(editor);
    return editor;
  }
  return nullptr;
}

void Controller::editorDestroyed(EditorView* const editor) {
  const auto itr = std::find(editors_.begin(), editors_.end(), editor);
  if (itr == editors_.end()) {
    return;
  }
  (*itr)->forget();
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
  if (const auto* const num_param =
          std::get_if<common::NumberParameter>(&param)) {
    core_.parameter_state_.SetValue(param_id,
                                    Denormalize(*num_param, normalized_value));
  } else if (const auto* const list_param =
                 std::get_if<common::ListParameter>(&param)) {
    core_.parameter_state_.SetValue(param_id,
                                    Denormalize(*list_param, normalized_value));
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
    editor->SyncValue(vst_param_id, normalized_value);
  }

  return kResultTrue;
}

// GUI 側から呼ばれて StringParameter の変更を反映させる
auto Controller::SetStringParameter(const ParamID vst_param_id,
                                    const std::u8string& value)
    -> common::ErrorCode {
  const auto param_id = static_cast<common::ParameterID>(vst_param_id);
  const auto param =
      std::get<common::StringParameter>(common::kSchema.GetParameter(param_id));
  core_.parameter_state_.SetValue(param_id, value);

  // processor に通知
  auto* const msg = allocateMessage();
  msg->setMessageID("param_change");
  msg->getAttributes()->setBinary("param_id", &vst_param_id,
                                  sizeof(vst_param_id));
  msg->getAttributes()->setBinary("data", value.c_str(), value.size());
  sendMessage(msg);
  msg->release();

  for (auto&& editor : editors_) {
    editor->SyncStringValue(vst_param_id, value);
  }

  return common::ErrorCode::kSuccess;
}

}  // namespace beatrice::vst
