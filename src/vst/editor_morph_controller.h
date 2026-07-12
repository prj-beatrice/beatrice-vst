// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_MORPH_CONTROLLER_H_
#define BEATRICE_VST_EDITOR_MORPH_CONTROLLER_H_

#include <vector>

#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/vsteditcontroller.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ccontrol.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/icontrollistener.h"

// Beatrice
#include "common/controller_core.h"

namespace beatrice::vst {

class MorphPadController final : public VSTGUI::IControlListener {
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;

 public:
  MorphPadController(const common::ControllerCore& core,
                     Steinberg::Vst::EditController& controller);

  void valueChanged(VSTGUI::CControl* control) override;
  void controlBeginEdit(VSTGUI::CControl* control) override;
  void controlEndEdit(VSTGUI::CControl* control) override;

 private:
  const common::ControllerCore& core_;
  Steinberg::Vst::EditController& controller_;
  std::vector<ParamID> edited_param_ids_;
  bool editing_ = false;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_MORPH_CONTROLLER_H_
