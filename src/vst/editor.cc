// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "vst/editor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "beatricelib/beatrice.h"
#include "toml11/single_include/toml.hpp"
#include "vst3sdk/pluginterfaces/base/fplatform.h"
#include "vst3sdk/pluginterfaces/base/fstrdefs.h"
#include "vst3sdk/pluginterfaces/base/ftypes.h"
#include "vst3sdk/pluginterfaces/base/funknown.h"
#include "vst3sdk/pluginterfaces/gui/iplugview.h"
#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/public.sdk/source/vst/vstguieditor.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"
#include "vst3sdk/vstgui4/vstgui/lib/cbitmap.h"
#include "vst3sdk/vstgui4/vstgui/lib/cbitmapfilter.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/cframe.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/platformfactory.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/error.h"
#include "common/model_config.h"
#include "common/parameter_schema.h"
#include "common/voice_morph_parameter.h"
#include "common/voice_morph_state.h"
#include "vst/controller.h"
#include "vst/controls.h"
#include "vst/editor_description.h"
#include "vst/editor_morph.h"
#include "vst/editor_morph_controller.h"
#include "vst/editor_views.h"
#include "vst/editor_voice_selector.h"
#include "vst/parameter.h"
#include "vst/surface_texture.h"

#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>  // NOLINT(misc-include-cleaner)
#else
#include <cstdlib>
#endif

namespace beatrice::vst {

using common::ParameterID;
using Steinberg::ViewRect;
using Steinberg::Vst::String128;
using Steinberg::Vst::StringListParameter;
using VSTGUI::CFontDesc;
using VSTGUI::CFrame;
using VSTGUI::COptionMenu;
using VSTGUI::CView;
using VSTGUI::CViewContainer;
using VSTGUI::getPlatformFactory;
using VSTGUI::kBoldFace;

// NOLINTNEXTLINE(readability-identifier-naming)
namespace BitmapFilter = VSTGUI::BitmapFilter;

namespace {

auto HasEnvironmentVariable(const char* const name) -> bool {
#if defined(_WIN32)
  // NOLINTNEXTLINE(misc-include-cleaner)
  return GetEnvironmentVariableA(name, nullptr, 0) > 0;
#else
  return std::getenv(name) != nullptr;
#endif
}

auto GetScreenshotModelPath() -> std::u8string {
#if defined(_WIN32)
  const auto size =
      // NOLINTNEXTLINE(misc-include-cleaner)
      GetEnvironmentVariableW(L"BEATRICE_SCREENSHOT_MODEL_PATH", nullptr, 0);
  if (size == 0) {
    return {};
  }
  auto value = std::wstring(size, L'\0');
  const auto copied =
      // NOLINTNEXTLINE(misc-include-cleaner)
      GetEnvironmentVariableW(L"BEATRICE_SCREENSHOT_MODEL_PATH", value.data(),
                              size);
  if (copied == 0) {
    return {};
  }
  value.resize(copied);
  const auto value_utf8 =
      Steinberg::Vst::StringConvert::convert(Steinberg::wscast(value.c_str()));
#else
  const auto* const value = std::getenv("BEATRICE_SCREENSHOT_MODEL_PATH");
  if (!value || value[0] == '\0') {
    return {};
  }
  const auto value_utf8 = std::string(value);
#endif
  return {value_utf8.begin(), value_utf8.end()};
}

auto ScaleBitmap(CBitmap* const bitmap, const int width, const int height)
    -> SharedPointer<CBitmap> {
  const auto scale =
      VSTGUI::owned(BitmapFilter::Factory::getInstance().createFilter(
          BitmapFilter::Standard::kScaleBilinear));
  scale->setProperty(BitmapFilter::Standard::Property::kInputBitmap, bitmap);
  scale->setProperty(BitmapFilter::Standard::Property::kOutputRect,
                     CRect(0, 0, width, height));
  if (!scale->run()) {
    return nullptr;
  }
  auto* const scaled_bitmap_obj =
      scale->getProperty(BitmapFilter::Standard::Property::kOutputBitmap)
          .getObject();
  auto* const scaled_bitmap = dynamic_cast<CBitmap*>(scaled_bitmap_obj);
  if (!scaled_bitmap) {
    return nullptr;
  }
  return VSTGUI::shared(scaled_bitmap);
}

void ApplyRoundedMask(CBitmap* const bitmap, const double radius) {
  if (!bitmap || radius <= 0.0) {
    return;
  }
  auto access = VSTGUI::owned(CBitmapPixelAccess::create(bitmap, false));
  if (!access) {
    return;
  }
  const auto width = static_cast<double>(access->getBitmapWidth());
  const auto height = static_cast<double>(access->getBitmapHeight());
  const auto r = std::min(radius, std::min(width, height) / 2.0);
  for (uint32_t y = 0; y < access->getBitmapHeight(); ++y) {
    for (uint32_t x = 0; x < access->getBitmapWidth(); ++x) {
      const auto px = static_cast<double>(x) + 0.5;
      const auto py = static_cast<double>(y) + 0.5;
      auto coverage = 1.0;
      auto cx = px;
      auto cy = py;
      auto in_corner = false;
      if (px < r && py < r) {
        cx = r;
        cy = r;
        in_corner = true;
      } else if (px > width - r && py < r) {
        cx = width - r;
        cy = r;
        in_corner = true;
      } else if (px < r && py > height - r) {
        cx = r;
        cy = height - r;
        in_corner = true;
      } else if (px > width - r && py > height - r) {
        cx = width - r;
        cy = height - r;
        in_corner = true;
      }
      if (in_corner) {
        const auto distance = std::hypot(px - cx, py - cy);
        coverage = std::clamp(r + 0.5 - distance, 0.0, 1.0);
      }
      if (coverage < 1.0) {
        access->setPosition(x, y);
        CColor color;
        access->getColor(color);
        color.alpha = static_cast<uint8_t>(
            std::clamp(std::round(static_cast<double>(color.alpha) * coverage),
                       0.0, 255.0));
        access->setColor(color);
      }
    }
  }
}

auto MakeRoundedBitmap(CBitmap* const source, const int width, const int height,
                       const double radius) -> SharedPointer<CBitmap> {
  auto bitmap = ScaleBitmap(source, width, height);
  if (bitmap) {
    ApplyRoundedMask(bitmap.get(), radius);
  }
  return bitmap;
}

}  // namespace

Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new CFontDesc("Segoe UI", 14)),
      font_bold_(new CFontDesc("Segoe UI", 14, kBoldFace)),
      font_description_(new CFontDesc("Meiryo", 12)),
      font_small_(new CFontDesc("Segoe UI", 11)),
      font_heading_(new CFontDesc("Segoe UI", 13, kBoldFace)),
      font_strong_(new CFontDesc("Segoe UI", 16, kBoldFace)),
      page_views_(),
      page_tabs_() {
  setRect(ViewRect(0, 0, kWindowWidth, kWindowHeight));
}

Editor::~Editor() {
  font_->forget();
  font_bold_->forget();
  font_description_->forget();
  font_small_->forget();
  font_heading_->forget();
  font_strong_->forget();
}

auto PLUGIN_API Editor::open(void* const parent,
                             const PlatformType& /*platformType*/) -> bool {
  if (frame) {
    return false;
  }
  frame = new CFrame(CRect(0, 0, kWindowWidth, kWindowHeight), this);
  if (!frame) {
    return false;
  }
  auto* const beatrice_controller = static_cast<Controller*>(getController());

  // テクスチャ設定
  const auto frame_texture = SurfaceTextureParams{
      .base = CColor(0x09, 0x09, 0x09),
      .low_frequency_strength = 0.9,
      .fine_grain_strength = 0.9,
      .baked_grain_strength = 0.5,
  };
  const auto header_texture = SurfaceTextureParams{
      .base = CColor(0x12, 0x11, 0x0f),
      .low_frequency_strength = 5.9,
      .fine_grain_strength = 0.95,
      .baked_grain_strength = 1.08,
  };
  const auto tab_texture = SurfaceTextureParams{
      .base = CColor(0x1a, 0x19, 0x18),
      .low_frequency_strength = 17.6,
      .fine_grain_strength = 0.95,
      .baked_grain_strength = 1.08,
  };
  const auto page_texture = SurfaceTextureParams{
      .base = CColor(0x19, 0x19, 0x18),
      .low_frequency_strength = 1.0,
      .fine_grain_strength = 0.7,
      .baked_grain_strength = 0.45,
  };
  const auto panel_texture = SurfaceTextureParams{
      .base = CColor(0x1d, 0x1d, 0x1b),
      .low_frequency_strength = 4.7,
      .fine_grain_strength = 0.70,
      .baked_grain_strength = 0.86,
  };
  const auto control_texture = SurfaceTextureParams{
      .base = CColor(0x10, 0x10, 0x0f),
      .low_frequency_strength = 5.9,
      .fine_grain_strength = 0.95,
      .baked_grain_strength = 1.08,
  };
  const auto surface_noise = SurfaceNoiseParams{};
  const auto surface_noise_maps =
      std::make_shared<SurfaceNoiseMaps>(surface_noise);
  const auto frame_surface =
      VSTGUI::owned(new SurfaceBitmap(frame_texture, surface_noise_maps));
  const auto header_surface = VSTGUI::owned(
      new SurfaceBitmap(header_texture, surface_noise_maps, 512, 82));
  const auto tab_surface = VSTGUI::owned(
      new SurfaceBitmap(tab_texture, surface_noise_maps, 512, 52));
  const auto page_surface =
      VSTGUI::owned(new SurfaceBitmap(page_texture, surface_noise_maps));
  const auto panel_surface = VSTGUI::owned(
      new SurfaceBitmap(panel_texture, surface_noise_maps, 512, 480));
  const auto control_surface = VSTGUI::owned(
      new SurfaceBitmap(control_texture, surface_noise_maps, 512, 48));

  // ルートビュー
  auto* const root = new SurfacePanel(CRect(0, 0, kWindowWidth, kWindowHeight),
                                      frame_surface, kTransparentCColor, 0.0);
  frame->addView(root);

  // UI 生成ヘルパー
  const auto make_label =
      [&](CViewContainer* parent, const CRect& rect, const char* text,
          CFontRef font, const CColor& color,
          CHoriTxtAlign align = CHoriTxtAlign::kLeftText) -> CTextLabel* {
    auto* label = new CTextLabel(rect, text, nullptr, CParamDisplay::kNoFrame);
    label->setBackColor(kTransparentCColor);
    label->setFont(font);
    label->setFontColor(color);
    label->setHoriAlign(align);
    parent->addView(label);
    return label;
  };
  const auto register_control = [&](const ParamID param_id,
                                    CControl* const control) -> void {
    const auto [it, inserted] = controls_.emplace(param_id, control);
    assert(inserted);
    if (!inserted) {
      it->second = control;
    }
  };
  const auto add_title = [&](CViewContainer* parent, const CRect& rect,
                             const char* text) -> CTextLabel* {
    return make_label(parent, rect, text, font_heading_,
                      CColor(0xc1, 0xbe, 0xb8), CHoriTxtAlign::kCenterText);
  };
  const auto add_panel = [&](CViewContainer* parent,
                             const CRect& rect) -> SurfacePanel* {
    auto* panel = new SurfacePanel(rect, panel_surface,
                                   CColor(0xff, 0xff, 0xff, 0x0a), 2.0);
    parent->addView(panel);
    return panel;
  };
  const auto add_slider = [&](CViewContainer* parent, ParamID param_id,
                              const CRect& rect, int precision, float wheel_inc,
                              float fine_wheel_inc) -> Slider* {
    auto* const param =
        static_cast<LinearParameter*>(controller->getParameterObject(param_id));
    auto* const slider_bmp = new MonotoneBitmap(
        static_cast<int>(rect.getWidth()), static_cast<int>(rect.getHeight()),
        kTransparentCColor, kTransparentCColor);
    auto* const handle_bmp = new MonotoneBitmap(
        kSliderKnobWidth, 18, CColor(0xc3, 0xa0, 0x66), kTransparentCColor);
    const auto title =
        Steinberg::Vst::StringConvert::convert(param->getInfo().title);
    auto* slider = new Slider(
        rect, this, static_cast<int>(param_id), static_cast<int>(rect.left),
        static_cast<int>(rect.right - handle_bmp->getWidth()), handle_bmp,
        slider_bmp,
        Steinberg::Vst::StringConvert::convert(param->getInfo().units),
        font_small_, font_bold_, title, precision);
    slider->setMin(param->GetMinPlain());
    slider->setMax(param->GetMaxPlain());
    slider->setWheelInc(wheel_inc);
    slider->setFineWheelInc(fine_wheel_inc);
    slider->setDefaultValue(
        param->toPlain(param->getInfo().defaultNormalizedValue));
    slider->setValue(
        static_cast<float>(param->toPlain(param->getNormalized())));
    parent->addView(slider);
    register_control(param_id, slider);
    slider_bmp->forget();
    handle_bmp->forget();
    return slider;
  };
  const auto add_option_menu =
      [&](CViewContainer* parent, ParamID param_id, const CRect& rect,
          const CColor& frame_color = CColor(0xe2, 0xba, 0x79, 0x16),
          const CColor& back_color = CColor(0x1b, 0x1a, 0x19)) -> COptionMenu* {
    auto* const param = static_cast<StringListParameter*>(
        controller->getParameterObject(param_id));
    auto* const bmp = new MonotoneBitmap(static_cast<int>(rect.getWidth()),
                                         static_cast<int>(rect.getHeight()),
                                         back_color, frame_color, 3.0);
    auto* const control =
        new COptionMenu(rect, this, static_cast<int>(param_id), bmp);
    bmp->forget();
    for (auto i = 0; i <= param->getInfo().stepCount; ++i) {
      String128 tmp_string128;
      param->toString(param->toNormalized(i), tmp_string128);
      const auto name = Steinberg::Vst::StringConvert::convert(tmp_string128);
      control->addEntry(name.c_str());
    }
    control->setValue(static_cast<float>(
        param->toPlain(controller->getParamNormalized(param_id))));
    control->setFont(font_);
    control->setFontColor(CColor(0xca, 0xc7, 0xc1));
    parent->addView(control);
    auto* const chevron = new ChevronView(
        CRect(rect.right - 24, rect.top + 8, rect.right - 8, rect.bottom - 8),
        CColor(0xd6, 0xa8, 0x57));
    parent->addView(chevron);
    register_control(param_id, control);
    return control;
  };

  // ヘッダー
  auto* const header =
      new SurfacePanel(CRect(0, 0, kWindowWidth, 82), header_surface,
                       CColor(0xff, 0xff, 0xff, 0x10), 0.0);
  root->addView(header);
  auto* const logo_view = new CView(CRect(26, 19, 158, 63));
  auto* const logo_bmp = new CBitmap("logo.png");
  logo_view->setBackground(logo_bmp);
  header->addView(logo_view);
  logo_bmp->forget();
  make_label(header, CRect(178, 32, 310, 54), "VOICE CONVERSION", font_small_,
             CColor(0xd6, 0xa8, 0x57));

  auto* const model_panel =
      new SurfacePanel(CRect(380, 18, 900, 66), control_surface,
                       CColor(0xff, 0xff, 0xff, 0x0e), 3.0);
  header->addView(model_panel);
  make_label(model_panel, CRect(12, 17, 76, 31), "MODEL", font_small_,
             CColor(0xd6, 0xa8, 0x57));
  auto* const model_selector =
      new FileSelector(CRect(80, 0, 440, 48), this,
                       static_cast<int>(ParameterID::kModel), nullptr);
  model_selector->setBackColor(kTransparentCColor);
  model_selector->setStyle(CParamDisplay::kNoFrame);
  model_selector->setFont(font_strong_);
  model_selector->setFontColor(CColor(0xca, 0xc7, 0xc1));
  model_selector->setHoriAlign(CHoriTxtAlign::kCenterText);
  model_panel->addView(model_selector);
  register_control(static_cast<ParamID>(ParameterID::kModel), model_selector);
  model_name_label_ = model_selector;

  // タブ
  auto* const tabs =
      new SurfacePanel(CRect(0, 82, kWindowWidth, 134), tab_surface,
                       CColor(0xff, 0xff, 0xff, 0x12), 0.0);
  root->addView(tabs);
  tab_indicator_ = new TabAccentView(CRect(528, 0, 640, 52));
  tabs->addView(tab_indicator_);
  auto add_tab = [&](int index, const CRect& rect, const char* text) -> void {
    auto* tab = new GlowingActionLabel(
        rect, text, [this, index]() -> void { SelectPage(index); });
    tab->setBackColor(kTransparentCColor);
    tab->setFont(font_);
    tab->setFontColor(CColor(0xb8, 0xb5, 0xaf));
    tab->setHoriAlign(CHoriTxtAlign::kCenterText);
    tab->setStyle(CParamDisplay::kNoFrame);
    tabs->addView(tab);
    page_tabs_[index] = tab;
  };
  add_tab(0, CRect(528, 0, 640, 52), "MAIN");
  add_tab(1, CRect(640, 0, 752, 52), "TUNING");
  make_label(tabs, CRect(1070, 15, 1252, 37),
             (UTF8String("Ver. ") + FULL_VERSION_STR).data(), font_small_,
             CColor(0x77, 0x74, 0x70), CHoriTxtAlign::kRightText);

  // ページコンテナ
  auto* const main_page = new SurfacePanel(
      CRect(0, 134, 1280, 720), page_surface, kTransparentCColor, 0.0);
  root->addView(main_page);
  page_views_[0] = main_page;
  auto* const tuning_page = new SurfacePanel(
      CRect(0, 134, 1280, 720), page_surface, kTransparentCColor, 0.0);
  root->addView(tuning_page);
  page_views_[1] = tuning_page;

  // Main ページ左
  auto* gain_panel = add_panel(main_page, CRect(14, 10, 292, 168));
  add_title(gain_panel, CRect(0, 10, 278, 32), "GAIN");
  add_slider(gain_panel, static_cast<ParamID>(ParameterID::kInputGain),
             CRect(17, 45, 261, 88), 1, 1.0f, 0.1f);
  add_slider(gain_panel, static_cast<ParamID>(ParameterID::kOutputGain),
             CRect(17, 102, 261, 145), 1, 1.0f, 0.1f);

  auto* pitch_panel = add_panel(main_page, CRect(14, 178, 292, 340));
  add_title(pitch_panel, CRect(0, 12, 278, 34), "PITCH");
  add_slider(pitch_panel, static_cast<ParamID>(ParameterID::kPitchShift),
             CRect(17, 48, 261, 91), 2, 1.0f, 0.125f);
  make_label(pitch_panel, CRect(17, 101, 80, 119), "Lock", font_small_,
             CColor(0xb8, 0xb5, 0xaf));
  add_option_menu(pitch_panel, static_cast<ParamID>(ParameterID::kLock),
                  CRect(17, 123, 261, 149));

  auto* source_panel = add_panel(main_page, CRect(14, 350, 292, 572));
  add_title(source_panel, CRect(0, 13, 278, 35), "SOURCE PITCH");
  add_slider(source_panel,
             static_cast<ParamID>(ParameterID::kAverageSourcePitch),
             CRect(17, 60, 261, 103), 2, 1.0f, 0.125f);
  add_slider(source_panel, static_cast<ParamID>(ParameterID::kMinSourcePitch),
             CRect(17, 112, 261, 155), 2, 1.0f, 0.125f);
  add_slider(source_panel, static_cast<ParamID>(ParameterID::kMaxSourcePitch),
             CRect(17, 164, 261, 207), 2, 1.0f, 0.125f);

  // Main ページ中央
  auto* portrait_panel =
      new SurfacePanel(CRect(302, 10, 782, 490), panel_surface,
                       CColor(0xd6, 0xa8, 0x57, 0x24), 3.0);
  main_page->addView(portrait_panel);
  portrait_view_ = new CView(CRect(0, 0, 480, 480));
  portrait_panel->addView(portrait_view_);
  unloaded_logo_view_ = new CView(CRect(174, 218, 306, 262));
  auto* const unloaded_logo = new CBitmap("logo.png");
  unloaded_logo_view_->setBackground(unloaded_logo);
  unloaded_logo_view_->setVisible(false);
  portrait_panel->addView(unloaded_logo_view_);
  unloaded_logo->forget();

  morph_pad_controller_ = std::make_unique<MorphPadController>(
      beatrice_controller->core_, *beatrice_controller);
  auto* const morph_pad =
      new MorphPadView(CRect(0, 0, 480, 480), morph_pad_controller_.get(),
                       font_bold_, font_small_);
  morph_pad_view_ = morph_pad;
  morph_pad_view_->setVisible(false);
  portrait_panel->addView(morph_pad_view_);

  portrait_description_pane_ = new DescriptionPane(
      CRect(302, 500, 782, 572), panel_surface, CColor(0xff, 0xff, 0xff, 0x0a),
      2.0, "PORTRAIT DESCRIPTION", CRect(14, 8, 466, 26),
      CRect(14, 35, 469, 68), font_heading_, font_description_,
      CColor(0xc1, 0xbe, 0xb8), CColor(0xbb, 0xb8, 0xb2),
      CRect(220, 224, 864, 701),
      [this](const char* const title, const std::u8string& text,
             const CRect popup_rect) -> void {
        ShowDescriptionPopup(title, text, popup_rect);
      });
  main_page->addView(portrait_description_pane_);

  const auto falloff_rect = CRect(14, 28, 466, 71);
  auto* const falloff_slider_bmp =
      new MonotoneBitmap(static_cast<int>(falloff_rect.getWidth()),
                         static_cast<int>(falloff_rect.getHeight()),
                         kTransparentCColor, kTransparentCColor);
  auto* const falloff_handle_bmp = new MonotoneBitmap(
      kSliderKnobWidth, 18, CColor(0xc3, 0xa0, 0x66), kTransparentCColor);
  auto* const morph_falloff = new MorphFalloffSlider(
      falloff_rect, this, static_cast<int32_t>(ParameterID::kVoiceMorphFalloff),
      falloff_handle_bmp, falloff_slider_bmp, font_small_, font_bold_);
  morph_falloff->SetAuxiliaryLabelStateChangedCallback(
      [this](const bool show_labels) -> void {
        if (morph_pad_view_) {
          morph_pad_view_->SetShowAuxiliaryLabels(show_labels);
        }
      });
  morph_falloff_slider_ = morph_falloff;
  morph_falloff_slider_->setVisible(false);
  portrait_description_pane_->addView(morph_falloff_slider_);
  register_control(static_cast<ParamID>(ParameterID::kVoiceMorphFalloff),
                   morph_falloff);
  falloff_slider_bmp->forget();
  falloff_handle_bmp->forget();

  // Main ページ右
  auto* const voice_panel = add_panel(main_page, CRect(792, 10, 1266, 244));
  add_title(voice_panel, CRect(0, 12, 473, 34), "VOICE");
  auto* const voice_menu =
      add_option_menu(voice_panel, static_cast<ParamID>(ParameterID::kVoice),
                      CRect(16, 40, 458, 98), CColor(0x44, 0x38, 0x22),
                      CColor(0x11, 0x10, 0x0f));
  voice_menu->setMouseEnabled(false);
  voice_menu->setFontColor(kTransparentCColor);
  voice_selector_ = new VoiceSelectorView(
      CRect(16, 40, 458, 98), [this]() -> void { ToggleVoiceMenu(); },
      font_bold_);
  voice_panel->addView(voice_selector_);
  add_slider(voice_panel, static_cast<ParamID>(ParameterID::kFormantShift),
             CRect(16, 118, 458, 161), 2, 1.0f, 0.5f);
  add_slider(voice_panel, static_cast<ParamID>(ParameterID::kVQNumNeighbors),
             CRect(16, 176, 458, 219), 0, 1.0f, 1.0f);

  model_description_pane_ = new DescriptionPane(
      CRect(792, 254, 1266, 410), panel_surface, CColor(0xff, 0xff, 0xff, 0x0a),
      2.0, "MODEL DESCRIPTION", CRect(15, 12, 458, 30), CRect(15, 43, 462, 146),
      font_heading_, font_description_, CColor(0xc1, 0xbe, 0xb8),
      CColor(0xbb, 0xb8, 0xb2), CRect(622, 224, 1266, 701),
      [this](const char* const title, const std::u8string& text,
             const CRect popup_rect) -> void {
        ShowDescriptionPopup(title, text, popup_rect);
      });
  main_page->addView(model_description_pane_);

  voice_description_pane_ = new DescriptionPane(
      CRect(792, 420, 1266, 572), panel_surface, CColor(0xff, 0xff, 0xff, 0x0a),
      2.0, "VOICE DESCRIPTION", CRect(15, 12, 458, 30), CRect(15, 43, 462, 144),
      font_heading_, font_description_, CColor(0xc1, 0xbe, 0xb8),
      CColor(0xbb, 0xb8, 0xb2), CRect(622, 271, 1266, 706),
      [this](const char* const title, const std::u8string& text,
             const CRect popup_rect) -> void {
        ShowDescriptionPopup(title, text, popup_rect);
      });
  main_page->addView(voice_description_pane_);

  // Tuning ページ
  auto* tuning_panel = new SurfacePanel(CRect(16, 16, 336, 304), panel_surface,
                                        CColor(0xff, 0xff, 0xff, 0x0d), 3.0);
  tuning_page->addView(tuning_panel);
  auto* const detail_heading_font = new CFontDesc("Segoe UI", 28);
  auto* const detail_title =
      make_label(tuning_panel, CRect(28, 27, 292, 62), "Tuning",
                 detail_heading_font, CColor(0xf0, 0xcf, 0x90));
  detail_title->setMouseEnabled(false);
  detail_heading_font->forget();
  add_slider(tuning_panel,
             static_cast<ParamID>(ParameterID::kIntonationIntensity),
             CRect(28, 80, 292, 123), 1, 0.5f, 0.1f);
  add_slider(tuning_panel, static_cast<ParamID>(ParameterID::kPitchCorrection),
             CRect(28, 140, 292, 183), 1, 0.5f, 0.1f);
  make_label(tuning_panel, CRect(28, 210, 292, 228), "Pitch Correction Type",
             font_small_, CColor(0xb8, 0xb5, 0xaf));
  add_option_menu(tuning_panel,
                  static_cast<ParamID>(ParameterID::kPitchCorrectionType),
                  CRect(28, 236, 292, 264));

  // Voice 選択メニュー
  voice_menu_overlay_ = new VoiceMenuOverlayView(
      CRect(0, 0, kWindowWidth, kWindowHeight), panel_surface, font_,
      [this](const int voice_id) -> void { SelectVoice(voice_id); });
  root->addView(voice_menu_overlay_);

  // Description の拡大表示
  description_popup_ = new DescriptionPopupView(
      CRect(0, 0, kWindowWidth, kWindowHeight), panel_surface,
      CColor(0xd6, 0xa8, 0x57, 0x2b), 3.0, font_bold_, font_description_);
  root->addView(description_popup_);

  SelectPage(0);

  if (!frame->open(parent)) {
    close();
    return false;
  }

  if (const auto* value = std::get_if<std::unique_ptr<std::u8string>>(
          &beatrice_controller->core_.parameter_state_.GetValue(
              ParameterID::kModel));
      value && *value) {
    static_cast<FileSelector*>(
        controls_.at(static_cast<ParamID>(ParameterID::kModel)))
        ->SetPath(**value);
  }
  SyncModelDescription();
  SyncSourcePitchRange();
  SyncParameterAvailability();

  // デバッグ用
  if (const auto model_path = GetScreenshotModelPath(); !model_path.empty()) {
    auto* const model_control = static_cast<FileSelector*>(
        controls_.at(static_cast<ParamID>(ParameterID::kModel)));
    model_control->SetPath(model_path);
    valueChanged(model_control);
  }
  if (HasEnvironmentVariable("BEATRICE_SCREENSHOT_VOICE_MORPH") &&
      model_config_.has_value() && common::GetVoiceCount(*model_config_) > 1) {
    auto* const voice_control = static_cast<COptionMenu*>(
        controls_.at(static_cast<ParamID>(ParameterID::kVoice)));
    voice_control->setValue(voice_control->getMax());
    valueChanged(voice_control);
  }

  return true;
}
void PLUGIN_API Editor::close() {
  if (frame) {
    if (morph_pad_view_ && morph_pad_view_->isEditing()) {
      morph_pad_view_->endEdit();
    }
    frame->forget();
    frame = nullptr;
    controls_.clear();
    portraits_.clear();
    portrait_menu_thumbnails_.clear();
    portrait_marker_thumbnails_.clear();
    portrait_view_ = nullptr;
    unloaded_logo_view_ = nullptr;
    morph_pad_controller_.reset();
    morph_pad_view_ = nullptr;
    portrait_description_pane_ = nullptr;
    morph_falloff_slider_ = nullptr;
    model_description_pane_ = nullptr;
    voice_description_pane_ = nullptr;
    voice_selector_ = nullptr;
    voice_menu_overlay_ = nullptr;
    description_popup_ = nullptr;
    model_name_label_ = nullptr;
    page_views_ = {};
    page_tabs_ = {};
    tab_indicator_ = nullptr;
    voice_morph_state_ = {};
  }
}

void Editor::SelectPage(const int page) {
  static constexpr auto kIndicatorRects =
      std::array{CRect(528, 0, 640, 52), CRect(640, 0, 752, 52)};
  const auto page_count = static_cast<int>(page_views_.size());
  const auto indicator_count = static_cast<int>(kIndicatorRects.size());
  for (auto i = 0; i < page_count; ++i) {
    if (page_views_[i]) {
      page_views_[i]->setVisible(i == page);
      page_views_[i]->setDirty();
    }
    if (page_tabs_[i]) {
      const auto selected = i == page;
      page_tabs_[i]->setFontColor(selected ? CColor(0xf0, 0xcf, 0x90)
                                           : CColor(0xb8, 0xb5, 0xaf));
      page_tabs_[i]->setStyle(CParamDisplay::kNoFrame);
      if (auto* const tab = dynamic_cast<GlowingActionLabel*>(page_tabs_[i])) {
        tab->SetActive(selected);
      }
      page_tabs_[i]->setDirty();
    }
  }
  if (tab_indicator_ && page >= 0 && page < indicator_count) {
    tab_indicator_->setViewSize(kIndicatorRects[page]);
    tab_indicator_->setMouseableArea(kIndicatorRects[page]);
    tab_indicator_->setDirty();
  }
  HideVoiceMenu();
  HideDescriptionPopup();
}

void Editor::SetPortraitDescriptionText(const std::u8string& text) {
  if (portrait_description_pane_) {
    portrait_description_pane_->SetText(text);
  }
}

void Editor::SetModelDescriptionText(const std::u8string& text) {
  if (model_description_pane_) {
    model_description_pane_->SetText(text);
  }
}

void Editor::SetVoiceDescriptionText(const std::u8string& text) {
  if (voice_description_pane_) {
    voice_description_pane_->SetText(text);
  }
}

void Editor::SetPortraitDescriptionMode(const bool morphing) {
  if (portrait_description_pane_) {
    portrait_description_pane_->SetTitle(morphing ? "MORPH CONTROLS"
                                                  : "PORTRAIT DESCRIPTION");
    portrait_description_pane_->SetBodyVisible(!morphing);
  }
  if (morph_falloff_slider_) {
    morph_falloff_slider_->setVisible(morphing);
    morph_falloff_slider_->setDirty();
  }
}

void Editor::SetVoiceSelectorDisplay(const int voice_id) {
  if (voice_selector_) {
    voice_selector_->SetDisplay(model_config_, portrait_menu_thumbnails_,
                                voice_id);
  }
}

void Editor::ToggleVoiceMenu() {
  auto* const voice_control = GetVoiceControl();
  if (!voice_menu_overlay_ || !voice_control) {
    return;
  }
  const auto selected_voice_id =
      static_cast<int>(std::round(voice_control->getValue()));
  voice_menu_overlay_->ToggleMenu(model_config_, portrait_menu_thumbnails_,
                                  selected_voice_id);
}

void Editor::HideVoiceMenu() {
  if (voice_menu_overlay_) {
    voice_menu_overlay_->HideMenu();
  }
}

void Editor::RebuildVoiceMenu() {
  auto* const voice_control = GetVoiceControl();
  if (!voice_menu_overlay_ || !voice_control) {
    return;
  }
  const auto selected_voice_id =
      static_cast<int>(std::round(voice_control->getValue()));
  voice_menu_overlay_->RebuildMenu(model_config_, portrait_menu_thumbnails_,
                                   selected_voice_id);
}

void Editor::SelectVoice(const int voice_id) {
  auto* const voice_control = GetVoiceControl();
  if (!voice_control) {
    return;
  }
  voice_control->beginEdit();
  voice_control->setValue(static_cast<float>(voice_id));
  voice_control->valueChanged();
  voice_control->endEdit();
}

auto Editor::GetVoiceControl() const -> COptionMenu* {
  const auto voice_param_id = static_cast<ParamID>(ParameterID::kVoice);
  const auto it = controls_.find(voice_param_id);
  if (it == controls_.end()) {
    return nullptr;
  }
  return static_cast<COptionMenu*>(it->second);
}

void Editor::ShowDescriptionPopup(const char* const title,
                                  const std::u8string& text, CRect size) {
  HideVoiceMenu();
  if (description_popup_) {
    description_popup_->Show(title, text, size);
  }
}

void Editor::HideDescriptionPopup() {
  if (description_popup_) {
    description_popup_->Hide();
  }
}

void Editor::ApplyVoiceMorphState(const common::VoiceMorphState& state) {
  voice_morph_state_ = state;
  if (model_config_.has_value()) {
    const auto voice_count = common::GetVoiceCount(*model_config_);
    assert(voice_count > 0);
    if (voice_count > 0) {
      for (auto i = 0; i < voice_morph_state_.marker_count; ++i) {
        voice_morph_state_.markers[i].voice_id = std::clamp(
            voice_morph_state_.markers[i].voice_id, 0, voice_count - 1);
      }
    }
  }

  if (morph_pad_view_) {
    morph_pad_view_->SetState(voice_morph_state_);
  }
  if (morph_falloff_slider_) {
    morph_falloff_slider_->SetValue(voice_morph_state_.falloff);
  }
  UpdateVoiceMorphingDescription();
}

void Editor::PerformParameterEdit(const ParamID param_id,
                                  const ParamValue normalized_value) {
  auto* const controller = static_cast<Controller*>(getController());
  if (controller->setParamNormalized(param_id, normalized_value) ==
      Steinberg::kResultTrue) {
    controller->performEdit(param_id, controller->getParamNormalized(param_id));
  }
}

void Editor::SendParameterEdit(const ParamID param_id,
                               const ParamValue normalized_value) {
  auto* const controller = static_cast<Controller*>(getController());
  controller->beginEdit(param_id);
  PerformParameterEdit(param_id, normalized_value);
  controller->endEdit(param_id);
}

// DAW 側から GUI にパラメータ変更を伝える。
// Voice など、controller と editor で最大値が異なるパラメータがあるため、
// controller->setValueNormalized は使わない。
// valueChanged からも controller を介して間接的に呼ばれる。
// 引数じゃなくて core から値を取った方が良い？
void Editor::SyncValue(const ParamID param_id, const float plain_value) {
  const auto parameter_id = static_cast<ParameterID>(param_id);
  if (common::IsVoiceMorphParameter(parameter_id)) {
    auto* const controller = static_cast<Controller*>(getController());
    ApplyVoiceMorphState(
        common::GetVoiceMorphState(controller->core_.parameter_state_));
    return;
  }

  if (!frame || !controls_.contains(param_id)) {
    return;
  }
  auto* const control = controls_.at(param_id);
  // Voice は色々ややこしいので特別扱いする
  if (param_id == static_cast<ParamID>(ParameterID::kVoice)) {
    const auto voice_id = static_cast<int>(std::round(plain_value));
    control->setValue(plain_value);
    if (!model_config_.has_value()) {
      portrait_view_->setBackground(nullptr);
      portrait_view_->setVisible(false);
      unloaded_logo_view_->setVisible(true);
      morph_pad_view_->setVisible(false);
      SetPortraitDescriptionMode(false);
      SetPortraitDescriptionText(u8"");
      SetVoiceDescriptionText(u8"");
      SetVoiceSelectorDisplay(-1);
    } else if (voice_id == 0 ||
               voice_id < static_cast<int>(control->getMax())) {
      portrait_view_->setBackground(
          portraits_.at(model_config_->voices[voice_id].portrait.path).get());
      portrait_view_->setVisible(true);
      unloaded_logo_view_->setVisible(false);
      morph_pad_view_->setVisible(false);
      SetPortraitDescriptionMode(false);
      SetVoiceSelectorDisplay(voice_id);
      SetPortraitDescriptionText(
          model_config_->voices[voice_id].portrait.description);
      SetVoiceDescriptionText(model_config_->voices[voice_id].description);
    } else {
      portrait_view_->setBackground(nullptr);
      portrait_view_->setVisible(false);
      unloaded_logo_view_->setVisible(false);
      morph_pad_view_->setVisible(true);
      SetPortraitDescriptionMode(true);
      SetPortraitDescriptionText(u8"");
      SetVoiceSelectorDisplay(-2);
      UpdateVoiceMorphingDescription();
    }
    if (voice_menu_overlay_ && voice_menu_overlay_->IsMenuVisible()) {
      RebuildVoiceMenu();
    }
  } else {
    control->setValue(plain_value);
  }
  control->setDirty();
}

void Editor::SyncStringValue(const ParamID param_id,
                             const std::u8string& value) {
  if (!frame || !controls_.contains(param_id)) {
    return;
  }
  auto* const control = static_cast<CTextLabel*>(controls_.at(param_id));
  if (param_id == static_cast<ParamID>(ParameterID::kModel)) {
    auto* const model_selector = static_cast<FileSelector*>(control);
    model_selector->SetPath(value);
    SyncModelDescription();
    SyncSourcePitchRange();
    SyncParameterAvailability();
  } else {
    control->setText(reinterpret_cast<const char*>(value.c_str()));
  }
}

// 現在読み込まれているモデルをもとに
// min_source_pitch, max_source_pitch の範囲を更新する。
void Editor::SyncSourcePitchRange() {
  if (!model_config_.has_value() || model_config_->model.VersionInt() < 0) {
    return;
  }
  auto* const min_source_pitch_slider = static_cast<Slider*>(
      controls_.at(static_cast<ParamID>(ParameterID::kMinSourcePitch)));
  auto* const max_source_pitch_slider = static_cast<Slider*>(
      controls_.at(static_cast<ParamID>(ParameterID::kMaxSourcePitch)));
  // MIDI ノートナンバー
  const auto min_source_pitch = 33.125f;
  const auto version_to_max_source_pitch = std::array<float, 3>{
      33.0f + (BEATRICE_20A2_PITCH_BINS - 1) *
                  (12.0f / BEATRICE_PITCH_BINS_PER_OCTAVE),
      33.0f + (BEATRICE_20B1_PITCH_BINS - 1) *
                  (12.0f / BEATRICE_PITCH_BINS_PER_OCTAVE),
      33.0f + (BEATRICE_20RC0_PITCH_BINS - 1) *
                  (12.0f / BEATRICE_PITCH_BINS_PER_OCTAVE)};
  const auto max_source_pitch = version_to_max_source_pitch[std::min(
      model_config_->model.VersionInt(),
      static_cast<int>(version_to_max_source_pitch.size()) - 1)];
  min_source_pitch_slider->setMin(min_source_pitch);
  max_source_pitch_slider->setMin(min_source_pitch);
  min_source_pitch_slider->setMax(max_source_pitch);
  max_source_pitch_slider->setMax(max_source_pitch);
  min_source_pitch_slider->setDirty();
  max_source_pitch_slider->setDirty();
}

// 現在読み込まれているモデルをもとに
// パラメータの有効/無効を更新する。
void Editor::SyncParameterAvailability() {
  if (!model_config_.has_value() || model_config_->model.VersionInt() < 0) {
    return;
  }
  auto* const vq_num_neighbors_slider = static_cast<Slider*>(
      controls_.at(static_cast<ParamID>(ParameterID::kVQNumNeighbors)));
  vq_num_neighbors_slider->SetEnabled(model_config_->model.VersionInt() >= 2);
  vq_num_neighbors_slider->setDirty();
}

// model_selector->getPath() をもとに
// 話者リスト等を更新する
void Editor::SyncModelDescription() {
  auto* const controller = static_cast<Controller*>(getController());
  auto* const model_selector = static_cast<FileSelector*>(
      controls_.at(static_cast<ParamID>(ParameterID::kModel)));
  auto* const voice_menu = static_cast<COptionMenu*>(
      controls_.at(static_cast<ParamID>(ParameterID::kVoice)));
  const auto file = model_selector->GetPath();
  model_selector->setText("<unloaded>");
  voice_menu->removeAllEntry();
  SetModelDescriptionText(u8"");
  SetVoiceDescriptionText(u8"");
  SetPortraitDescriptionText(u8"");
  if (portrait_view_) {
    portrait_view_->setBackground(nullptr);
    portrait_view_->setVisible(false);
  }
  SetPortraitDescriptionMode(false);
  if (unloaded_logo_view_) {
    unloaded_logo_view_->setVisible(true);
  }
  if (morph_pad_view_) {
    morph_pad_view_->setVisible(false);
  }
  model_config_ = std::nullopt;
  SetVoiceSelectorDisplay(-1);
  RebuildVoiceMenu();
  portraits_.clear();
  portrait_menu_thumbnails_.clear();
  portrait_marker_thumbnails_.clear();
  if (morph_pad_view_) {
    morph_pad_view_->SetVoices({}, {});
  }
  if (file.empty()) {
    // 初期状態
    return;
  }
  auto file_error = std::error_code{};
  auto stream = std::ifstream{};
  if (std::filesystem::is_regular_file(file, file_error)) {
    stream.open(file, std::ios::binary);
  }
  if (!stream.is_open()) {
    // ファイルが移動して読み込めない場合の分岐だが、
    // モデルを読み込んだ後に GUI を閉じモデルファイルを移動して
    // 再び GUI を開いた場合などには
    // Processor のみ読み込まれている可能性がある。
    model_selector->setText("<failed to load>");
    SetModelDescriptionText(
        u8"Error: The model could not be loaded due to a file move or another "
        u8"issue. Please reload a valid model.");
    return;
  }
  try {
    const auto source_path = file.u8string();
    const auto source_name = std::string(
        reinterpret_cast<const char*>(source_path.data()), source_path.size());
    const auto toml_data = toml::parse(stream, source_name);
    model_config_ = toml::get<common::ModelConfig>(toml_data);
    if (model_config_->model.VersionInt() == -1) {
      SetModelDescriptionText(u8"Error: Unknown model version.");
      return;
    }
    model_selector->setText(
        reinterpret_cast<const char*>(model_config_->model.name.c_str()));
    const auto voice_count = common::GetVoiceCount(*model_config_);
    // 話者のリストを読み込む。
    // また、予め portrait を読み込んで、必要に応じてリサイズしておく。
    for (auto i = 0; i < voice_count; ++i) {
      const auto& voice = model_config_->voices[i];
      voice_menu->addEntry(reinterpret_cast<const char*>(voice.name.c_str()));

      // portrait
      {
        if (portraits_.contains(voice.portrait.path)) {
          goto load_portrait_succeeded;
        }
        const auto portrait_file = file.parent_path() / voice.portrait.path;
        auto portrait_error = std::error_code{};
        if (!std::filesystem::is_regular_file(portrait_file, portrait_error)) {
          goto load_portrait_failed;
        }
        const auto platform_bitmap = getPlatformFactory().createBitmapFromPath(
            reinterpret_cast<const char*>(portrait_file.u8string().c_str()));
        if (!platform_bitmap) {
          goto load_portrait_failed;
        }
        const auto original_bitmap =
            VSTGUI::owned(new CBitmap(platform_bitmap));
        auto rounded_portrait = MakeRoundedBitmap(
            original_bitmap.get(), kPortraitWidth, kPortraitHeight, 4.0);
        auto menu_thumbnail = ScaleBitmap(original_bitmap.get(), 42, 42);
        auto circular_thumbnail =
            MakeRoundedBitmap(original_bitmap.get(), 58, 58, 29.0);
        if (!rounded_portrait || !menu_thumbnail || !circular_thumbnail) {
          goto load_portrait_failed;
        }
        portraits_.insert({voice.portrait.path, rounded_portrait});
        portrait_menu_thumbnails_.insert({voice.portrait.path, menu_thumbnail});
        portrait_marker_thumbnails_.insert(
            {voice.portrait.path, circular_thumbnail});
        goto load_portrait_succeeded;
      }
      assert(false);
    load_portrait_failed:
      portraits_.insert({voice.portrait.path, nullptr});
      portrait_menu_thumbnails_.insert({voice.portrait.path, nullptr});
      portrait_marker_thumbnails_.insert({voice.portrait.path, nullptr});
    load_portrait_succeeded: {}
    }

    if (voice_count > 1) {
      const auto flags = model_config_->model.VersionInt() <= 2
                             ? VSTGUI::CMenuItem::kNoFlags
                             : VSTGUI::CMenuItem::kDisabled;
      voice_menu->addEntry("Voice Morphing Mode", -1, flags);
      portraits_.insert({u8"", nullptr});
      portrait_menu_thumbnails_.insert({u8"", nullptr});
      portrait_marker_thumbnails_.insert({u8"", nullptr});
    }

    if (morph_pad_view_) {
      std::vector<SharedPointer<CBitmap>> marker_bitmaps;
      std::vector<std::string> voice_names;
      marker_bitmaps.reserve(static_cast<size_t>(voice_count));
      voice_names.reserve(static_cast<size_t>(voice_count));
      for (auto i = 0; i < voice_count; ++i) {
        const auto& path = model_config_->voices[i].portrait.path;
        if (const auto it = portrait_marker_thumbnails_.find(path);
            it != portrait_marker_thumbnails_.end()) {
          marker_bitmaps.push_back(it->second);
        } else {
          marker_bitmaps.emplace_back(nullptr);
        }
        const auto& name = model_config_->voices[i].name;
        voice_names.emplace_back(name.begin(), name.end());
      }
      morph_pad_view_->SetVoices(marker_bitmaps, voice_names);
    }

    voice_menu->setDirty();
    ApplyVoiceMorphState(
        common::GetVoiceMorphState(controller->core_.parameter_state_));
    auto voice_id =
        Denormalize(std::get<common::ListParameter>(
                        common::kSchema.GetParameter(ParameterID::kVoice)),
                    controller->getParamNormalized(
                        static_cast<ParamID>(ParameterID::kVoice)));
    if (voice_id < voice_count) {
      const auto& voice = model_config_->voices[voice_id];
      portrait_view_->setBackground(portraits_.at(voice.portrait.path).get());
      portrait_view_->setVisible(true);
      unloaded_logo_view_->setVisible(false);
      morph_pad_view_->setVisible(false);
      SetPortraitDescriptionMode(false);
      SetPortraitDescriptionText(voice.portrait.description);
      SetVoiceDescriptionText(voice.description);
      SetVoiceSelectorDisplay(voice_id);
    } else {
      portrait_view_->setBackground(nullptr);
      portrait_view_->setVisible(false);
      unloaded_logo_view_->setVisible(false);
      morph_pad_view_->setVisible(true);
      SetPortraitDescriptionMode(true);
      SetPortraitDescriptionText(u8"");
      SetVoiceSelectorDisplay(-2);
      UpdateVoiceMorphingDescription();
    }
    SetModelDescriptionText(model_config_->model.description);
    RebuildVoiceMenu();

    portrait_view_->setDirty();
    if (portrait_description_pane_) {
      portrait_description_pane_->setDirty();
    }
    unloaded_logo_view_->setDirty();
    morph_pad_view_->setDirty();
  } catch (const std::exception& e) {
    model_selector->setText("<failed to load>");
    SetModelDescriptionText(
        u8"Error:\n" +
        std::u8string(e.what(), e.what() + std::strlen(e.what())));
    return;
  }
}

// GUI でパラメータに変更があったときに、DAW に伝える。
// あとダブルクリックでデフォルトに戻したい。
void Editor::valueChanged(CControl* const pControl) {
  assert(pControl);
  if (!pControl) {
    return;
  }
  const auto vst_param_id = pControl->getTag();
  const auto param_id = static_cast<ParameterID>(vst_param_id);
  const auto& param = common::kSchema.GetParameter(param_id);
  auto* const controller = static_cast<Controller*>(getController());
  auto& core = controller->core_;
  // 各々の Control でやるべきという感じも
  if (auto* const control = dynamic_cast<Slider*>(pControl)) {
    // SendParameterEdit 含めて controller の中に処理書いた方が明快？
    const auto* const num_param = std::get_if<common::NumberParameter>(&param);
    assert(num_param);
    const auto normalized_value = Normalize(*num_param, control->getValue());
    const auto plain_value = Denormalize(*num_param, normalized_value);
    const auto control_value = static_cast<float>(plain_value);
    if (control->getValue() != control_value) {
      control->setValue(control_value);
      control->invalid();
    }
    if (plain_value ==
        std::get<double>(core.parameter_state_.GetValue(param_id))) {
      return;
    }
    core.parameter_state_.SetValue(param_id, plain_value);
    [[maybe_unused]] const auto error_code =
        num_param->ControllerSetValue(core, plain_value);
    assert(error_code == common::ErrorCode::kSuccess);
    PerformParameterEdit(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<COptionMenu*>(pControl)) {
    const auto* const list_param = std::get_if<common::ListParameter>(&param);
    assert(list_param);
    const auto plain_value = static_cast<int>(control->getValue());
    if (plain_value ==
        std::get<int>(core.parameter_state_.GetValue(param_id))) {
      return;
    }
    const auto normalized_value = Normalize(*list_param, plain_value);
    const auto error_code = list_param->ControllerSetValue(core, plain_value);
    if (error_code == common::ErrorCode::kSpeakerIDOutOfRange) {
      // これが表示されることは無いはず
      SetVoiceDescriptionText(u8"Error: Speaker ID out of range.");
    }
    assert(error_code == common::ErrorCode::kSuccess);
    PerformParameterEdit(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<FileSelector*>(pControl)) {
    const auto* const str_param = std::get_if<common::StringParameter>(&param);
    assert(str_param);
    const auto file = control->GetPath().u8string();
    auto error_code = str_param->ControllerSetValue(core, file);
    if (error_code == common::ErrorCode::kFileOpenError ||
        error_code == common::ErrorCode::kTOMLSyntaxError ||
        error_code == common::ErrorCode::kInvalidModelConfig) {
      // Controller とは別に Editor::SyncModelDescription でも改めて
      // ファイルを読み込もうとして失敗するので、ここではエラー処理しない
      error_code = common::ErrorCode::kSuccess;
    }
    assert(error_code == common::ErrorCode::kSuccess);
    if (error_code != common::ErrorCode::kSuccess) {
      return;
    }
    controller->SetStringParameter(vst_param_id, file);
    // processor に通知
    if (const auto msg = Steinberg::owned(controller->allocateMessage())) {
      msg->setMessageID("param_change");
      msg->getAttributes()->setBinary("param_id", &vst_param_id,
                                      sizeof(vst_param_id));
      msg->getAttributes()->setBinary(
          "data", file.c_str(), static_cast<Steinberg::uint32>(file.size()));
      controller->sendMessage(msg);
    }
  } else {
    assert(false);
  }

  // 連動するパラメータの処理
  for (const auto& param_id : core.updated_parameters_) {
    const auto vst_param_id = static_cast<ParamID>(param_id);
    const auto& value = core.parameter_state_.GetValue(param_id);
    const auto& param = common::kSchema.GetParameter(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      const auto normalized_value =
          Normalize(*num_param, std::get<double>(value));
      SendParameterEdit(vst_param_id, normalized_value);
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto normalized_value =
          Normalize(*list_param, std::get<int>(value));
      SendParameterEdit(vst_param_id, normalized_value);
    } else if (std::get_if<common::StringParameter>(&param)) {
      // 現状何かに連動して StringParameter が変化することはない
      assert(false);
    } else {
      assert(false);
    }
  }
  core.updated_parameters_.clear();
}

void Editor::beginEdit(const Steinberg::int32 index) {
  if (index >= 0) {
    Steinberg::Vst::VSTGUIEditor::beginEdit(index);
  }
}

void Editor::endEdit(const Steinberg::int32 index) {
  if (index >= 0) {
    Steinberg::Vst::VSTGUIEditor::endEdit(index);
  }
}

void Editor::UpdateVoiceMorphingDescription() {
  if (!model_config_.has_value() || !morph_pad_view_ ||
      !morph_pad_view_->isVisible()) {
    return;
  }
  std::u8string str;

  str += u8"[注意 / Caution]";
  str += u8"\n";
  str +=
      u8"Voice Morphing Mode では、未選択の Voice の学習データが\n"
      u8"変換結果に影響を与えやすくなる可能性があります。\n"
      u8"意図せぬ声質の類似や権利侵害にご注意ください。\n";
  str +=
      u8"In Voice Morphing Mode, the training data of unselected Voices could "
      u8"be more prone to influencing the conversion results. Please be "
      u8"mindful of unintended similarities in timbre and possible rights "
      u8"infringement.\n";
  str += u8"\n";
  const auto voice_count = common::GetVoiceCount(*model_config_);
  const auto weights = voice_morph_state_.CalculateWeights();
  for (auto i = 0; i < voice_count; ++i) {
    if (weights[i] >= common::kVoiceMorphWeightThreshold) {
      str += model_config_->voices[i].name;
      str += u8"\n";
      str += model_config_->voices[i].description;
      str += u8"\n";
    }
  }
  SetVoiceDescriptionText(str);
}

}  // namespace beatrice::vst
