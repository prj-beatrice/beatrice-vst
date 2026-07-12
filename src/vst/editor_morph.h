// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_MORPH_H_
#define BEATRICE_VST_EDITOR_MORPH_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "vst3sdk/vstgui4/vstgui/lib/cbitmap.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawmethods.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ccontrol.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/model_config.h"
#include "common/voice_morph_state.h"
#include "vst/controls.h"

namespace beatrice::vst {

using VSTGUI::kDrawFilled;
using VSTGUI::kDrawStroked;

using common::VoiceMorphMarker;
using common::VoiceMorphState;

class MorphPadView final : public CControl {
 public:
  MorphPadView(const CRect& rect, IControlListener* const listener,
               CFontRef label_font, CFontRef name_font)
      : CControl(rect, listener, -1),
        label_font_(label_font),
        name_font_(name_font) {
    if (label_font_) {
      label_font_->remember();
    }
    if (name_font_) {
      name_font_->remember();
    }
  }
  ~MorphPadView() override {
    if (label_font_) {
      label_font_->forget();
    }
    if (name_font_) {
      name_font_->forget();
    }
  }

  void SetVoices(std::vector<SharedPointer<CBitmap>> bitmaps,
                 std::vector<std::string> names) {
    voice_bitmaps_ = std::move(bitmaps);
    voice_names_ = std::move(names);
    voice_count_ = std::min(static_cast<int>(voice_bitmaps_.size()),
                            static_cast<int>(common::kMaxNSpeakers));
    invalid();
  }

  void SetState(const VoiceMorphState& state) {
    state_ = state;
    invalid();
  }

  [[nodiscard]] auto GetState() const -> const VoiceMorphState& {
    return state_;
  }

  void SetShowAuxiliaryLabels(const bool show) {
    if (show_auxiliary_labels_ == show) {
      return;
    }
    show_auxiliary_labels_ = show;
    invalid();
  }

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isRightButton()) {
      HandleRightClick(where);
      return VSTGUI::kMouseEventHandled;
    }
    if (!buttons.isLeftButton()) {
      return CView::onMouseDown(where, buttons);
    }
    auto move_cursor = false;
    if (HitCursor(where)) {
      drag_target_ = kCursorDragTarget;
    } else if (const auto marker_index = HitMarker(where); marker_index >= 0) {
      drag_target_ = marker_index;
    } else {
      drag_target_ = kCursorDragTarget;
      move_cursor = true;
    }
    state_before_edit_ = state_;
    beginEdit();
    if (move_cursor) {
      SetCursorFromPoint(where);
    }
    SetShowAuxiliaryLabels(true);
    return VSTGUI::kMouseEventHandled;
  }

  auto onMouseMoved(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (!buttons.isLeftButton() || drag_target_ == kNoDragTarget) {
      return CView::onMouseMoved(where, buttons);
    }
    if (drag_target_ == kCursorDragTarget) {
      SetCursorFromPoint(where);
    } else if (IsMarkerDragTarget()) {
      const auto position = PointToNormalized(where);
      state_.markers[drag_target_].x = position.x;
      state_.markers[drag_target_].y = position.y;
      NotifyStateChanged();
      invalid();
    }
    return VSTGUI::kMouseEventHandled;
  }

  auto onMouseUp(CPoint&, const CButtonState&) -> CMouseEventResult override {
    if (drag_target_ != kNoDragTarget) {
      endEdit();
    }
    drag_target_ = kNoDragTarget;
    SetShowAuxiliaryLabels(false);
    return VSTGUI::kMouseEventHandled;
  }

  auto onMouseCancel() -> CMouseEventResult override {
    if (drag_target_ != kNoDragTarget) {
      state_ = state_before_edit_;
      NotifyStateChanged();
      endEdit();
      invalid();
    }
    drag_target_ = kNoDragTarget;
    SetShowAuxiliaryLabels(false);
    return VSTGUI::kMouseEventHandled;
  }

  void draw(CDrawContext* const context) override {
    auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    context->setFillColor(CColor(0x0f, 0x0e, 0x0d));
    context->drawRect(rect, kDrawFilled);

    auto grid = rect;
    grid.inset(0.5, 0.5);
    context->setFillColor(CColor(0x10, 0x0f, 0x0e));
    context->setFrameColor(CColor(0xc3, 0xa0, 0x66, 0x52));
    context->drawRect(grid, kDrawFilledAndStroked);
    for (int i = 1; i < 12; ++i) {
      const auto x = grid.left + grid.getWidth() * i / 12.0;
      const auto y = grid.top + grid.getHeight() * i / 12.0;
      context->setFrameColor(CColor(0xc3, 0xa0, 0x66, 0x24));
      context->drawLine(CPoint(x, grid.top), CPoint(x, grid.bottom));
      context->drawLine(CPoint(grid.left, y), CPoint(grid.right, y));
    }
    context->setFrameColor(CColor(0xc3, 0xa0, 0x66, 0x38));
    context->drawLine(CPoint(grid.left + grid.getWidth() / 2.0, grid.top),
                      CPoint(grid.left + grid.getWidth() / 2.0, grid.bottom));
    context->drawLine(CPoint(grid.left, grid.top + grid.getHeight() / 2.0),
                      CPoint(grid.right, grid.top + grid.getHeight() / 2.0));

    const auto center = NormalizedToPoint(state_.cursor_x, state_.cursor_y);
    const auto marker_weights = state_.CalculateMarkerWeights();
    for (auto i = 0; i < state_.marker_count; ++i) {
      if (marker_weights[i] < common::kVoiceMorphWeightThreshold) {
        continue;
      }
      const auto point =
          NormalizedToPoint(state_.markers[i].x, state_.markers[i].y);
      auto t =
          std::clamp((marker_weights[i] - common::kVoiceMorphWeightThreshold) /
                         (1.0f - common::kVoiceMorphWeightThreshold),
                     0.0f, 1.0f);
      t = std::pow(t, 0.45f);
      t = t * t * (3.0f - 2.0f * t);
      constexpr auto kMinLineAlpha = 0x18;
      constexpr auto kMaxLineAlpha = 0xf8;
      const auto alpha = static_cast<uint8_t>(std::clamp(
          kMinLineAlpha + static_cast<int>(t * 208.0f), 0, kMaxLineAlpha));
      const auto line_red = static_cast<uint8_t>(
          std::clamp(0xc3 + static_cast<int>(t * 0x3c), 0, 0xff));
      if (t > 0.28f) {
        const auto halo_alpha = static_cast<uint8_t>(
            std::clamp(static_cast<int>(t * 0x48), 0, 0x48));
        context->setFrameColor(CColor(line_red, 0xa0, 0x66, halo_alpha));
        context->setLineWidth(2.5 + t * 2.5);
        context->drawLine(center, point);
      }
      context->setFrameColor(CColor(line_red, 0xa0, 0x66, alpha));
      context->setLineWidth(0.55 + t * 3.15);
      context->drawLine(center, point);
    }
    context->setLineWidth(1);
    const auto voice_bitmap_count = static_cast<int>(voice_bitmaps_.size());
    for (auto i = 0; i < state_.marker_count; ++i) {
      const auto& marker_state = state_.markers[i];
      const auto point = NormalizedToPoint(marker_state.x, marker_state.y);
      const auto marker =
          CRect(point.x - 29, point.y - 29, point.x + 29, point.y + 29);
      context->setFillColor(CColor(0x1d, 0x19, 0x14));
      context->setFrameColor(CColor(0xeb, 0xca, 0x89, 0x84));
      context->drawEllipse(marker, kDrawFilledAndStroked);
      const auto voice_id = marker_state.voice_id;
      if (voice_id >= 0 && voice_id < voice_bitmap_count &&
          voice_bitmaps_[voice_id]) {
        voice_bitmaps_[voice_id]->draw(context, marker);
      }
      context->setFrameColor(CColor(0xeb, 0xca, 0x89, 0x84));
      context->drawEllipse(marker, kDrawStroked);
      context->setFillColor(CColor(0xd8, 0xb3, 0x6c));
      context->setFrameColor(CColor(0x15, 0x11, 0x0a, 0xa0));
      context->drawEllipse(
          CRect(point.x + 10, point.y + 10, point.x + 28, point.y + 28),
          kDrawFilledAndStroked);
      const auto label = std::to_string(i + 1);
      context->setFont(label_font_);
      context->setFontColor(CColor(0x17, 0x12, 0x0a));
      context->drawString(
          label.c_str(),
          CRect(point.x + 10, point.y + 10, point.x + 28, point.y + 28),
          CHoriTxtAlign::kCenterText);
    }
    context->setFrameColor(CColor(0x15, 0x11, 0x0a));
    context->drawLine(CPoint(center.x, center.y - 26),
                      CPoint(center.x, center.y + 26));
    context->drawLine(CPoint(center.x - 26, center.y),
                      CPoint(center.x + 26, center.y));
    context->setFillColor(CColor(0xd8, 0xb3, 0x6c));
    context->setFrameColor(CColor(0x06, 0x06, 0x05));
    context->drawEllipse(
        CRect(center.x - 14, center.y - 14, center.x + 14, center.y + 14),
        kDrawFilledAndStroked);
    if (show_auxiliary_labels_) {
      for (auto i = 0; i < state_.marker_count; ++i) {
        const auto& marker = state_.markers[i];
        DrawVoiceName(context, NormalizedToPoint(marker.x, marker.y),
                      marker.voice_id);
      }
      for (auto i = 0; i < state_.marker_count; ++i) {
        if (marker_weights[i] < common::kVoiceMorphWeightThreshold) {
          continue;
        }
        const auto point =
            NormalizedToPoint(state_.markers[i].x, state_.markers[i].y);
        DrawWeightLabel(context, center, point, marker_weights[i]);
      }
    }
    context->restoreGlobalState();
    setDirty(false);
  }

  // NOLINTNEXTLINE(modernize-use-trailing-return-type)
  CLASS_METHODS_NOCOPY(MorphPadView, CControl)

 private:
  static constexpr auto kNoDragTarget = -2;
  static constexpr auto kCursorDragTarget = -1;

  [[nodiscard]] auto NormalizedToPoint(const float x, const float y) const
      -> CPoint {
    auto rect = getViewSize();
    rect.inset(0.5, 0.5);
    return {rect.left + rect.getWidth() * x, rect.top + rect.getHeight() * y};
  }

  [[nodiscard]] auto PointToNormalized(const CPoint& point) const -> CPoint {
    auto rect = getViewSize();
    rect.inset(0.5, 0.5);
    return {std::clamp((point.x - rect.left) / rect.getWidth(), 0.0, 1.0),
            std::clamp((point.y - rect.top) / rect.getHeight(), 0.0, 1.0)};
  }

  [[nodiscard]] auto HitMarker(const CPoint& point) const -> int {
    for (auto i = state_.marker_count - 1; i >= 0; --i) {
      const auto marker = state_.markers[i];
      const auto marker_point = NormalizedToPoint(marker.x, marker.y);
      const auto dx = marker_point.x - point.x;
      const auto dy = marker_point.y - point.y;
      if (dx * dx + dy * dy <= 34.0 * 34.0) {
        return i;
      }
    }
    return -1;
  }

  [[nodiscard]] auto HitCursor(const CPoint& point) const -> bool {
    const auto cursor = NormalizedToPoint(state_.cursor_x, state_.cursor_y);
    const auto dx = cursor.x - point.x;
    const auto dy = cursor.y - point.y;
    return dx * dx + dy * dy <= 28.0 * 28.0;
  }

  void SetCursorFromPoint(const CPoint& point) {
    const auto position = PointToNormalized(point);
    state_.cursor_x = position.x;
    state_.cursor_y = position.y;
    NotifyStateChanged();
    invalid();
  }

  void HandleRightClick(const CPoint& point) {
    if (voice_count_ <= 0) {
      return;
    }
    const auto marker_index = HitMarker(point);
    if (marker_index >= 0) {
      ShowMarkerMenu(point, marker_index);
      return;
    }
    ShowPadMenu(point);
  }

  void ShowPadMenu(const CPoint& point) {
    if (state_.marker_count >= common::kMaxNVoiceMorphMarkers) {
      return;
    }
    auto* const frame = getFrame();
    if (!frame) {
      return;
    }

    auto menu = VSTGUI::owned(new VSTGUI::COptionMenu());
    menu->addEntry("Add Marker");
    const auto frame_point = translateToGlobal(point);
    const auto self = VSTGUI::shared(this);
    menu->popup(
        frame, frame_point, [self, point](VSTGUI::COptionMenu* popup) -> void {
          if (!self->isAttached() || !popup || popup->getLastResult() < 0) {
            return;
          }
          if (self->state_.marker_count >= common::kMaxNVoiceMorphMarkers) {
            return;
          }
          const auto position = self->PointToNormalized(point);
          self->beginEdit();
          self->state_.markers[self->state_.marker_count] =
              VoiceMorphMarker{.voice_id = self->NextVoiceId(-1, -1),
                               .x = static_cast<float>(position.x),
                               .y = static_cast<float>(position.y)};
          ++self->state_.marker_count;
          self->NotifyStateChanged();
          self->endEdit();
          self->invalid();
        });
  }

  void ShowMarkerMenu(const CPoint& point, const int marker_index) {
    auto* const frame = getFrame();
    if (!frame) {
      return;
    }

    auto menu = VSTGUI::owned(new VSTGUI::COptionMenu());
    auto* const title_item = new VSTGUI::CMenuItem("Assign Voice", -1);
    title_item->setIsTitle(true);
    menu->addEntry(title_item);
    const auto voice_name_count = static_cast<int>(voice_names_.size());
    for (auto voice_id = 0; voice_id < voice_count_; ++voice_id) {
      const auto title =
          voice_id < voice_name_count && !voice_names_[voice_id].empty()
              ? voice_names_[voice_id]
              : "Voice " + std::to_string(voice_id + 1);
      auto* const item = new VSTGUI::CMenuItem(title.c_str(), voice_id);
      item->setChecked(state_.markers[marker_index].voice_id == voice_id);
      menu->addEntry(item);
    }
    menu->addSeparator();
    constexpr auto kRemoveMarkerTag = 10000;
    menu->addEntry(new VSTGUI::CMenuItem("Remove Marker", kRemoveMarkerTag));

    const auto frame_point = translateToGlobal(point);
    const auto self = VSTGUI::shared(this);
    menu->popup(frame, frame_point,
                [self, marker_index](VSTGUI::COptionMenu* popup) -> void {
                  if (!self->isAttached() || !popup) {
                    return;
                  }
                  if (marker_index >= self->state_.marker_count) {
                    return;
                  }
                  const auto selected_index = popup->getLastResult();
                  if (selected_index < 0) {
                    return;
                  }
                  auto* const selected_item = popup->getEntry(selected_index);
                  if (!selected_item) {
                    return;
                  }
                  const auto tag = selected_item->getTag();
                  if (tag == kRemoveMarkerTag) {
                    if (self->state_.marker_count <= 1) {
                      return;
                    }
                    self->beginEdit();
                    for (auto i = marker_index;
                         i < self->state_.marker_count - 1; ++i) {
                      self->state_.markers[i] = self->state_.markers[i + 1];
                    }
                    --self->state_.marker_count;
                  } else if (tag >= 0 && tag < self->voice_count_) {
                    if (self->state_.markers[marker_index].voice_id == tag) {
                      return;
                    }
                    self->beginEdit();
                    self->state_.markers[marker_index].voice_id = tag;
                  } else {
                    return;
                  }
                  self->NotifyStateChanged();
                  self->endEdit();
                  self->invalid();
                });
  }

  [[nodiscard]] auto NextVoiceId(const int current,
                                 const int marker_index) const -> int {
    for (auto step = 1; step <= voice_count_; ++step) {
      const auto candidate = (current + step + voice_count_) % voice_count_;
      if (!IsVoiceUsed(candidate, marker_index)) {
        return candidate;
      }
    }
    return std::clamp(current, 0, std::max(voice_count_ - 1, 0));
  }

  [[nodiscard]] auto IsVoiceUsed(const int voice_id,
                                 const int ignored_marker_index) const -> bool {
    for (auto i = 0; i < state_.marker_count; ++i) {
      if (i != ignored_marker_index && state_.markers[i].voice_id == voice_id) {
        return true;
      }
    }
    return false;
  }

  void NotifyStateChanged() { valueChanged(); }

  [[nodiscard]] auto IsMarkerDragTarget() const -> bool {
    return drag_target_ >= 0 && drag_target_ < state_.marker_count;
  }

  void DrawWeightLabel(CDrawContext* const context, const CPoint& begin,
                       const CPoint& end, const float weight) const {
    const auto percent =
        std::clamp(static_cast<int>(std::round(weight * 100.0f)), 0, 100);
    const auto text = std::to_string(percent) + '%';

    const auto center_x = (begin.x + end.x) * 0.5;
    const auto center_y = (begin.y + end.y) * 0.5;
    auto label_rect = CRect(center_x - 22.0, center_y - 10.0, center_x + 22.0,
                            center_y + 10.0);
    context->setFillColor(CColor(0x07, 0x06, 0x05, 0xd8));
    context->setFrameColor(CColor(0xe0, 0xb8, 0x74, 0x70));
    context->setLineWidth(1);
    if (auto path = VSTGUI::owned(
            context->createRoundRectGraphicsPath(label_rect, 4.0))) {
      context->drawGraphicsPath(path, CDrawContext::kPathFilled);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    context->setFont(label_font_);
    context->setFontColor(CColor(0xea, 0xe4, 0xda));
    context->drawString(text.c_str(), label_rect, CHoriTxtAlign::kCenterText,
                        true);
  }

  void DrawVoiceName(CDrawContext* const context, const CPoint& marker,
                     const int voice_id) const {
    const auto voice_name_count = static_cast<int>(voice_names_.size());
    if (!name_font_ || voice_id < 0 || voice_id >= voice_name_count) {
      return;
    }
    const auto& name = voice_names_[voice_id];
    if (name.empty()) {
      return;
    }

    constexpr auto kLabelWidth = 120.0;
    constexpr auto kLabelHeight = 17.0;
    constexpr auto kMarkerGap = 33.0;
    const auto left = marker.x - kLabelWidth / 2.0;
    const auto top = marker.y - kMarkerGap - kLabelHeight;
    const auto label_rect =
        CRect(left, top, left + kLabelWidth, top + kLabelHeight);
    const auto text = VSTGUI::CDrawMethods::createTruncatedText(
        VSTGUI::CDrawMethods::kTextTruncateTail, UTF8String(name), name_font_,
        kLabelWidth);
    context->setFont(name_font_);
    context->setFontColor(CColor(0xc1, 0xbe, 0xb8, 0xa0));
    context->drawString(text.getPlatformString(), label_rect,
                        CHoriTxtAlign::kCenterText, true);
  }

  CFontRef label_font_;
  CFontRef name_font_;
  int voice_count_ = 0;
  int drag_target_ = kNoDragTarget;
  bool show_auxiliary_labels_ = false;
  VoiceMorphState state_;
  VoiceMorphState state_before_edit_;
  std::vector<SharedPointer<CBitmap>> voice_bitmaps_;
  std::vector<std::string> voice_names_;
};

class MorphFalloffSlider final : public Slider {
 public:
  MorphFalloffSlider(const CRect& rect, IControlListener* const listener,
                     const int32_t tag, CBitmap* const handle,
                     CBitmap* const background, CFontRef font,
                     CFontRef value_font)
      : Slider(rect, listener, tag, static_cast<int>(rect.left),
               static_cast<int>(rect.right - handle->getWidth()), handle,
               background, "", font, value_font, "Morph Falloff", 1) {
    setMin(common::kVoiceMorphFalloffMin);
    setMax(common::kVoiceMorphFalloffMax);
    setWheelInc(common::kVoiceMorphFalloffStep);
    setFineWheelInc(common::kVoiceMorphFalloffStep);
    setDefaultValue(common::kVoiceMorphFalloffDefault);
    setValue(common::kVoiceMorphFalloffDefault);
  }

  void SetValue(const float value) {
    setValue(value);
    invalid();
  }

  void SetAuxiliaryLabelStateChangedCallback(std::function<void(bool)> cb) {
    auxiliary_label_state_changed_callback_ = std::move(cb);
  }

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (!buttons.isLeftButton()) {
      return Slider::onMouseDown(where, buttons);
    }
    dragging_ = true;
    NotifyAuxiliaryLabelStateChanged();
    return Slider::onMouseDown(where, buttons);
  }

  auto onMouseUp(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    const auto result = Slider::onMouseUp(where, buttons);
    dragging_ = false;
    NotifyAuxiliaryLabelStateChanged();
    return result;
  }

  auto onMouseCancel() -> CMouseEventResult override {
    const auto result = Slider::onMouseCancel();
    dragging_ = false;
    NotifyAuxiliaryLabelStateChanged();
    return result;
  }

  void takeFocus() override {
    Slider::takeFocus();
    focused_ = true;
    NotifyAuxiliaryLabelStateChanged();
  }

  void looseFocus() override {
    Slider::looseFocus();
    focused_ = false;
    NotifyAuxiliaryLabelStateChanged();
  }

 protected:
  [[nodiscard]] auto GetTrackYOffset() const -> CCoord override { return 27.0; }

 private:
  void NotifyAuxiliaryLabelStateChanged() {
    if (auxiliary_label_state_changed_callback_) {
      auxiliary_label_state_changed_callback_(dragging_ || focused_);
    }
  }

  bool dragging_ = false;
  bool focused_ = false;
  std::function<void(bool)> auxiliary_label_state_changed_callback_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_MORPH_H_
