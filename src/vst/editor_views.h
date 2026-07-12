// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_VIEWS_H_
#define BEATRICE_VST_EDITOR_VIEWS_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/cview.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "vst/controls.h"

namespace beatrice::vst {

using VSTGUI::CView;
using VSTGUI::kDrawFilled;
using VSTGUI::kDrawStroked;

class TabAccentView final : public CView {
 public:
  explicit TabAccentView(const CRect& size) : CView(size) {}

  void draw(CDrawContext* const context) override {
    auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    const auto line_left = rect.left + 24.0;
    const auto line_right = rect.right - 24.0;
    const auto line_top = rect.bottom - 3.0;
    const auto line_bottom = rect.bottom;
    static constexpr auto kSegments = 64;
    for (auto i = 0; i < kSegments; ++i) {
      const auto t0 = static_cast<double>(i) / kSegments;
      const auto t1 = static_cast<double>(i + 1) / kSegments;
      const auto mid = (t0 + t1) * 0.5;
      const auto alpha = static_cast<uint8_t>(
          std::clamp(255.0 * (1.0 - std::abs(mid - 0.5) * 2.0), 0.0, 255.0));
      context->setFillColor(CColor(0xf0, 0xcf, 0x90, alpha));
      context->drawRect(
          CRect(line_left + (line_right - line_left) * t0, line_top,
                line_left + (line_right - line_left) * t1 + 0.35, line_bottom),
          kDrawFilled);
    }
    context->restoreGlobalState();
    setDirty(false);
  }
};

inline void DrawMorphGridIcon(CDrawContext* const context, CRect rect) {
  context->setFillColor(CColor(0x10, 0x0f, 0x0e));
  context->setFrameColor(CColor(0xd6, 0xa8, 0x57, 0x60));
  if (auto path =
          VSTGUI::owned(context->createRoundRectGraphicsPath(rect, 3.0))) {
    context->drawGraphicsPath(path, CDrawContext::kPathFilled);
    context->drawGraphicsPath(path, CDrawContext::kPathStroked);
  } else {
    context->drawRect(rect, kDrawFilledAndStroked);
  }
  rect.inset(5.0, 5.0);
  context->setFrameColor(CColor(0xd6, 0xa8, 0x57, 0x64));
  context->setLineWidth(1);
  context->drawRect(rect, kDrawStroked);
  context->setFrameColor(CColor(0xd6, 0xa8, 0x57, 0x3e));
  context->drawLine(CPoint(rect.left + rect.getWidth() / 2.0, rect.top),
                    CPoint(rect.left + rect.getWidth() / 2.0, rect.bottom));
  context->drawLine(CPoint(rect.left, rect.top + rect.getHeight() / 2.0),
                    CPoint(rect.right, rect.top + rect.getHeight() / 2.0));
}

class MorphSelectorIconView final : public CView {
 public:
  explicit MorphSelectorIconView(const CRect& rect) : CView(rect) {}

  void draw(CDrawContext* const context) override {
    auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    DrawMorphGridIcon(context, rect);
    context->restoreGlobalState();
    setDirty(false);
  }
};

class DismissOverlayView final : public CView {
 public:
  DismissOverlayView(const CRect& rect, std::function<void()> action)
      : CView(rect), action_(std::move(action)) {}

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isLeftButton() && action_) {
      action_();
      return VSTGUI::kMouseEventHandled;
    }
    return CView::onMouseDown(where, buttons);
  }

  void draw(CDrawContext* const /*context*/) override { setDirty(false); }

 private:
  std::function<void()> action_;
};

class GlowingActionLabel final : public ActionLabel {
 public:
  GlowingActionLabel(const CRect& size, const UTF8String& text,
                     std::function<void()> action)
      : ActionLabel(size, text, std::move(action)) {}

  void SetActive(const bool active) {
    if (active_ == active) {
      return;
    }
    active_ = active;
    invalid();
  }

  void draw(CDrawContext* const context) override {
    if (!active_) {
      ActionLabel::draw(context);
      return;
    }

    const auto rect = getViewSize();
    const auto text = getText().getPlatformString();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    context->setFont(getFont());
    for (const auto& pass : std::array{
             std::pair{CColor(0xef, 0xca, 0x82, 0x26), 2.0},
             std::pair{CColor(0xef, 0xca, 0x82, 0x1a), 5.0},
             std::pair{CColor(0xef, 0xca, 0x82, 0x10), 8.0},
         }) {
      context->setFontColor(pass.first);
      context->drawString(text,
                          CRect(rect.left - pass.second, rect.top,
                                rect.right + pass.second, rect.bottom),
                          CHoriTxtAlign::kCenterText, true);
      context->drawString(text,
                          CRect(rect.left, rect.top - pass.second, rect.right,
                                rect.bottom + pass.second),
                          CHoriTxtAlign::kCenterText, true);
    }
    context->setFontColor(getFontColor());
    context->drawString(text, rect, CHoriTxtAlign::kCenterText, true);
    context->restoreGlobalState();
    setDirty(false);
  }

 private:
  bool active_ = false;
};

class VoiceMenuItemView final : public CView {
 public:
  VoiceMenuItemView(const CRect& rect, std::string label,
                    SharedPointer<CBitmap> thumbnail, bool morph_item,
                    bool selected, CFontRef font, std::function<void()> action)
      : CView(rect),
        label_(std::move(label)),
        thumbnail_(std::move(thumbnail)),
        morph_item_(morph_item),
        selected_(selected),
        font_(font),
        action_(std::move(action)) {
    if (font_) {
      font_->remember();
    }
  }

  ~VoiceMenuItemView() override {
    if (font_) {
      font_->forget();
    }
  }

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isLeftButton() && action_) {
      action_();
      return VSTGUI::kMouseEventHandled;
    }
    return CView::onMouseDown(where, buttons);
  }

  auto onMouseEntered(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    hovered_ = true;
    invalid();
    return CView::onMouseEntered(where, buttons);
  }

  auto onMouseExited(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    hovered_ = false;
    invalid();
    return CView::onMouseExited(where, buttons);
  }

  void draw(CDrawContext* const context) override {
    auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    if (selected_) {
      context->setFillColor(hovered_ ? CColor(0xd7, 0xac, 0x62, 0x22)
                                     : CColor(0xd7, 0xac, 0x62, 0x17));
      context->setFrameColor(hovered_ ? CColor(0xd8, 0xb3, 0x6d, 0x80)
                                      : CColor(0xd8, 0xb3, 0x6d, 0x6b));
    } else if (hovered_) {
      context->setFillColor(CColor(0xd7, 0xac, 0x62, 0x0f));
      context->setFrameColor(kTransparentCColor);
    } else {
      context->setFillColor(kTransparentCColor);
      context->setFrameColor(kTransparentCColor);
    }
    auto item_rect = rect;
    item_rect.inset(0.5, 0.5);
    if (auto path = VSTGUI::owned(
            context->createRoundRectGraphicsPath(item_rect, 2.0))) {
      context->drawGraphicsPath(path, CDrawContext::kPathFilled);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }

    constexpr auto kThumbSize = 42.0;
    const auto thumb_top =
        rect.top + std::floor((rect.getHeight() - kThumbSize) / 2.0);
    auto thumb = CRect(rect.left + 6, thumb_top, rect.left + 6 + kThumbSize,
                       thumb_top + kThumbSize);
    if (thumbnail_) {
      auto clip = thumb;
      clip.extend(1.0, 1.0);
      thumbnail_->draw(context, clip,
                       CPoint(clip.left - thumb.left, clip.top - thumb.top));
      auto thumb_frame = thumb;
      thumb_frame.inset(0.5, 0.5);
      context->setFrameColor(CColor(0x1b, 0x17, 0x12, 0x58));
      if (auto path = VSTGUI::owned(
              context->createRoundRectGraphicsPath(thumb_frame, 2.0))) {
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
    } else {
      context->setFillColor(CColor(0x27, 0x23, 0x1f));
      context->setFrameColor(CColor(0xd6, 0xa8, 0x57, 0x38));
      if (auto path =
              VSTGUI::owned(context->createRoundRectGraphicsPath(thumb, 2.0))) {
        context->drawGraphicsPath(path, CDrawContext::kPathFilled);
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
      if (morph_item_) {
        DrawMorphGridIcon(context, thumb);
      }
    }

    context->setFont(font_);
    context->setFontColor(CColor(0xca, 0xc7, 0xc1));
    constexpr auto kTextHeight = 18.0;
    const auto text_top =
        rect.top + std::floor((rect.getHeight() - kTextHeight) / 2.0);
    context->drawString(
        UTF8String(label_).getPlatformString(),
        CRect(rect.left + 58, text_top, rect.right - 8, text_top + kTextHeight),
        CHoriTxtAlign::kLeftText, true);
    context->restoreGlobalState();
    setDirty(false);
  }

 private:
  std::string label_;
  SharedPointer<CBitmap> thumbnail_;
  bool morph_item_;
  bool selected_;
  bool hovered_ = false;
  CFontRef font_;
  std::function<void()> action_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_VIEWS_H_
