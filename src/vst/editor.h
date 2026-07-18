// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_H_
#define BEATRICE_VST_EDITOR_H_

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "vst3sdk/pluginterfaces/base/fplatform.h"
#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/vstguieditor.h"
#include "vst3sdk/vstgui4/vstgui/lib/cview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/model_config.h"
#include "common/voice_morph_state.h"
#include "vst/controls.h"

namespace beatrice::vst {

static constexpr auto kWindowWidth = 1280;
static constexpr auto kWindowHeight = 720;

class DescriptionPane;
class DescriptionPopupView;
class MorphFalloffSlider;
class MorphPadController;
class MorphPadView;
class VoiceMenuOverlayView;
class VoiceSelectorView;

// NOLINTNEXTLINE(misc-multiple-inheritance)
class Editor : public Steinberg::Vst::VSTGUIEditor, public IControlListener {
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;
  using PlatformType = VSTGUI::PlatformType;
  using CView = VSTGUI::CView;
  using CViewContainer = VSTGUI::CViewContainer;

 public:
  explicit Editor(void* controller);
  ~Editor() SMTG_OVERRIDE;
  auto PLUGIN_API open(void* parent, const PlatformType& platformType)
      -> bool SMTG_OVERRIDE;
  void PLUGIN_API close() SMTG_OVERRIDE;
  void beginEdit(Steinberg::int32 index) SMTG_OVERRIDE;
  void endEdit(Steinberg::int32 index) SMTG_OVERRIDE;
  void SyncValue(ParamID param_id, float plain_value);
  void SyncStringValue(ParamID param_id, const std::u8string& value);
  void valueChanged(CControl* pControl) SMTG_OVERRIDE;
  // auto notify(CBaseObject* sender,
  //                       const char* message) -> CMessageResult SMTG_OVERRIDE;

 private:
  static constexpr auto kPortraitWidth = 480;
  static constexpr auto kPortraitHeight = kPortraitWidth;
  void SyncSourcePitchRange();
  void SyncModelDescription();
  void SyncParameterAvailability();
  void SelectPage(int page);
  void SetPortraitDescriptionText(const std::u8string& text);
  void SetModelDescriptionText(const std::u8string& text);
  void SetVoiceDescriptionText(const std::u8string& text);
  void SetPortraitDescriptionMode(bool morphing);
  void SetVoiceSelectorDisplay(int voice_id);
  void ToggleVoiceMenu();
  void HideVoiceMenu();
  void RebuildVoiceMenu();
  void SelectVoice(int voice_id);
  [[nodiscard]] auto GetVoiceControl() const -> VSTGUI::COptionMenu*;
  void ShowDescriptionPopup(const char* title, const std::u8string& text,
                            CRect size);
  void HideDescriptionPopup();
  void UpdateVoiceMorphingDescription();
  void ApplyVoiceMorphState(const common::VoiceMorphState& state);
  void PerformParameterEdit(ParamID param_id, ParamValue normalized_value);
  void SendParameterEdit(ParamID param_id, ParamValue normalized_value);

  std::map<ParamID, CControl*> controls_;
  CFontRef font_, font_bold_, font_description_, font_small_;
  CFontRef font_heading_, font_strong_;
  std::optional<common::ModelConfig> model_config_;

  // Portrait / morph
  CView* portrait_view_ = nullptr;
  CView* unloaded_logo_view_ = nullptr;
  std::unique_ptr<MorphPadController> morph_pad_controller_;
  MorphPadView* morph_pad_view_ = nullptr;
  DescriptionPane* portrait_description_pane_ = nullptr;
  MorphFalloffSlider* morph_falloff_slider_ = nullptr;

  // Model / Voice Description
  DescriptionPane* model_description_pane_ = nullptr;
  DescriptionPane* voice_description_pane_ = nullptr;

  // Voice 選択
  VoiceSelectorView* voice_selector_ = nullptr;
  VoiceMenuOverlayView* voice_menu_overlay_ = nullptr;

  // Description popup
  DescriptionPopupView* description_popup_ = nullptr;

  // Header / Page
  CTextLabel* model_name_label_ = nullptr;
  std::array<CViewContainer*, 2> page_views_;
  std::array<CTextLabel*, 2> page_tabs_;
  CView* tab_indicator_ = nullptr;

  // Portrait bitmap cache
  std::map<std::u8string, SharedPointer<CBitmap>> portraits_;
  std::map<std::u8string, SharedPointer<CBitmap>> portrait_menu_thumbnails_;
  std::map<std::u8string, SharedPointer<CBitmap>> portrait_marker_thumbnails_;

  // Morphing parameters
  common::VoiceMorphState voice_morph_state_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_H_
