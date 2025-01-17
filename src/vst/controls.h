// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_CONTROLS_H_
#define BEATRICE_VST_CONTROLS_H_

#include <filesystem>
#include <string>
#include <utility>

#include "vst3sdk/vstgui4/vstgui/lib/cbitmap.h"
#include "vst3sdk/vstgui4/vstgui/lib/ccolor.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawcontext.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfileselector.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/cgraphicspath.h"
#include "vst3sdk/vstgui4/vstgui/lib/cgraphicstransform.h"
#include "vst3sdk/vstgui4/vstgui/lib/clinestyle.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cparamdisplay.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cscrollbar.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/cslider.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ctextlabel.h"
#include "vst3sdk/vstgui4/vstgui/lib/cpoint.h"
#include "vst3sdk/vstgui4/vstgui/lib/cscrollview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cstring.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "vst/parameter.h"

namespace beatrice::vst {

using VSTGUI::CBitmap;
using VSTGUI::CButtonState;
using VSTGUI::CColor;
using VSTGUI::CControl;
using VSTGUI::CDrawContext;
using VSTGUI::CFileExtension;
using VSTGUI::CFontRef;
using VSTGUI::CGraphicsPath;
using VSTGUI::CGraphicsTransform;
using VSTGUI::CHoriTxtAlign;
using VSTGUI::CHorizontalSlider;
using VSTGUI::CMessageResult;
using VSTGUI::CMouseEventResult;
using VSTGUI::CMultiLineTextLabel;
using VSTGUI::CNewFileSelector;
using VSTGUI::CParamDisplay;
using VSTGUI::CPoint;
using VSTGUI::CRect;
using VSTGUI::CTextLabel;
using VSTGUI::IControlListener;
using VSTGUI::kAntiAliasing;
using VSTGUI::kCenterText;
using VSTGUI::kLineSolid;
using VSTGUI::kMessageNotified;
using VSTGUI::kTransparentCColor;
using VSTGUI::SharedPointer;
using VSTGUI::UTF8String;

struct ColorScheme {
  CColor surface_0;   // ヘッダー、フッターの背景
  CColor surface_1;   // 操作部分の背景 (左)
  CColor surface_2;   // 操作部分の背景 (中央)
  CColor surface_3;   // 操作部分の背景 (右)
  CColor on_surface;  // 文字
  CColor primary;     // 重要なボタン
  CColor on_primary;  // 文字
  CColor secondary;
  CColor secondary_dim;  // つまみ
  CColor outline;        // 境界線
  CColor background;     // 背景
};

static constexpr auto kDarkColorScheme = ColorScheme{
    .surface_0 = CColor(0x1a, 0x13, 0x14),
    .surface_1 = CColor(0x26, 0x1d, 0x1e),
    .surface_2 = CColor(0x2b, 0x22, 0x23),
    .surface_3 = CColor(0x32, 0x29, 0x2a),
    .on_surface = CColor(0xff, 0xff, 0xff),
    .primary = CColor(0xfb, 0xe1, 0x86),
    .on_primary = CColor(0x1e, 0x1b, 0x13),
    .secondary = {},
    .secondary_dim = CColor(0x7d, 0x38, 0x3c),
    .outline = CColor(0x46, 0x36, 0x2e),  // CColor(0x7d, 0x77, 0x87),
                                          // なぜか正しい色にならないので仮置き
    .background = CColor(0x46, 0x36, 0x2e),
};

class MonotoneBitmap : public CBitmap {
 public:
  MonotoneBitmap(const int width, const int height, const CColor& back_color,
                 const CColor& frame_color)
      : CBitmap(width, height),
        back_color_(back_color),
        frame_color_(frame_color) {}

  void draw(  // NOLINT(google-default-arguments)
      CDrawContext* const context, const CRect& rect,
      const CPoint& offset = CPoint(0, 0),
      const float /*alpha*/ = 1.0F) override {
    context->setFillColor(back_color_);
    SharedPointer<CGraphicsPath> path =
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
      path->addRect(frame_rect);
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
};

class Slider : public CHorizontalSlider {
 public:
  Slider(const CRect& size, IControlListener* const listener, const int32_t tag,
         const int min_pos, const int max_pos, CBitmap* const handle,
         CBitmap* const background, std::string units, CFontRef font_ref,
         const int precision = 1)
      : CHorizontalSlider(size, listener, tag, min_pos, max_pos, handle,
                          background, CPoint(0, 0), CSliderBase::kLeft),
        units_(std::move(units)),
        font_ref_(font_ref),
        precision_(precision) {
    font_ref->remember();
  }

  ~Slider() override { font_ref_->forget(); }

  void draw(CDrawContext* const context) override {
    CHorizontalSlider::draw(context);

    context->saveGlobalState();
    auto text_rect = getViewSize();
    // text_rect.inset(textInset.x, textInset.y);

    const auto& param =
        common::kSchema.GetParameter(static_cast<common::ParameterID>(tag));
    const auto denormalized_value = Denormalize(
        std::get<common::NumberParameter>(param), getValueNormalized());

    // 値を文字で表示
    auto value_string = std::string();
    {
      value_string.resize(8);
      const auto [ptr, ec] =
          std::to_chars(std::to_address(value_string.begin()),
                        std::to_address(value_string.end()), denormalized_value,
                        std::chars_format::fixed, precision_);
      value_string.resize(ptr - std::to_address(value_string.begin()));
      value_string += " ";
      value_string += static_cast<const char*>(units_.c_str());
    }

    const auto antialias = true;

    CPoint center(getViewSize().getCenter());
    CGraphicsTransform transform;
    transform.rotate(0.0, center);
    CDrawContext::Transform ctx_transform(*context, transform);

    context->setDrawMode(kAntiAliasing);
    context->setFont(font_ref_);
    context->setFontColor(kDarkColorScheme.on_surface);
    // getPlatformString の結果を直接渡さないと
    // use-after-free になるので注意
    context->drawString(UTF8String(value_string).getPlatformString(), text_rect,
                        kCenterText, antialias);
    context->restoreGlobalState();
    setDirty(false);
  }

 private:
  std::string units_;
  CFontRef font_ref_;
  int precision_;
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
        selector->run(this);  // notify に送られる
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

  auto GetPath() const -> const std::filesystem::path& { return file_; }

 private:
  std::filesystem::path file_;
};

// model description が空だったらラベルも非表示にする。
// voice description が空だったらラベルも非表示にする。
// voice description の位置は model description の大きさに依存する。
class ModelVoiceDescription : public VSTGUI::CScrollView {
 public:
  ModelVoiceDescription(const CRect& area, CFontRef font,
                        const int element_height, const int element_mergin_y)
      : VSTGUI::CScrollView(area,
                            CRect(0, 0, area.getWidth(), area.getHeight()),
                            VSTGUI::CScrollView::kVerticalScrollbar |
                                VSTGUI::CScrollView::kDontDrawFrame |
                                VSTGUI::CScrollView::kOverlayScrollbars),
        element_height_(element_height),
        element_mergin_y_(element_mergin_y) {
    setBackgroundColor(kTransparentCColor);
    auto scroll_bar = getVerticalScrollbar();
    scroll_bar->setFrameColor(kDarkColorScheme.outline);
    scroll_bar->setScrollerColor(kDarkColorScheme.secondary_dim);
    scroll_bar->setBackgroundColor(kTransparentCColor);

    auto y = 0;
    model_description_label_ =
        new CTextLabel(CRect(0, y, area.getWidth(), y + element_height),
                       "Model Description", nullptr, CParamDisplay::kNoFrame);
    model_description_label_->setFont(font);
    model_description_label_->setFontColor(kDarkColorScheme.on_surface);
    model_description_label_->setHoriAlign(CHoriTxtAlign::kLeftText);
    model_description_label_->setBackColor(kTransparentCColor);
    addView(model_description_label_);
    y += element_height + element_mergin_y;

    model_description_ = new CMultiLineTextLabel(
        CRect(0, y, area.getWidth(), area.getHeight() - y));
    model_description_->setFont(font);
    model_description_->setFontColor(kDarkColorScheme.on_surface);
    model_description_->setHoriAlign(CHoriTxtAlign::kLeftText);
    model_description_->setBackColor(kTransparentCColor);
    model_description_->setAutoHeight(true);
    model_description_->setStyle(CParamDisplay::kNoFrame);
    model_description_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    model_description_->setTextInset(CPoint(0, 2));
    addView(model_description_);
    y += model_description_->getHeight() + element_mergin_y;

    voice_description_label_ =
        new CTextLabel(CRect(0, y, area.getWidth(), y + element_height),
                       "Voice Description", nullptr, CParamDisplay::kNoFrame);
    voice_description_label_->setFont(font);
    voice_description_label_->setFontColor(kDarkColorScheme.on_surface);
    voice_description_label_->setHoriAlign(CHoriTxtAlign::kLeftText);
    voice_description_label_->setBackColor(kTransparentCColor);
    addView(voice_description_label_);
    y += element_height + element_mergin_y;

    voice_description_ = new CMultiLineTextLabel(
        CRect(0, y, area.getWidth(), area.getHeight() - y));
    voice_description_->setFont(font);
    voice_description_->setFontColor(kDarkColorScheme.on_surface);
    voice_description_->setHoriAlign(CHoriTxtAlign::kLeftText);
    voice_description_->setBackColor(kTransparentCColor);
    voice_description_->setAutoHeight(true);
    voice_description_->setStyle(CParamDisplay::kNoFrame);
    voice_description_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    voice_description_->setTextInset(CPoint(0, 2));
    addView(voice_description_);
    AdjustVoiceDescriptionPosition();
  }

  void SetModelDescription(const std::u8string& description) {
    if (description.empty()) {
      model_description_->setText(nullptr);
      model_description_->setVisible(false);
      model_description_label_->setVisible(false);
    } else {
      model_description_->setText(
          reinterpret_cast<const char*>(description.c_str()));
      model_description_->setVisible(true);
      model_description_label_->setVisible(true);
    }
    AdjustVoiceDescriptionPosition();
  }

  void SetVoiceDescription(const std::u8string& description) {
    if (description.empty()) {
      voice_description_->setText(nullptr);
      voice_description_->setVisible(false);
      voice_description_label_->setVisible(false);
    } else {
      voice_description_->setText(
          reinterpret_cast<const char*>(description.c_str()));
      voice_description_->setVisible(true);
      voice_description_label_->setVisible(true);
    }
    AdjustVoiceDescriptionPosition();
    Invalid();
  }

  void Invalid() const {
    if (auto* const parent_view = getParentView()) {
      parent_view->invalid();
    }
  }

 private:
  int element_height_;
  int element_mergin_y_;
  CTextLabel* model_description_label_ = nullptr;
  CTextLabel* voice_description_label_ = nullptr;
  CMultiLineTextLabel* model_description_ = nullptr;
  CMultiLineTextLabel* voice_description_ = nullptr;
  friend class Editor;

  void AdjustVoiceDescriptionPosition() {
    auto y =
        model_description_->getText() == nullptr
            ? 0
            : model_description_->getViewSize().bottom + element_mergin_y_ + 4;
    auto area = getViewSize();

    voice_description_label_->setViewSize(
        CRect(0, y, area.getWidth(), y + element_height_));
    y += element_height_ + element_mergin_y_;
    voice_description_->setViewSize(
        CRect(0, y, area.getWidth(), y + voice_description_->getHeight()));
    y += voice_description_->getHeight() + element_mergin_y_;
    auto container_size = getContainerSize();
    container_size.setHeight(y);
    setContainerSize(container_size);

    setDirty();
    Invalid();
  }
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_CONTROLS_H_
