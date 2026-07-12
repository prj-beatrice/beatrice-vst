// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_CONTROLS_H_
#define BEATRICE_VST_CONTROLS_H_

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "vst3sdk/vstgui4/vstgui/lib/cbitmap.h"
#include "vst3sdk/vstgui4/vstgui/lib/ccolor.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawcontext.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfileselector.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/cgraphicspath.h"
#include "vst3sdk/vstgui4/vstgui/lib/clinestyle.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cparamdisplay.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cslider.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ctextlabel.h"
#include "vst3sdk/vstgui4/vstgui/lib/cpoint.h"
#include "vst3sdk/vstgui4/vstgui/lib/cstring.h"
#include "vst3sdk/vstgui4/vstgui/lib/events.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

namespace beatrice::vst {

using VSTGUI::CBitmap;
using VSTGUI::CButtonState;
using VSTGUI::CColor;
using VSTGUI::CControl;
using VSTGUI::CCoord;
using VSTGUI::CDrawContext;
using VSTGUI::CFileExtension;
using VSTGUI::CFontRef;
using VSTGUI::CGraphicsPath;
using VSTGUI::CHoriTxtAlign;
using VSTGUI::CHorizontalSlider;
using VSTGUI::CMessageResult;
using VSTGUI::CMouseEventResult;
using VSTGUI::CNewFileSelector;
using VSTGUI::CParamDisplay;
using VSTGUI::CPoint;
using VSTGUI::CRect;
using VSTGUI::CTextLabel;

inline constexpr int kSliderKnobWidth = 6;
using VSTGUI::CView;
using VSTGUI::IControlListener;
using VSTGUI::kAntiAliasing;
using VSTGUI::kDrawFilledAndStroked;
using VSTGUI::kLineSolid;
using VSTGUI::kMessageNotified;
using VSTGUI::kTransparentCColor;
using VSTGUI::SharedPointer;
using VSTGUI::UTF8String;

class ChevronView final : public CView {
 public:
  ChevronView(const CRect& size, const CColor& color)
      : CView(size), color_(color) {
    setMouseEnabled(false);
  }

  void draw(CDrawContext* const context) override {
    auto rect = getViewSize();
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    context->setLineStyle(kLineSolid);
    context->setLineWidth(1.5);
    context->setFrameColor(color_);
    const auto center_y = rect.getCenter().y + 1.0;
    const auto center_x = rect.getCenter().x;
    context->drawLine(CPoint(center_x - 4.0, center_y - 2.0),
                      CPoint(center_x, center_y + 2.0));
    context->drawLine(CPoint(center_x, center_y + 2.0),
                      CPoint(center_x + 4.0, center_y - 2.0));
    context->restoreGlobalState();
    setDirty(false);
  }

 private:
  CColor color_;
};

class ActionLabel : public CTextLabel {
 public:
  ActionLabel(const CRect& size, const UTF8String& text,
              std::function<void()> action)
      : CTextLabel(size, text, nullptr, CParamDisplay::kNoFrame),
        action_(std::move(action)) {}

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isLeftButton() && action_) {
      action_();
      return VSTGUI::kMouseEventHandled;
    }
    return CTextLabel::onMouseDown(where, buttons);
  }

 private:
  std::function<void()> action_;
};

class MonotoneBitmap : public CBitmap {
 public:
  MonotoneBitmap(const int width, const int height, const CColor& back_color,
                 const CColor& frame_color, CCoord radius = 0.0)
      : CBitmap(width, height),
        back_color_(back_color),
        frame_color_(frame_color),
        radius_(radius) {}

  void draw(  // NOLINT(google-default-arguments)
      CDrawContext* const context, const CRect& rect,
      const CPoint& offset = CPoint(0, 0),
      const float /*alpha*/ = 1.0f) override {
    context->setFillColor(back_color_);
    const SharedPointer<CGraphicsPath> path =
        VSTGUI::owned(context->createGraphicsPath());
    auto frame_rect = rect;
    frame_rect.offset(offset);
    auto stroke_path = true;
    if (path) {
      if (stroke_path) {
        if (frame_color_ == kTransparentCColor) {
          frame_rect.inset(1.0, 1.0);
          stroke_path = false;
        } else if (frame_color_ == back_color_) {
          stroke_path = false;
        } else {
          frame_rect.inset(0.5, 0.5);
        }
      }
      if (radius_ > 0.0) {
        path->addRoundRect(frame_rect, radius_);
      } else {
        path->addRect(frame_rect);
      }
      context->drawGraphicsPath(path, CDrawContext::kPathFilled);
      if (stroke_path) {
        context->setLineStyle(kLineSolid);
        context->setLineWidth(1);
        context->setFrameColor(frame_color_);
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
      }
    }
  }

 private:
  CColor back_color_;
  CColor frame_color_;
  CCoord radius_;
};

class Slider : public CHorizontalSlider {
 public:
  Slider(const CRect& size, IControlListener* const listener, const int32_t tag,
         const int min_pos, const int max_pos, CBitmap* const handle,
         CBitmap* const background, std::string units, CFontRef font_ref,
         CFontRef value_font_ref, std::string title, const int precision = 1)
      : CHorizontalSlider(size, listener, tag, min_pos, max_pos, handle,
                          background, CPoint(0, 0), CSliderBase::kLeft),
        units_(std::move(units)),
        font_ref_(font_ref),
        value_font_ref_(value_font_ref),
        title_(std::move(title)),
        precision_(precision) {
    font_ref_->remember();
    value_font_ref_->remember();
  }

  ~Slider() override {
    font_ref_->forget();
    value_font_ref_->forget();
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void setFineWheelInc(const float fine_wheel_inc) {
    fine_wheel_inc_ = fine_wheel_inc;
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  [[nodiscard]] auto getFineWheelInc() const -> float {
    return fine_wheel_inc_;
  }

  void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
    auto distance = isStyleHorizontal() ? event.deltaX : event.deltaY;
    if (distance == 0.0) {
      return;
    }

    onMouseWheelEditing(this);
    if (isStyleHorizontal()) {
      distance *= -1.0;
    }
    if (isInverseStyle()) {
      distance *= -1.0;
    }

    auto plain_value = getValue();
    const auto increment =
        buttonStateFromEventModifiers(event.modifiers) & kZoomModifier
            ? getFineWheelInc()
            : getWheelInc();
    plain_value += static_cast<float>(distance) * increment;
    setValue(plain_value);
    if (isDirty()) {
      invalid();
      valueChanged();
    }
    event.consumed = true;
  }

  // CSliderBase::onKeyboardEvent がベース
  void onKeyboardEvent(VSTGUI::KeyboardEvent& event) override {
    using VSTGUI::VirtualKey;
    if (event.type != VSTGUI::EventType::KeyDown) {
      return;
    }
    switch (event.virt) {
      case VirtualKey::Up:
        [[fallthrough]];
      case VirtualKey::Right:
        [[fallthrough]];
      case VirtualKey::Down:
        [[fallthrough]];
      case VirtualKey::Left: {
        auto distance = 1.0f;
        const auto is_inverse = isInverseStyle();
        if ((event.virt == VirtualKey::Down && !is_inverse) ||
            (event.virt == VirtualKey::Up && is_inverse) ||
            (event.virt == VirtualKey::Left && !is_inverse) ||
            (event.virt == VirtualKey::Right && is_inverse)) {
          distance = -distance;
        }

        auto plain_value = getValue();
        if (buttonStateFromEventModifiers(event.modifiers) & kZoomModifier) {
          plain_value += distance * getFineWheelInc();
        } else {
          plain_value += distance * getWheelInc();
        }
        setValue(plain_value);

        if (isDirty()) {
          invalid();
          beginEdit();
          valueChanged();
          endEdit();
        }
        event.consumed = true;
      }
      case VirtualKey::Escape: {
        if (isEditing()) {
          onMouseCancel();
          event.consumed = true;
        }
        break;
      }
      default:
        break;
    }
  }

  void draw(CDrawContext* const context) override {
    // 値を文字で表示
    auto value_string = std::string();
    if (IsEnabled()) {
      value_string.resize(10);
      const auto result =
          std::to_chars(std::to_address(value_string.begin()),
                        std::to_address(value_string.end()), getValue(),
                        std::chars_format::fixed, precision_);
      value_string.resize(result.ptr - std::to_address(value_string.begin()));
      if (!units_.empty()) {
        value_string += " ";
        value_string += units_;
      }
    } else {
      value_string = "Disabled";
    }

    auto rect = getViewSize();

    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing);
    context->setFont(font_ref_);
    context->setFontColor(IsEnabled() ? CColor(0xb8, 0xb5, 0xaf)
                                      : CColor(0x76, 0x73, 0x6d));
    // getPlatformString の結果を直接渡さないと
    // use-after-free になるので注意
    context->drawString(
        UTF8String(title_).getPlatformString(),
        CRect(rect.left, rect.top, rect.left + rect.getWidth() * 0.60,
              rect.top + 18),
        CHoriTxtAlign::kLeftText, true);
    context->setFont(value_font_ref_);
    context->setFontColor(IsEnabled() ? CColor(0xca, 0xc7, 0xc1)
                                      : CColor(0x76, 0x73, 0x6d));
    context->drawString(UTF8String(value_string).getPlatformString(),
                        CRect(rect.left + rect.getWidth() * 0.56, rect.top,
                              rect.right, rect.top + 18),
                        CHoriTxtAlign::kRightText, true);

    const auto track_y = rect.top + GetTrackYOffset();
    const auto left = rect.left;
    const auto right = rect.right;
    context->setLineStyle(kLineSolid);
    context->setLineWidth(3);
    context->setFrameColor(CColor(0x05, 0x05, 0x05));
    context->drawLine(CPoint(left, track_y), CPoint(right, track_y));
    context->setFrameColor(IsEnabled() ? CColor(0xe2, 0xba, 0x79)
                                       : CColor(0x5b, 0x54, 0x49));
    const auto norm = (getMax() == getMin())
                          ? 0.0
                          : (getValue() - getMin()) / (getMax() - getMin());
    constexpr auto kKnobHalfWidth = kSliderKnobWidth / 2.0;
    constexpr auto kKnobFrameInset = 0.5;
    const auto knob_x = std::clamp(left + (right - left) * norm,
                                   left + kKnobHalfWidth + kKnobFrameInset,
                                   right - kKnobHalfWidth - kKnobFrameInset);
    context->drawLine(CPoint(left, track_y), CPoint(knob_x, track_y));
    context->setFillColor(IsEnabled() ? CColor(0xc3, 0xa0, 0x66)
                                      : CColor(0x5b, 0x54, 0x49));
    auto knob_rect = CRect(knob_x - kKnobHalfWidth, track_y - 8.0,
                           knob_x + kKnobHalfWidth, track_y + 7.0);
    context->setFillColor(CColor(0x00, 0x00, 0x00, IsEnabled() ? 0x5c : 0x2c));
    auto shadow_rect = knob_rect;
    shadow_rect.offset(0.0, 2.0);
    if (auto path = VSTGUI::owned(
            context->createRoundRectGraphicsPath(shadow_rect, 2.0))) {
      context->drawGraphicsPath(path, CDrawContext::kPathFilled);
    }
    context->setFillColor(IsEnabled() ? CColor(0xe2, 0xba, 0x79)
                                      : CColor(0x5b, 0x54, 0x49));
    context->setFrameColor(IsEnabled() ? CColor(0xf4, 0xd7, 0x9e, 0x9e)
                                       : CColor(0x5b, 0x54, 0x49));
    context->setLineWidth(1);
    if (auto path = VSTGUI::owned(
            context->createRoundRectGraphicsPath(knob_rect, 2.0))) {
      context->drawGraphicsPath(path, CDrawContext::kPathFilled);
      context->drawGraphicsPath(path, CDrawContext::kPathStroked);
    } else {
      context->drawRect(knob_rect, kDrawFilledAndStroked);
    }
    context->restoreGlobalState();
    setDirty(false);
  }

  void SetEnabled(const bool enabled) {
    if (enabled_ == enabled) {
      return;
    }
    enabled_ = enabled;
    if (enabled_) {
      setMouseEnabled(true);
      setWantsFocus(true);
      setAlphaValue(1.0);
    } else {
      setMouseEnabled(false);
      setWantsFocus(false);
      setAlphaValue(0.3);
    }
  }

  [[nodiscard]] auto IsEnabled() const -> bool { return enabled_; }

 protected:
  [[nodiscard]] virtual auto GetTrackYOffset() const -> CCoord { return 33.0; }

 private:
  std::string units_;
  CFontRef font_ref_;
  CFontRef value_font_ref_;
  std::string title_;
  int precision_;
  float fine_wheel_inc_ = 0.1f;
  bool enabled_ = true;
};

class FileSelector : public CTextLabel {
 public:
  explicit FileSelector(const CRect& size,
                        IControlListener* listener_ = nullptr, int32_t tag_ = 0,
                        CBitmap* background = nullptr)
      : CTextLabel(size, "", background) {
    setTag(tag_);
    setListener(listener_);
  }
  explicit FileSelector(const CRect& size, const UTF8String& text = "")
      : CTextLabel(size, text) {}

  auto onMouseDown(CPoint& where, const CButtonState& buttons)
      -> CMouseEventResult override {
    if (buttons.isLeftButton()) {
      auto* const selector =
          CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectFile);
      if (selector) {
        selector->addFileExtension(CFileExtension("TOML", "toml"));
        selector->run(
            [self = VSTGUI::shared(this)](CNewFileSelector* sender) -> void {
              self->notify(sender, CNewFileSelector::kSelectEndMessage);
            });
        selector->forget();
      }
      return VSTGUI::kMouseEventHandled;
    }
    return CTextLabel::onMouseDown(where, buttons);
  }

  auto notify(CBaseObject* sender, const char* message)
      -> CMessageResult override {
    if (std::strcmp(message, CNewFileSelector::kSelectEndMessage) == 0) {
      // ファイルパスを取得
      auto* const selector = static_cast<CNewFileSelector*>(sender);
      if (const auto* const file_char = selector->getSelectedFile(0)) {
        const auto file_u8string =
            std::u8string(file_char, file_char + std::strlen(file_char));
        const auto file = std::filesystem::path(file_u8string);
        if (std::filesystem::exists(file) &&
            std::filesystem::is_regular_file(file)) {
          SetPath(file);
          // Editor に通知
          valueChanged();
        }
      }
      return kMessageNotified;
    }
    return CTextLabel::notify(sender, message);
  }

  void SetPath(const std::filesystem::path& file) {
    if (file == file_) {
      return;
    }
    file_ = file;
  }

  [[nodiscard]] auto GetPath() const -> const std::filesystem::path& {
    return file_;
  }

 private:
  std::filesystem::path file_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_CONTROLS_H_
