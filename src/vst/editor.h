// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_H_
#define BEATRICE_VST_EDITOR_H_

#include <map>
#include <optional>
#include <vector>

#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/vstguieditor.h"
#include "vst3sdk/vstgui4/vstgui/lib/cscrollview.h"
#include "vst3sdk/vstgui4/vstgui/lib/ctabview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cview.h"

// Beatrice
#include "common/model_config.h"
#include "vst/controls.h"

namespace beatrice::vst {

static constexpr auto kWindowWidth = 1280;
static constexpr auto kWindowHeight = 720;

class Editor : public Steinberg::Vst::VSTGUIEditor, public IControlListener {
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;
  using PlatformType = VSTGUI::PlatformType;
  using CView = VSTGUI::CView;

 public:
  explicit Editor(void* controller);
  ~Editor() SMTG_OVERRIDE;
  auto PLUGIN_API open(void* parent, const PlatformType& platformType)
      -> bool SMTG_OVERRIDE;
  void PLUGIN_API close() SMTG_OVERRIDE;
  void SyncValue(ParamID param_id, ParamValue value);
  void SyncStringValue(ParamID param_id, const std::u8string& value);
  void valueChanged(CControl* pControl) SMTG_OVERRIDE;
  // auto notify(CBaseObject* sender,
  //                       const char* message) -> CMessageResult SMTG_OVERRIDE;

 private:
  static constexpr auto kHeaderHeight = 56;
  static constexpr auto kFooterHeight = 32;
  static constexpr auto kColumnMerginY = 0;
  static constexpr auto kColumnMerginX = 1;
  static constexpr auto kColumnWidth = 400 - kColumnMerginX;
  static constexpr auto kInnerColumnMerginY = 12;
  static constexpr auto kInnerColumnMerginX = 12;
  static constexpr auto kGroupLabelMerginY = 12;
  static constexpr auto kGroupIndentX = 4;
  static constexpr auto kElementWidth = 224;
  static constexpr auto kElementHeight = 24;
  static constexpr auto kElementMerginY = 8;
  static constexpr auto kElementMerginX = 8;
  static constexpr auto kLabelWidth =
      kColumnWidth - 2 * (kInnerColumnMerginX + kGroupIndentX) - kElementWidth -
      kElementMerginX;
  static constexpr auto kPortraitColumnWidth =
      kWindowWidth - 2 * (kColumnWidth + kColumnMerginX);
  static constexpr auto kPortraitWidth = kPortraitColumnWidth;
  static constexpr auto kPortraitHeight = kPortraitWidth;
  struct Context {
    int y = kHeaderHeight + kColumnMerginY;
    int x = 0;
    int column_start_y = -1;
    int column_start_x = -1;
    int column_width = -1;
    CColor column_back_color;
    int last_element_mergin = 0;
    bool first_group = true;
    std::vector<CView*> column_elements;
  };
  void SyncModelDescription();
  static void BeginColumn(Context&, int width, const CColor& back_color);
  auto EndColumn(Context&) -> CView*;
  auto BeginGroup(Context&, const std::u8string& name) -> CView*;
  static void EndGroup(Context&);
  auto MakeSlider(Context&, ParamID param_id, int precision = 1) -> CView*;
  auto MakeCombobox(Context&, ParamID, const CColor&, const CColor&) -> CView*;
  auto MakeFileSelector(Context&, ParamID param_id) -> CView*;
  auto MakePortraitView(Context&) -> CView*;
  auto MakeModelVoiceDescription(Context&) -> CView*;
  auto MakePortraitDescription(Context&) -> CView*;
  static void BeginTabColumn(Context&, int width, const CColor& back_color);
  auto EndTabColumn(Context&) -> CView*;
  auto MakeVoiceMorphingView(Context&) -> CView*;
  void SyncVoiceMorphingDescription();

  std::map<ParamID, CControl*> controls_;
  CFontRef font_, font_bold_;
  std::optional<common::ModelConfig> model_config_;

  ModelVoiceDescription* model_voice_description_;

  VSTGUI::CTabView* tab_view_;

  CView* portrait_view_;
  CMultiLineTextLabel* portrait_description_;

  std::map<std::u8string, SharedPointer<CBitmap>> portraits_;

  std::array<CTextLabel*, common::kMaxNSpeakers> morphing_labels_;
  VSTGUI::CScrollView* morphing_weights_view_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_H_
