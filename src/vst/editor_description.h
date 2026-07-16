// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_EDITOR_DESCRIPTION_H_
#define BEATRICE_VST_EDITOR_DESCRIPTION_H_

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "vst3sdk/vstgui4/vstgui/lib/cbuttonstate.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cparamdisplay.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cscrollbar.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ctextlabel.h"
#include "vst3sdk/vstgui4/vstgui/lib/cscrollview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/events.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "vst/surface_texture.h"

namespace beatrice::vst {

using VSTGUI::CButtonState;
using VSTGUI::CFontRef;
using VSTGUI::CHoriTxtAlign;
using VSTGUI::CMouseEventResult;
using VSTGUI::CMultiLineTextLabel;
using VSTGUI::CParamDisplay;
using VSTGUI::CScrollbar;
using VSTGUI::CScrollView;
using VSTGUI::CTextLabel;
using VSTGUI::CView;
using VSTGUI::kDrawFilled;

inline void SetScrollableDescription(CScrollView* const scroll,
                                     CMultiLineTextLabel* const label,
                                     const std::u8string& text) {
  if (!scroll || !label) {
    return;
  }
  const auto viewport = scroll->getViewSize();
  constexpr auto kTextRightPadding = 6.0;
  scroll->resetScrollOffset();
  label->setText(reinterpret_cast<const char*>(text.c_str()));

  auto content_width = viewport.getWidth();
  const auto measure_height = [&]() -> CCoord {
    const auto text_width = std::max(20.0, content_width - kTextRightPadding);
    label->setViewSize(CRect(0, 0, text_width, viewport.getHeight()));
    label->setAutoHeight(true);
    const auto height = label->getViewSize().getHeight();
    label->setAutoHeight(false);
    return height;
  };
  auto content_height = measure_height();
  if (content_height > viewport.getHeight()) {
    content_width -= scroll->getScrollbarWidth();
    content_height = measure_height();
  }
  content_height = std::max(viewport.getHeight(), content_height);
  label->setViewSize(CRect(
      0, 0, std::max(20.0, content_width - kTextRightPadding), content_height));
  label->setMouseableArea(label->getViewSize());
  scroll->setContainerSize(CRect(0, 0, content_width, content_height));
  scroll->setDirty();
  label->setDirty();
}

inline void ApplyScrollbarTheme(CScrollView* const scroll) {
  if (!scroll) {
    return;
  }
  const auto apply = [](CScrollbar* const bar) -> void {
    if (!bar) {
      return;
    }
    bar->setBackgroundColor(CColor(0x08, 0x08, 0x07, 0xb8));
    bar->setFrameColor(CColor(0xe2, 0xba, 0x79, 0x20));
    bar->setScrollerColor(CColor(0xc3, 0xa0, 0x66, 0xd0));
    bar->setMinScrollerLength(18.0);
    bar->setDirty();
  };
  apply(scroll->getVerticalScrollbar());
  apply(scroll->getHorizontalScrollbar());
}

class DimOverlayView final : public CView {
 public:
  explicit DimOverlayView(const CRect& rect) : CView(rect) {
    setMouseEnabled(false);
  }

  void draw(CDrawContext* const context) override {
    const auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    context->setFillColor(CColor(0x00, 0x00, 0x00, 0x2a));
    context->drawRect(rect, kDrawFilled);
    context->restoreGlobalState();
    setDirty(false);
  }
};

class DescriptionPopupPanel final : public SurfacePanel {
 public:
  DescriptionPopupPanel(const CRect& rect,
                        const SharedPointer<SurfaceBitmap>& texture,
                        const CColor& border, CCoord radius,
                        std::function<void()> on_exit)
      : SurfacePanel(rect, texture, border, radius),
        on_exit_(std::move(on_exit)) {}

  auto onMouseExited(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (on_exit_) {
      on_exit_();
      return VSTGUI::kMouseEventHandled;
    }
    return SurfacePanel::onMouseExited(where, buttons);
  }

 private:
  std::function<void()> on_exit_;
};

class DescriptionPane final : public SurfacePanel {
 public:
  using ExpandAction = std::function<void(
      const char* title, const std::u8string& text, CRect popup_rect)>;

  DescriptionPane(const CRect& rect,
                  const SharedPointer<SurfaceBitmap>& texture,
                  const CColor& border, CCoord radius, std::string title,
                  const CRect& title_rect, const CRect& scroll_rect,
                  CFontRef title_font, CFontRef body_font,
                  const CColor& title_color, const CColor& body_color,
                  CRect popup_rect, ExpandAction expand_action)
      : SurfacePanel(rect, texture, border, radius),
        title_(std::move(title)),
        popup_rect_(popup_rect),
        expand_action_(std::move(expand_action)) {
    title_label_ = new CTextLabel(title_rect, title_.c_str(), nullptr,
                                  CParamDisplay::kNoFrame);
    title_label_->setBackColor(kTransparentCColor);
    title_label_->setFont(title_font);
    title_label_->setFontColor(title_color);
    title_label_->setHoriAlign(CHoriTxtAlign::kLeftText);
    addView(title_label_);

    scroll_ = new CScrollView(
        scroll_rect,
        CRect(0, 0, scroll_rect.getWidth(), scroll_rect.getHeight()),
        CScrollView::kVerticalScrollbar | CScrollView::kDontDrawFrame |
            CScrollView::kAutoHideScrollbars,
        6);
    ApplyScrollbarTheme(scroll_);
    scroll_->setBackgroundColor(kTransparentCColor);
    scroll_->setTransparency(true);
    addView(scroll_);

    const auto label_width = std::max(20.0, scroll_rect.getWidth() - 10.0);
    label_ = new CMultiLineTextLabel(
        CRect(0, 0, label_width, scroll_rect.getHeight()));
    label_->setFont(body_font);
    label_->setFontColor(body_color);
    label_->setBackColor(kTransparentCColor);
    label_->setStyle(CParamDisplay::kNoFrame);
    label_->setHoriAlign(CHoriTxtAlign::kLeftText);
    label_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    label_->setMouseEnabled(false);
    scroll_->addView(label_);
  }

  void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override {
    SurfacePanel::onMouseDownEvent(event);
    if (!event.consumed && body_visible_ && event.buttonState.isLeft()) {
      if (expand_action_) {
        expand_action_(title_.c_str(), text_, popup_rect_);
      }
      event.consumed = true;
    }
  }

  void SetTitle(std::string title) {
    title_ = std::move(title);
    if (title_label_) {
      title_label_->setText(title_.c_str());
      title_label_->setDirty();
    }
  }

  void SetText(const std::u8string& text) {
    if (text_ == text) {
      return;
    }
    text_ = text;
    SetScrollableDescription(scroll_, label_, text_);
  }

  void SetBodyVisible(const bool visible) {
    body_visible_ = visible;
    if (label_) {
      label_->setVisible(visible);
      label_->setDirty();
    }
    if (scroll_) {
      scroll_->setVisible(visible);
      scroll_->setDirty();
    }
  }

  [[nodiscard]] auto Text() const -> const std::u8string& { return text_; }

 private:
  std::string title_;
  std::u8string text_;
  CRect popup_rect_;
  ExpandAction expand_action_;
  CTextLabel* title_label_ = nullptr;
  CScrollView* scroll_ = nullptr;
  CMultiLineTextLabel* label_ = nullptr;
  bool body_visible_ = true;
};

class DescriptionPopupView final : public CViewContainer {
 public:
  DescriptionPopupView(const CRect& rect,
                       const SharedPointer<SurfaceBitmap>& panel_texture,
                       const CColor& border, CCoord radius, CFontRef title_font,
                       CFontRef body_font)
      : CViewContainer(rect) {
    setBackgroundColor(kTransparentCColor);
    setTransparency(true);

    dim_view_ = new DimOverlayView(rect);
    addView(dim_view_);

    panel_ = new DescriptionPopupPanel(CRect(0, 0, 1, 1), panel_texture, border,
                                       radius, [this]() -> void { Hide(); });
    addView(panel_);

    title_ = new CTextLabel(CRect(32, 28, 360, 52), "", nullptr,
                            CParamDisplay::kNoFrame);
    title_->setBackColor(kTransparentCColor);
    title_->setFont(title_font);
    title_->setFontColor(CColor(0xca, 0xc7, 0xc1));
    title_->setHoriAlign(CHoriTxtAlign::kLeftText);
    panel_->addView(title_);

    scroll_ = new CScrollView(CRect(32, 72, 612, 260), CRect(0, 0, 580, 188),
                              CScrollView::kVerticalScrollbar |
                                  CScrollView::kDontDrawFrame |
                                  CScrollView::kAutoHideScrollbars,
                              7);
    ApplyScrollbarTheme(scroll_);
    scroll_->setBackgroundColor(kTransparentCColor);
    scroll_->setTransparency(true);
    panel_->addView(scroll_);

    text_ = new CMultiLineTextLabel(CRect(0, 0, 570, 188));
    text_->setFont(body_font);
    text_->setFontColor(CColor(0xca, 0xc7, 0xc1));
    text_->setBackColor(kTransparentCColor);
    text_->setStyle(CParamDisplay::kNoFrame);
    text_->setHoriAlign(CHoriTxtAlign::kLeftText);
    text_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    scroll_->addView(text_);

    setVisible(false);
  }

  void Show(const char* const title, const std::u8string& text, CRect size) {
    if (!panel_ || !title_ || !text_ || !scroll_) {
      return;
    }
    panel_->setViewSize(size);
    panel_->setMouseableArea(size);
    title_->setText(title);
    const auto scroll_rect =
        CRect(32, 72, size.getWidth() - 32, size.getHeight() - 28);
    scroll_->setViewSize(scroll_rect);
    scroll_->setMouseableArea(scroll_rect);
    SetScrollableDescription(scroll_, text_, text);
    setVisible(true);
    invalid();
  }

  void Hide() {
    setVisible(false);
    invalid();
  }

 private:
  DimOverlayView* dim_view_ = nullptr;
  DescriptionPopupPanel* panel_ = nullptr;
  CTextLabel* title_ = nullptr;
  CScrollView* scroll_ = nullptr;
  CMultiLineTextLabel* text_ = nullptr;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_EDITOR_DESCRIPTION_H_
