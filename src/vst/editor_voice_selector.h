// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_VOICE_SELECTOR_H_
#define BEATRICE_VST_EDITOR_VOICE_SELECTOR_H_

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "vst3sdk/vstgui4/vstgui/lib/cscrollview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/model_config.h"
#include "vst/controls.h"
#include "vst/editor_description.h"
#include "vst/editor_views.h"
#include "vst/surface_texture.h"

namespace beatrice::vst {

namespace voice_selector_detail {

[[nodiscard]] inline auto GetVoiceDisplayName(
    const common::ModelConfig::Voice& voice, const int voice_id)
    -> std::string {
  if (voice.name.empty()) {
    return "Voice " + std::to_string(voice_id + 1);
  }
  return {reinterpret_cast<const char*>(voice.name.data()), voice.name.size()};
}

}  // namespace voice_selector_detail

class VoiceSelectorView final : public CViewContainer {
 public:
  using ThumbnailMap = std::map<std::u8string, SharedPointer<CBitmap>>;
  using ToggleMenuAction = std::function<void()>;

  VoiceSelectorView(const CRect& rect, ToggleMenuAction toggle_menu_action,
                    CFontRef font_bold)
      : CViewContainer(rect),
        toggle_menu_action_(std::move(toggle_menu_action)) {
    setBackgroundColor(kTransparentCColor);
    setTransparency(true);

    portrait_ = new CView(CRect(7, 7, 49, 49));
    portrait_->setMouseEnabled(false);
    portrait_->setVisible(false);
    addView(portrait_);

    morph_icon_ = new MorphSelectorIconView(CRect(7, 7, 49, 49));
    morph_icon_->setMouseEnabled(false);
    morph_icon_->setVisible(false);
    addView(morph_icon_);

    name_ = new CTextLabel(CRect(60, 17, 398, 42), "", nullptr,
                           CParamDisplay::kNoFrame);
    name_->setBackColor(kTransparentCColor);
    name_->setFont(font_bold);
    name_->setFontColor(CColor(0xca, 0xc7, 0xc1));
    name_->setHoriAlign(CHoriTxtAlign::kLeftText);
    name_->setMouseEnabled(false);
    addView(name_);
  }

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isLeftButton() && toggle_menu_action_) {
      toggle_menu_action_();
      return VSTGUI::kMouseEventHandled;
    }
    return CViewContainer::onMouseDown(where, buttons);
  }

  void SetDisplay(const std::optional<common::ModelConfig>& model_config,
                  const ThumbnailMap& thumbnails, const int voice_id) {
    if (!name_ || !portrait_ || !morph_icon_) {
      return;
    }

    thumbnail_ = nullptr;
    portrait_->setBackground(nullptr);
    portrait_->setVisible(false);
    morph_icon_->setVisible(false);

    if (!model_config.has_value()) {
      name_->setText("");
      SetDisplayDirty();
      return;
    }

    const auto voice_count = common::GetVoiceCount(*model_config);
    if (voice_id < 0 || voice_id >= voice_count) {
      name_->setText("Voice Morphing Mode");
      if (voice_id < -1) {
        morph_icon_->setVisible(true);
      }
      SetDisplayDirty();
      return;
    }

    const auto& voice = model_config->voices[voice_id];
    const auto display_name =
        voice_selector_detail::GetVoiceDisplayName(voice, voice_id);
    name_->setText(display_name.c_str());
    if (const auto it = thumbnails.find(voice.portrait.path);
        it != thumbnails.end() && it->second) {
      thumbnail_ = it->second;
      portrait_->setBackground(thumbnail_.get());
      portrait_->setVisible(true);
    }
    SetDisplayDirty();
  }

 private:
  void SetDisplayDirty() {
    name_->setDirty();
    portrait_->setDirty();
    morph_icon_->setDirty();
  }

  ToggleMenuAction toggle_menu_action_;
  CView* portrait_ = nullptr;
  CView* morph_icon_ = nullptr;
  CTextLabel* name_ = nullptr;
  SharedPointer<CBitmap> thumbnail_;
};

class VoiceMenuOverlayView final : public CViewContainer {
 public:
  using ThumbnailMap = std::map<std::u8string, SharedPointer<CBitmap>>;
  using SelectVoiceAction = std::function<void(int)>;

  VoiceMenuOverlayView(const CRect& rect,
                       const SharedPointer<SurfaceBitmap>& panel_surface,
                       CFontRef font, SelectVoiceAction select_voice_action)
      : CViewContainer(rect),
        font_(font),
        select_voice_action_(std::move(select_voice_action)) {
    setBackgroundColor(kTransparentCColor);

    auto* const dismiss_overlay =
        new DismissOverlayView(CRect(0, 0, rect.getWidth(), rect.getHeight()),
                               [this]() -> void { HideMenu(); });
    addView(dismiss_overlay);

    menu_panel_ = new SurfacePanel(CRect(808, 242, 1250, 302), panel_surface,
                                   CColor(0xe2, 0xba, 0x79, 0x1a), 2.0);
    addView(menu_panel_);

    menu_scroll_ =
        new VSTGUI::CScrollView(CRect(8, 8, 434, 52), CRect(0, 0, 426, 44),
                                VSTGUI::CScrollView::kVerticalScrollbar |
                                    VSTGUI::CScrollView::kDontDrawFrame |
                                    VSTGUI::CScrollView::kOverlayScrollbars |
                                    VSTGUI::CScrollView::kAutoHideScrollbars,
                                6);
    ApplyScrollbarTheme(menu_scroll_);
    menu_scroll_->setBackgroundColor(kTransparentCColor);
    menu_scroll_->setTransparency(true);
    menu_panel_->addView(menu_scroll_);

    setVisible(false);
  }

  void ToggleMenu(const std::optional<common::ModelConfig>& model_config,
                  const ThumbnailMap& thumbnails, const int selected_voice_id) {
    if (isVisible()) {
      HideMenu();
      return;
    }
    if (!BuildMenu(model_config, thumbnails, selected_voice_id)) {
      return;
    }
    setVisible(true);
  }

  void HideMenu() {
    setVisible(false);
  }

  void RebuildMenu(const std::optional<common::ModelConfig>& model_config,
                   const ThumbnailMap& thumbnails,
                   const int selected_voice_id) {
    static_cast<void>(BuildMenu(model_config, thumbnails, selected_voice_id));
  }

  [[nodiscard]] auto IsMenuVisible() const -> bool { return isVisible(); }

 private:
  auto BuildMenu(const std::optional<common::ModelConfig>& model_config,
                 const ThumbnailMap& thumbnails, const int selected_voice_id)
      -> bool {
    if (!menu_panel_ || !menu_scroll_) {
      return false;
    }
    menu_scroll_->removeAll(true);
    if (!model_config.has_value()) {
      HideMenu();
      return false;
    }

    const auto voice_count = common::GetVoiceCount(*model_config);
    const auto has_morph = voice_count > 1;
    const auto entry_count = voice_count + (has_morph ? 1 : 0);
    if (entry_count == 0) {
      HideMenu();
      return false;
    }

    const auto columns = std::clamp((entry_count + 4) / 5, 1, 5);
    const auto rows = (entry_count + columns - 1) / columns;
    const auto item_min_width = 210.0;
    const auto item_gap = 6.0;
    const auto item_height = 52.0;
    const auto viewport_width =
        columns == 1 ? 426.0
                     : item_min_width * static_cast<double>(columns) +
                           item_gap * static_cast<double>(columns - 1);
    const auto item_width =
        (viewport_width - item_gap * static_cast<double>(columns - 1)) /
        static_cast<double>(columns);
    const auto content_height = rows * item_height;
    const auto viewport_height = std::min(360.0 - 16.0, content_height);
    const auto panel_height = viewport_height + 16.0;
    const auto panel_top = 242.0;
    const auto panel_right = 1250.0;
    const auto panel_width = viewport_width + 16.0;
    const auto panel_rect = CRect(panel_right - panel_width, panel_top,
                                  panel_right, panel_top + panel_height);
    menu_panel_->setViewSize(panel_rect);
    menu_panel_->setMouseableArea(panel_rect);
    menu_scroll_->setViewSize(
        CRect(8, 8, 8 + viewport_width, 8 + viewport_height));
    menu_scroll_->setMouseableArea(menu_scroll_->getViewSize());
    menu_scroll_->setContainerSize(CRect(0, 0, viewport_width, content_height));
    menu_scroll_->resetScrollOffset();

    const auto add_item =
        [&](const int entry_index, const int voice_id, const std::string& label,
            SharedPointer<CBitmap> thumbnail, const bool morph_item) -> void {
      const auto row = entry_index / columns;
      const auto column = entry_index % columns;
      const auto x = column * (item_width + item_gap);
      const auto y = row * item_height;
      auto* const item = new VoiceMenuItemView(
          CRect(x, y, x + item_width, y + 48.0), label, std::move(thumbnail),
          morph_item, selected_voice_id == voice_id, font_,
          [this, voice_id]() -> void {
            HideMenu();
            if (select_voice_action_) {
              select_voice_action_(voice_id);
            }
          });
      menu_scroll_->addView(item);
    };

    auto entry_index = 0;
    for (auto i = 0; i < voice_count; ++i) {
      const auto& voice = model_config->voices[i];
      SharedPointer<CBitmap> thumbnail = nullptr;
      if (const auto it = thumbnails.find(voice.portrait.path);
          it != thumbnails.end() && it->second) {
        thumbnail = it->second;
      }
      add_item(entry_index++, i,
               voice_selector_detail::GetVoiceDisplayName(voice, i), thumbnail,
               false);
    }
    if (has_morph) {
      add_item(entry_index, voice_count, "Voice Morphing Mode", nullptr, true);
    }
    menu_scroll_->invalid();
    menu_panel_->invalid();
    return true;
  }

  CFontRef font_;
  SelectVoiceAction select_voice_action_;
  SurfacePanel* menu_panel_ = nullptr;
  VSTGUI::CScrollView* menu_scroll_ = nullptr;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_VOICE_SELECTOR_H_
