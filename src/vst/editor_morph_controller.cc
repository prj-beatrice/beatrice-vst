// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "vst/editor_morph_controller.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <variant>

#include "vst3sdk/pluginterfaces/base/funknown.h"
#include "vst3sdk/public.sdk/source/vst/vsteditcontroller.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ccontrol.h"

// Beatrice
#include "common/parameter_schema.h"
#include "common/voice_morph_parameter.h"
#include "vst/editor_morph.h"
#include "vst/parameter.h"

namespace beatrice::vst {

MorphPadController::MorphPadController(
    const common::ControllerCore& core,
    Steinberg::Vst::EditController& controller)
    : core_(core), controller_(controller) {}

void MorphPadController::valueChanged(VSTGUI::CControl* const control) {
  assert(editing_);
  if (!editing_) {
    return;
  }
  auto* const morph_pad = dynamic_cast<MorphPadView*>(control);
  assert(morph_pad);
  if (!morph_pad) {
    return;
  }

  constexpr auto kEpsilon = 0.000001;
  for (const auto [param_id, value] :
       common::GetVoiceMorphParameterValues(morph_pad->GetState())) {
    const auto* const num_param = std::get_if<common::NumberParameter>(
        &common::kSchema.GetParameter(param_id));
    assert(num_param);
    if (!num_param) {
      continue;
    }
    const auto normalized_value = Normalize(*num_param, value);
    const auto plain_value = Denormalize(*num_param, normalized_value);
    if (std::abs(std::get<double>(core_.parameter_state_.GetValue(param_id)) -
                 plain_value) < kEpsilon) {
      continue;
    }

    const auto vst_param_id = static_cast<ParamID>(param_id);
    if (std::ranges::find(edited_param_ids_, vst_param_id) ==
        edited_param_ids_.end()) {
      controller_.beginEdit(vst_param_id);
      edited_param_ids_.push_back(vst_param_id);
    }
    if (controller_.setParamNormalized(vst_param_id, normalized_value) ==
        Steinberg::kResultTrue) {
      controller_.performEdit(vst_param_id,
                              controller_.getParamNormalized(vst_param_id));
    }
  }
}

void MorphPadController::controlBeginEdit(VSTGUI::CControl* const control) {
  assert(dynamic_cast<MorphPadView*>(control));
  assert(!editing_);
  if (editing_) {
    return;
  }
  editing_ = true;
  edited_param_ids_.clear();
  controller_.startGroupEdit();
}

void MorphPadController::controlEndEdit(VSTGUI::CControl* const control) {
  assert(dynamic_cast<MorphPadView*>(control));
  assert(editing_);
  if (!editing_) {
    return;
  }
  for (const auto param_id : edited_param_ids_) {
    controller_.endEdit(param_id);
  }
  controller_.finishGroupEdit();
  edited_param_ids_.clear();
  editing_ = false;
}

}  // namespace beatrice::vst
