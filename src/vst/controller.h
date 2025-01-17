// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_CONTROLLER_H_
#define BEATRICE_VST_CONTROLLER_H_

#include <vector>

#include "vst3sdk/pluginterfaces/base/ftypes.h"
#include "vst3sdk/pluginterfaces/base/ibstream.h"
#include "vst3sdk/public.sdk/source/vst/vsteditcontroller.h"

// Beatrice
#include "common/controller_core.h"
#include "common/error.h"
#include "vst/editor.h"

namespace beatrice::vst {

class Controller : public Steinberg::Vst::EditController {
  using tresult = Steinberg::tresult;
  using IBStream = Steinberg::IBStream;
  using IPlugView = Steinberg::IPlugView;
  using EditorView = Steinberg::Vst::EditorView;
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;

 public:
  ~Controller() override;

  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto createInstance(void*) -> FUnknown* {
    return static_cast<IEditController*>(new Controller());
  }

  // from IPluginBase
  auto PLUGIN_API initialize(FUnknown* context) -> tresult SMTG_OVERRIDE;

  // from EditController
  auto PLUGIN_API setComponentState(IBStream* state) -> tresult SMTG_OVERRIDE;

  auto PLUGIN_API createView(const char* name) -> IPlugView* SMTG_OVERRIDE;

  void editorDestroyed(EditorView* editorView) SMTG_OVERRIDE;

  auto PLUGIN_API setParamNormalized(ParamID param_id, ParamValue value)
      -> tresult SMTG_OVERRIDE;

 private:
  common::ControllerCore core_;
  std::vector<Editor*> editors_;

  auto SetStringParameter(ParamID, const std::u8string&) -> common::ErrorCode;
  friend Editor;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_CONTROLLER_H_
