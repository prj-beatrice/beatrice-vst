// Copyright (c) 2024-2025 Project Beatrice and Contributors

// TODO(refactor)

#include "vst/editor.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "beatricelib/beatrice.h"
#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/utility/stringconvert.h"
#include "vst3sdk/public.sdk/source/vst/vstparameters.h"
#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.h"
#include "vst3sdk/vstgui4/vstgui/lib/ctabview.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/platformfactory.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

// Beatrice
#include "common/error.h"
#include "common/parameter_schema.h"
#include "common/processor_core_2.h"
#include "vst/controller.h"
#include "vst/controls.h"
#include "vst/parameter.h"

#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

namespace beatrice::vst {

using common::ParameterID;
using Steinberg::ViewRect;
using Steinberg::Vst::String128;
using Steinberg::Vst::StringListParameter;
using VSTGUI::CFontDesc;
using VSTGUI::CFrame;
using VSTGUI::COptionMenu;
using VSTGUI::CViewContainer;
using VSTGUI::getPlatformFactory;
using VSTGUI::kBoldFace;
using VSTGUI::kNormalFont;

namespace BitmapFilter = VSTGUI::BitmapFilter;

Editor::Editor(void* const controller)
    : VSTGUIEditor(controller),
      font_(new CFontDesc("Segoe UI", 14)),
      font_bold_(new CFontDesc("Segoe UI", 14, kBoldFace)),
      font_description_(new CFontDesc("Meiryo", 12)),
      font_version_(new CFontDesc("Segoe UI", 12)),
      tab_view_(),
      portrait_view_(),
      portrait_description_(),
      morphing_labels_(),
      morphing_weights_view_() {
  setRect(ViewRect(0, 0, kWindowWidth, kWindowHeight));
}

Editor::~Editor() {
  font_->forget();
  font_bold_->forget();
  font_description_->forget();
  font_version_->forget();
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

  // 背景を設定
  frame->setBackgroundColor(kDarkColorScheme.background);

  // ヘッダーを作る
  auto* const header =
      new CViewContainer(CRect(0, 0, kWindowWidth, kHeaderHeight));
  header->setBackgroundColor(kDarkColorScheme.surface_0);
  frame->addView(header);

  // ロゴを表示する
  auto* const logo_view = new CView(CRect(0, 0, 132, 44).offset(34, 7));
  auto* const logo_bmp = new CBitmap("logo.png");
  logo_view->setBackground(logo_bmp);
  header->addView(logo_view);
  logo_bmp->forget();

  // バージョンを表示する
  auto* const version_label = new CTextLabel(
      CRect(0, 0, 200, kHeaderHeight).offset(kWindowWidth - 200 - 17, 0),
      (UTF8String("Ver. ") + FULL_VERSION_STR).data(), nullptr,
      CParamDisplay::kNoFrame);
  version_label->setBackColor(kTransparentCColor);
  version_label->setFont(font_version_);
  version_label->setFontColor(kDarkColorScheme.on_surface);
  version_label->setHoriAlign(CHoriTxtAlign::kRightText);
  header->addView(version_label);

  // フッターを作る
  auto* const footer = new CViewContainer(
      CRect(0, kWindowHeight - kFooterHeight, kWindowWidth, kWindowHeight));
  footer->setBackgroundColor(kDarkColorScheme.surface_0);
  frame->addView(footer);

  auto context = Context();  // オフセット設定
  BeginColumn(context, kColumnWidth, kDarkColorScheme.surface_1);
  BeginGroup(context, u8"General");
  MakeSlider(context, static_cast<ParamID>(ParameterID::kInputGain), 1, 1.0f,
             0.1f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kOutputGain), 1, 1.0f,
             0.1f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kAverageSourcePitch), 2,
             1.0f, 0.125f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kMinSourcePitch), 2,
             1.0f, 0.125f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kMaxSourcePitch), 2,
             1.0f, 0.125f);
  EndGroup(context);
  BeginGroup(context, u8"Pitch Shift");
  MakeSlider(context, static_cast<ParamID>(ParameterID::kPitchShift), 2, 1.0f,
             0.125f);
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kLock),
               kTransparentCColor, kDarkColorScheme.on_surface);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kIntonationIntensity),
             1, 0.5f, 0.1f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kPitchCorrection), 1,
             0.5f, 0.1f);
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kPitchCorrectionType),
               kTransparentCColor, kDarkColorScheme.on_surface);
  EndGroup(context);
  EndColumn(context);

  BeginColumn(context, kColumnWidth, kDarkColorScheme.surface_2);
  BeginGroup(context, u8"Model");
  MakeFileSelector(context, static_cast<ParamID>(ParameterID::kModel));
  MakeCombobox(context, static_cast<ParamID>(ParameterID::kVoice),
               kDarkColorScheme.primary, kDarkColorScheme.on_primary);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kFormantShift), 2, 1.0f,
             0.5f);
  MakeSlider(context, static_cast<ParamID>(ParameterID::kVQNumNeighbors), 0,
             1.0f, 1.0f);
  MakeModelVoiceDescription(context);
  EndGroup(context);
  EndColumn(context);

  BeginTabColumn(context, kPortraitColumnWidth, kDarkColorScheme.surface_3);
  MakePortraitView(context);
  MakePortraitDescription(context);
  EndTabColumn(context);

  BeginTabColumn(context, kPortraitColumnWidth, kDarkColorScheme.surface_3);
  BeginGroup(context, u8"Voice Morphing Weights");
  MakeVoiceMorphingView(context);
  EndGroup(context);
  EndTabColumn(context);

  // 1 要素 1 クラスの方が良かったのか？？？

  if (!frame->open(parent)) {
    return false;
  }

  // frame->open で Attach された後でないと
  // テキストに合わせてテキストボックスの位置が変わらない
  SyncModelDescription();

  SyncSourcePitchRange();
  SyncParameterAvailability();

  return true;
}

void PLUGIN_API Editor::close() {
  if (frame) {
    tab_view_->removeAllTabs();
    frame->forget();
    frame = nullptr;
    model_voice_description_ = nullptr;
    portraits_.clear();
    tab_view_ = nullptr;
    portrait_view_ = nullptr;
    portrait_description_ = nullptr;
    morphing_weights_view_ = nullptr;
  }
}

// DAW 側から GUI にパラメータ変更を伝える。
// Voice など、controller と editor で最大値が異なるパラメータがあるため、
// controller->setValueNormalized は使わない。
// valueChanged からも controller を介して間接的に呼ばれる。
// 引数じゃなくて core から値を取った方が良い？
void Editor::SyncValue(const ParamID param_id, const float plain_value) {
  if (!frame || !controls_.contains(param_id)) {
    return;
  }
  auto* const control = controls_.at(param_id);
  const auto vst_param_id = static_cast<int>(param_id);

  // Voice は色々ややこしいので特別扱いする
  if (param_id == static_cast<ParamID>(ParameterID::kVoice)) {
    const auto voice_id = static_cast<int>(std::round(plain_value));
    control->setValue(plain_value);
    if (!model_config_.has_value()) {
      portrait_view_->setBackground(nullptr);
      portrait_description_->setText("");
      tab_view_->selectTab(0);
    } else if (voice_id == 0 ||
               voice_id < static_cast<int>(control->getMax())) {
      portrait_view_->setBackground(
          portraits_.at(model_config_->voices[voice_id].portrait.path).get());
      portrait_description_->setText(reinterpret_cast<const char*>(
          model_config_->voices[voice_id].portrait.description.c_str()));
      model_voice_description_->SetVoiceDescription(
          model_config_->voices[voice_id].description);
      tab_view_->selectTab(0);
    } else {
      portrait_view_->setBackground(nullptr);
      portrait_description_->setText("");
      // model_voice_description_->SetVoiceDescription(
      //    u8"< Voice Morphing Mode >");
      SyncVoiceMorphingDescription();
      SyncVoiceMorphingSliders();
      tab_view_->selectTab(1);
    }
  } else if (vst_param_id >=
                 static_cast<int>(ParameterID::kVoiceMorphWeights) &&
             vst_param_id < static_cast<int>(ParameterID::kVoiceMorphWeights) +
                                common::kMaxNSpeakers) {
    auto* const voice_control =
        controls_.at(static_cast<int>(ParameterID::kVoice));
    const auto voice_id = voice_control->getValue();
    if (voice_id > 0 && voice_id == static_cast<int>(voice_control->getMax())) {
      SyncVoiceMorphingDescription();
    }
    control->setValue(plain_value);
    SyncVoiceMorphingSliders();
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
  auto* const model_selector = static_cast<FileSelector*>(
      controls_.at(static_cast<ParamID>(ParameterID::kModel)));
  auto* const voice_combobox = static_cast<COptionMenu*>(
      controls_.at(static_cast<ParamID>(ParameterID::kVoice)));
  const auto file = model_selector->GetPath();
  model_selector->setText("<unloaded>");
  voice_combobox->removeAllEntry();
  model_voice_description_->SetModelDescription(u8"");
  model_voice_description_->SetVoiceDescription(u8"");
  model_config_ = std::nullopt;
  portraits_.clear();
  if (file.empty()) {
    // 初期状態
    return;
  } else if (!std::filesystem::exists(file) ||
             !std::filesystem::is_regular_file(file)) {
    // ファイルが移動して読み込めない場合の分岐だが、
    // モデルを読み込んだ後に GUI を閉じモデルファイルを移動して
    // 再び GUI を開いた場合などには
    // Processor のみ読み込まれている可能性がある。
    model_selector->setText("<failed to load>");
    model_voice_description_->SetModelDescription(
        u8"Error: The model could not be loaded due to a file move or another "
        u8"issue. Please reload a valid model.");
    return;
  }
  try {
    const auto toml_data = toml::parse(file);
    model_config_ = toml::get<common::ModelConfig>(toml_data);
    if (model_config_->model.VersionInt() == -1) {
      model_voice_description_->SetModelDescription(
          u8"Error: Unknown model version.");
      return;
    }
    model_selector->setText(
        reinterpret_cast<const char*>(model_config_->model.name.c_str()));
    // 話者のリストを読み込む。
    // また、予め portrait を読み込んで、必要に応じてリサイズしておく。
    int voice_counter = 0;
    for (const auto& voice : model_config_->voices) {
      if (voice.name.empty() && voice.description.empty() &&
          voice.portrait.path.empty() && voice.portrait.description.empty()) {
        break;
      }
      ++voice_counter;
      voice_combobox->addEntry(
          reinterpret_cast<const char*>(voice.name.c_str()));

      // portrait
      {
        if (portraits_.contains(voice.portrait.path)) {
          goto load_portrait_succeeded;
        }
        const auto portrait_file = file.parent_path() / voice.portrait.path;
        if (!std::filesystem::exists(portrait_file) ||
            !std::filesystem::is_regular_file(portrait_file)) {
          goto load_portrait_failed;
        }
        const auto platform_bitmap = getPlatformFactory().createBitmapFromPath(
            reinterpret_cast<const char*>(portrait_file.u8string().c_str()));
        if (!platform_bitmap) {
          goto load_portrait_failed;
        }
        const auto original_bitmap =
            VSTGUI::owned(new CBitmap(platform_bitmap));
        const auto original_size = original_bitmap->getSize();
        if (original_size.x == kPortraitWidth &&
            original_size.y == kPortraitHeight) {
          portraits_.insert({voice.portrait.path, original_bitmap});
          goto load_portrait_succeeded;
        }
        const auto scale =
            VSTGUI::owned(BitmapFilter::Factory::getInstance().createFilter(
                BitmapFilter::Standard::kScaleBilinear));
        scale->setProperty(BitmapFilter::Standard::Property::kInputBitmap,
                           original_bitmap.get());
        scale->setProperty(BitmapFilter::Standard::Property::kOutputRect,
                           CRect(0, 0, kPortraitWidth, kPortraitHeight));
        if (!scale->run()) {
          goto load_portrait_failed;
        }
        auto* const scaled_bitmap_obj =
            scale->getProperty(BitmapFilter::Standard::Property::kOutputBitmap)
                .getObject();
        auto* const scaled_bitmap = dynamic_cast<CBitmap*>(scaled_bitmap_obj);
        if (!scaled_bitmap) {
          goto load_portrait_failed;
        }
        portraits_.insert({voice.portrait.path, VSTGUI::shared(scaled_bitmap)});
        goto load_portrait_succeeded;
      }
      assert(false);
    load_portrait_failed:
      portraits_.insert({voice.portrait.path, nullptr});
    load_portrait_succeeded: {}
    }

    if (voice_counter > 1) {
      const auto flags = model_config_->model.VersionInt() <= 2
                             ? VSTGUI::CMenuItem::kNoFlags
                             : VSTGUI::CMenuItem::kDisabled;
      voice_combobox->addEntry("Voice Morphing Mode", -1, flags);
      portraits_.insert({u8"", nullptr});
    }

    voice_combobox->setDirty();
    for (auto i = 0; i < common::kMaxNSpeakers; ++i) {
      auto* const slider =
          static_cast<Slider*>(controls_.at(static_cast<ParamID>(
              static_cast<int>(ParameterID::kVoiceMorphWeights) + i)));
      auto* const label = morphing_labels_[i];
      if (i < voice_counter) {
        slider->setVisible(true);
        slider->SetEnabled(true);
        label->setVisible(true);
        label->setText(reinterpret_cast<const char*>(
            model_config_->voices[i].name.c_str()));
      } else {
        slider->setVisible(false);
        slider->SetEnabled(false);
        label->setVisible(false);
        label->setText("");
      }
      slider->setDirty();
      label->setDirty();
    }
    auto container_size = morphing_weights_view_->getContainerSize();
    container_size.setHeight(voice_counter *
                             (kElementHeight + kElementMerginY));
    morphing_weights_view_->setContainerSize(container_size);

    const auto voice_id =
        Denormalize(std::get<common::ListParameter>(
                        common::kSchema.GetParameter(ParameterID::kVoice)),
                    controller->getParamNormalized(
                        static_cast<ParamID>(ParameterID::kVoice)));
    if (voice_id < voice_counter) {
      const auto& voice = model_config_->voices[voice_id];
      portrait_view_->setBackground(portraits_.at(voice.portrait.path).get());
      portrait_description_->setText(
          reinterpret_cast<const char*>(voice.portrait.description.c_str()));
      model_voice_description_->SetVoiceDescription(voice.description);
      tab_view_->selectTab(0);
    } else {
      portrait_view_->setBackground(nullptr);
      portrait_description_->setText("");
      // model_voice_description_->SetVoiceDescription(u8"< Voice Morphing Mode
      // >");
      SyncVoiceMorphingDescription();
      SyncVoiceMorphingSliders();
      tab_view_->selectTab(1);
    }
    model_voice_description_->SetModelDescription(
        model_config_->model.description);

    portrait_view_->setDirty();
    portrait_description_->setDirty();
    morphing_weights_view_->setDirty();

    if (auto* const column_view = model_voice_description_->getParentView()) {
      column_view->setDirty();
    }
  } catch (const std::exception& e) {
    model_selector->setText("<failed to load>");
    model_voice_description_->SetModelDescription(
        u8"Error:\n" +
        std::u8string(e.what(), e.what() + std::strlen(e.what())));
    return;
  }
}

// GUI でパラメータに変更があったときに、DAW に伝える。
// この中で量子化しているが、スライダーの 1 箇所をクリックし続けただけで
// ループしてしまう気がするので、できれば Slider の実装側で量子化したい。
// あとダブルクリックでデフォルトに戻したい。
void Editor::valueChanged(CControl* const pControl) {
  const auto vst_param_id = pControl->getTag();
  const auto param_id = static_cast<ParameterID>(vst_param_id);
  const auto& param = common::kSchema.GetParameter(param_id);
  auto* const controller = static_cast<Controller*>(getController());
  auto& core = controller->core_;
  const auto communicate = [&controller](const int param_id,
                                         const double normalized_value) {
    controller->setParamNormalized(param_id, normalized_value);
    controller->beginEdit(param_id);
    controller->performEdit(param_id, normalized_value);
    controller->endEdit(param_id);
  };
  // 各々の Control でやるべきという感じも
  if (auto* const control = dynamic_cast<Slider*>(pControl)) {
    // communicate 含めて controller の中に処理書いた方が明快？
    const auto* const num_param = std::get_if<common::NumberParameter>(&param);
    assert(num_param);
    const auto plain_value = control->getValue();
    if (plain_value ==
        std::get<double>(core.parameter_state_.GetValue(param_id))) {
      return;
    }
    const auto normalized_value =
        static_cast<float>(Normalize(*num_param, plain_value));
    const auto error_code = num_param->ControllerSetValue(core, plain_value);
    assert(error_code == common::ErrorCode::kSuccess);
    communicate(vst_param_id, normalized_value);

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
      model_voice_description_->SetVoiceDescription(
          u8"Error: Speaker ID out of range.");
    }
    assert(error_code == common::ErrorCode::kSuccess);
    communicate(vst_param_id, normalized_value);
  } else if (auto* const control = dynamic_cast<FileSelector*>(pControl)) {
    const auto* const str_param = std::get_if<common::StringParameter>(&param);
    assert(str_param);
    const auto file = control->GetPath().u8string();
    auto error_code = str_param->ControllerSetValue(core, file);
    if (error_code == common::ErrorCode::kFileOpenError ||
        error_code == common::ErrorCode::kTOMLSyntaxError) {
      // Controller とは別に Editor::SyncModelDescription でも改めて
      // ファイルを読み込もうとして失敗するので、ここではエラー処理しない
      error_code = common::ErrorCode::kSuccess;
    }
    assert(error_code == common::ErrorCode::kSuccess);
    error_code = controller->SetStringParameter(vst_param_id, file);
    assert(error_code == common::ErrorCode::kSuccess);
    // processor に通知
    auto* const msg = controller->allocateMessage();
    msg->setMessageID("param_change");
    msg->getAttributes()->setBinary("param_id", &vst_param_id,
                                    sizeof(vst_param_id));
    msg->getAttributes()->setBinary("data", file.c_str(), file.size());
    controller->sendMessage(msg);
    msg->release();
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
      communicate(vst_param_id, normalized_value);
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto normalized_value =
          Normalize(*list_param, std::get<int>(value));
      communicate(vst_param_id, normalized_value);
    } else if (std::get_if<common::StringParameter>(&param)) {
      // 現状何かに連動して StringParameter が変化することはない
      assert(false);
    } else {
      assert(false);
    }
  }
  core.updated_parameters_.clear();
}

// auto Editor::notify(CBaseObject* const sender,
//                            const char* const message) -> CMessageResult{
//     return VSTGUIEditor::notify(sender, message);
// }

// 以下は open() からのみ呼ばれるメンバ関数

void Editor::BeginColumn(Context& context, const int width,
                         const CColor& back_color) {
  context.column_width = width;
  context.column_back_color = back_color;
  context.column_start_y = context.y;
  context.column_start_x = context.x;
  context.y = 0;
  context.x = 0;
  context.last_element_mergin = kInnerColumnMerginY;
  context.x += kInnerColumnMerginX;
}

auto Editor::EndColumn(Context& context) -> CView* {
  // const auto bottom =
  //     context.y + std::max(context.last_element_mergin,
  //     kInnerColumnMerginY);
  const auto bottom = kWindowHeight - kFooterHeight;
  auto* const column = new CViewContainer(
      CRect(context.column_start_x, kHeaderHeight,
            context.column_start_x + context.column_width, bottom));
  column->setBackgroundColor(context.column_back_color);
  frame->addView(column);
  for (auto&& element : context.column_elements) {
    column->addView(element);
  }
  context.column_elements.clear();
  context.y = kHeaderHeight;
  context.x = context.column_start_x + context.column_width + kColumnMerginX;
  context.column_start_y = -1;
  context.column_start_x = -1;
  context.last_element_mergin = 0;
  context.first_group = true;
  return column;
}

void Editor::BeginTabColumn(Context& context, const int width,
                            const CColor& back_color) {
  context.column_width = width;
  context.column_back_color = back_color;
  context.column_start_y = context.y;
  context.column_start_x = context.x;
  context.y = 0;
  context.x = 0;
  context.last_element_mergin = kInnerColumnMerginY;
  context.x += kInnerColumnMerginX;
}

auto Editor::EndTabColumn(Context& context) -> CView* {
  auto size = CRect(context.column_start_x, kHeaderHeight,
                    context.column_start_x + context.column_width,
                    kWindowHeight - kFooterHeight);
  if (!tab_view_) {
    tab_view_ = new VSTGUI::CTabView(
        size,
        CRect(context.column_start_x, kHeaderHeight,
              context.column_start_x + context.column_width, kHeaderHeight));
    frame->addView(tab_view_);
  }
  auto* child_view = new CViewContainer(size);
  child_view->setBackgroundColor(context.column_back_color);
  for (auto&& element : context.column_elements) {
    child_view->addView(element);
  }
  tab_view_->addTab(child_view);
  context.column_elements.clear();
  context.y = kHeaderHeight;
  context.x = context.column_start_x;
  context.column_start_y = -1;
  context.column_start_x = -1;
  context.last_element_mergin = 0;
  context.first_group = true;
  return tab_view_;
}

auto Editor::BeginGroup(Context& context, const std::u8string& name) -> CView* {
  if (!context.first_group) {
    context.last_element_mergin = 20;  // 線を引くとかする？
  }
  context.first_group = false;
  context.y += std::max(context.last_element_mergin, kGroupLabelMerginY);
  auto* const group_label =
      new CTextLabel(CRect(0, 0, context.column_width, kElementHeight)
                         .offset(context.x, context.y),
                     reinterpret_cast<const char*>((u8"⚙ " + name).c_str()),
                     nullptr, CParamDisplay::kNoFrame);
  group_label->setBackColor(kTransparentCColor);
  group_label->setFont(font_bold_);
  group_label->setFontColor(kDarkColorScheme.on_surface);
  group_label->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(group_label);
  context.y += kElementHeight;
  context.x += kGroupIndentX;
  context.last_element_mergin = kGroupLabelMerginY;
  return group_label;
}

void Editor::EndGroup(Context& context) { context.x -= kGroupIndentX; }

// NumberParameter 用
auto Editor::MakeSlider(Context& context, const ParamID param_id,
                        const int precision, const float wheel_inc,
                        const float fine_wheel_inc) -> CView* {
  static constexpr auto kHandleWidth = 10;  // 透明の左右の淵を含む
  auto* const param =
      static_cast<LinearParameter*>(controller->getParameterObject(param_id));
  auto* const slider_bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  auto* const handle_bmp =
      new MonotoneBitmap(kHandleWidth, kElementHeight,
                         kDarkColorScheme.secondary_dim, kTransparentCColor);

  context.y += std::max(context.last_element_mergin, kElementMerginY);

  auto* const slider_control = new Slider(
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y),
      this, static_cast<int>(param_id), context.x,
      context.x + kElementWidth - kHandleWidth, handle_bmp, slider_bmp,
      VST3::StringConvert::convert(param->getInfo().units), font_, precision);
  slider_control->setMin(param->GetMinPlain());
  slider_control->setMax(param->GetMaxPlain());
  slider_control->setWheelInc(wheel_inc);
  slider_control->setFineWheelInc(fine_wheel_inc);
  slider_control->setDefaultValue(
      param->toPlain(param->getInfo().defaultNormalizedValue));
  slider_control->setValue(
      static_cast<float>(param->toPlain(param->getNormalized())));
  context.column_elements.push_back(slider_control);
  slider_bmp->forget();
  handle_bmp->forget();

  controls_.insert({param_id, slider_control});

  // 名前
  const auto title_pos =
      CRect(0, 0, kLabelWidth, kElementHeight)
          .offset(context.x + kElementWidth + kElementMerginX, context.y);
  const auto title_string =
      VST3::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return slider_control;
}

// ListParameter 用
auto Editor::MakeCombobox(
    Context& context, const ParamID param_id,
    const CColor& back_color = kTransparentCColor,
    const CColor& font_color = kDarkColorScheme.on_surface) -> CView* {
  auto* const param = static_cast<StringListParameter*>(
      controller->getParameterObject(param_id));
  const auto step_count = param->getInfo().stepCount;

  auto* const bmp = new MonotoneBitmap(kElementWidth, kElementHeight,
                                       back_color, kDarkColorScheme.outline);
  context.y += std::max(context.last_element_mergin, kElementMerginY);

  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y);
  auto* const control =
      new COptionMenu(pos, this, static_cast<int>(param_id), bmp);
  bmp->forget();
  for (auto i = 0; i <= step_count; ++i) {
    String128 tmp_string128;
    param->toString(param->toNormalized(i), tmp_string128);
    const auto name = VST3::StringConvert::convert(tmp_string128);
    control->addEntry(name.c_str());
  }
  control->setValue(static_cast<float>(
      param->toPlain(controller->getParamNormalized(param_id))));
  control->setFont(font_);
  control->setFontColor(font_color);
  context.column_elements.push_back(control);
  controls_.insert({param_id, control});

  // ▼ 記号
  const auto arrow_pos =
      CRect(0, 0, kElementHeight, kElementHeight)
          .offset(context.x + (kElementWidth - kElementHeight), context.y)
          .inset(8, 8);
  auto* const arrow_control =
      new CTextLabel(arrow_pos, reinterpret_cast<const char*>(u8"▼"), nullptr,
                     CParamDisplay::kNoFrame);
  arrow_control->setBackColor(kTransparentCColor);
  auto* const arrow_font =
      new CFontDesc(font_->getName(), font_->getSize() - 6);
  arrow_control->setFont(arrow_font);
  arrow_font->forget();
  arrow_control->setFontColor(font_color);
  arrow_control->setHoriAlign(CHoriTxtAlign::kCenterText);
  // クリックの判定吸われないように、▼ 記号へのマウス操作を無効にする
  arrow_control->setMouseEnabled(false);
  context.column_elements.push_back(arrow_control);

  // 名前
  const auto title_pos =
      CRect(0, 0, kLabelWidth, kElementHeight)
          .offset(context.x + kElementWidth + kElementMerginX, context.y);
  const auto title_string =
      VST3::StringConvert::convert(param->getInfo().title);
  auto* const title_control = new CTextLabel(title_pos, title_string.c_str(),
                                             nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return control;
}

// StringParameter 用
// クリックされる -> onMouseDown でダイアログが開いてパスを取得
// -> valueChanged から ControllerSetValue が呼ばれる
// -> valueChanged から processor にメッセージ (ファイル名) を送る
// -> notify で processor の mutex を取得、
//    この間 process は無音を出力し、パラメータ変更はキューに詰めとく
// -> モデルを読み込む
// -> valueChanged で連動した他のパラメータの変更が処理される
auto Editor::MakeFileSelector(Context& context, ParamID vst_param_id)
    -> CView* {
  const auto param_id = static_cast<ParameterID>(vst_param_id);
  const auto param =
      std::get<common::StringParameter>(common::kSchema.GetParameter(param_id));
  auto* const bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  context.y += std::max(context.last_element_mergin, kElementMerginY);

  const auto pos =
      CRect(0, 0, kElementWidth, kElementHeight).offset(context.x, context.y);
  auto* const control =
      new FileSelector(pos, this, static_cast<int>(vst_param_id), bmp);
  bmp->forget();
  auto* const controller = static_cast<Controller*>(getController());
  control->setBackColor(kTransparentCColor);
  control->setFont(font_);
  control->setFontColor(kDarkColorScheme.on_surface);
  control->setHoriAlign(CHoriTxtAlign::kCenterText);
  control->SetPath(*std::get<std::unique_ptr<std::u8string>>(
      controller->core_.parameter_state_.GetValue(param_id)));
  context.column_elements.push_back(control);
  controls_.insert({vst_param_id, control});

  // 名前
  const auto title_pos =
      CRect(0, 0, kLabelWidth, kElementHeight)
          .offset(context.x + kElementWidth + kElementMerginX, context.y);
  auto* const title_control = new CTextLabel(
      title_pos, reinterpret_cast<const char*>(param.GetName().c_str()),
      nullptr, CParamDisplay::kNoFrame);
  title_control->setBackColor(kTransparentCColor);
  title_control->setFont(font_);
  title_control->setFontColor(kDarkColorScheme.on_surface);
  title_control->setHoriAlign(CHoriTxtAlign::kLeftText);
  context.column_elements.push_back(title_control);

  context.y += kElementHeight;
  context.last_element_mergin = kElementMerginY;

  return control;
}

auto Editor::MakePortraitView(Context& context) -> CView* {
  portrait_view_ = new CView(CRect(0, 0, kPortraitWidth, kPortraitHeight));
  context.column_elements.push_back(portrait_view_);
  context.y += kPortraitHeight;
  context.last_element_mergin = kElementMerginY;
  return portrait_view_;
}

auto Editor::MakeModelVoiceDescription(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, 24);
  const auto offset_x = context.x;

  model_voice_description_ = new ModelVoiceDescription(
      CRect(context.x, context.y, context.column_width - offset_x,
            kWindowHeight - kFooterHeight - kHeaderHeight),
      font_description_, kElementHeight, kElementMerginY + 4);

  context.column_elements.push_back(model_voice_description_);

  return nullptr;
}

auto Editor::MakePortraitDescription(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, 24);
  const auto offset_x = context.x;
  auto* const description = new CMultiLineTextLabel(
      CRect(context.x, context.y, context.column_width - offset_x,
            kWindowHeight - kFooterHeight));
  description->setFont(font_description_);
  description->setFontColor(kDarkColorScheme.on_surface);
  description->setBackColor(kTransparentCColor);
  description->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
  description->setStyle(CParamDisplay::kNoFrame);
  description->setHoriAlign(CHoriTxtAlign::kLeftText);
  portrait_description_ = description;
  context.column_elements.push_back(portrait_description_);

  context.y = kWindowHeight - kFooterHeight;
  context.last_element_mergin = kElementMerginY;
  return description;
}

auto Editor::MakeVoiceMorphingView(Context& context) -> CView* {
  context.y += std::max(context.last_element_mergin, kElementMerginY);

  const auto size =
      CRect(context.x, context.y, context.column_width - context.x,
            kWindowHeight - kFooterHeight - kHeaderHeight);
  const auto container_size =
      CRect(0, 0, size.getWidth(),
            (kElementHeight + kElementMerginY) * common::kMaxNSpeakers);
  morphing_weights_view_ =
      new VSTGUI::CScrollView(size, container_size,
                              VSTGUI::CScrollView::kVerticalScrollbar |
                                  VSTGUI::CScrollView::kDontDrawFrame |
                                  VSTGUI::CScrollView::kOverlayScrollbars);
  // morphing_weights_view_->setAutosizeFlags( VSTGUI::kAutosizeRow |
  // VSTGUI::kAutosizeBottom );
  morphing_weights_view_->setBackgroundColor(kTransparentCColor);
  auto scroll_bar = morphing_weights_view_->getVerticalScrollbar();
  scroll_bar->setFrameColor(kDarkColorScheme.outline);
  scroll_bar->setScrollerColor(kDarkColorScheme.secondary_dim);
  scroll_bar->setBackgroundColor(kTransparentCColor);

  static constexpr auto kHandleWidth = 10;  // 透明の左右の淵を含む
  auto* const slider_bmp =
      new MonotoneBitmap(kElementWidth, kElementHeight, kTransparentCColor,
                         kDarkColorScheme.outline);
  auto* const handle_bmp =
      new MonotoneBitmap(kHandleWidth, kElementHeight,
                         kDarkColorScheme.secondary_dim, kTransparentCColor);

  const auto label_width = morphing_weights_view_->getWidth() - kElementWidth -
                           kElementMerginX -
                           morphing_weights_view_->getScrollbarWidth();
  for (auto i = 0; i < common::kMaxNSpeakers; ++i) {
    const auto label_pos = CRect(0, 0, label_width, kElementHeight)
                               .offset(kElementWidth + kElementMerginX,
                                       i * (kElementHeight + kElementMerginY));
    auto* const label_control =
        new CTextLabel(label_pos, "", nullptr, CParamDisplay::kNoFrame);
    label_control->setBackColor(kTransparentCColor);
    label_control->setFont(font_);
    label_control->setFontColor(kDarkColorScheme.on_surface);
    label_control->setHoriAlign(CHoriTxtAlign::kLeftText);
    label_control->setVisible(false);

    morphing_weights_view_->addView(label_control);
    morphing_labels_[i] = label_control;
  }
  for (auto i = 0; i < common::kMaxNSpeakers; ++i) {
    auto const vst_param_id =
        static_cast<int>(ParameterID::kVoiceMorphWeights) + i;
    auto const param_id = static_cast<ParamID>(vst_param_id);
    auto* const param =
        static_cast<LinearParameter*>(controller->getParameterObject(param_id));
    auto* const slider_control = new Slider(
        CRect(0, 0, kElementWidth, kElementHeight)
            .offset(0, i * (kElementHeight + kElementMerginY)),
        this, static_cast<int>(param_id), 0, kElementWidth - kHandleWidth,
        handle_bmp, slider_bmp,
        VST3::StringConvert::convert(param->getInfo().units), font_, 2);
    slider_control->setValue(
        static_cast<float>(param->toPlain(param->getNormalized())));
    slider_control->setVisible(false);

    morphing_weights_view_->addView(slider_control);
    controls_.insert({vst_param_id, slider_control});
  }

  slider_bmp->forget();
  handle_bmp->forget();

  context.column_elements.push_back(morphing_weights_view_);
  return morphing_weights_view_;
}

void Editor::SyncVoiceMorphingDescription() {
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

  for (auto i = 0; i < common::kMaxNSpeakers; ++i) {
    if (morphing_labels_[i]->isVisible()) {
      auto control =
          controls_.at(static_cast<int>(ParameterID::kVoiceMorphWeights) + i);
      if (control->getValue() >= (0.01f - FLT_EPSILON)) {
        str += model_config_->voices[i].name;
        str += u8"\n";
        str += model_config_->voices[i].description;
        str += u8"\n";
      }
    } else {
      break;
    }
  }
  model_voice_description_->SetVoiceDescription(str);
}

void Editor::SyncVoiceMorphingSliders() {
  if (model_config_->model.VersionInt() >= 2) {
    int non_zero_count = 0;
    auto f_set_zero = [this](Slider* slider) {
      slider->setValue(0.0f);
      slider->setDirty();
      valueChanged(slider);
    };

    for (int i = 0; i < common::kMaxNSpeakers; ++i) {
      auto* const slider =
          static_cast<Slider*>(controls_.at(static_cast<ParamID>(
              static_cast<int>(ParameterID::kVoiceMorphWeights) + i)));
      if (!slider->IsEnabled()) {
        // Disable にされてるスライダーを DAW 側から
        // コントロールされた場合のケア
        f_set_zero(slider);
      }

      // UIをゆっくり動かしたときなどにたまに0に見えて微小な値を持っているような
      // 挙動が見られたため、その場合のケア
      // 閾値はスライダーの段階依存なので、スライダーの段階に応じた適切な値を設定する
      if (slider->getValue() >= (0.01f - FLT_EPSILON)) {
        ++non_zero_count;
      } else {
        f_set_zero(slider);
      }
    }

    if (non_zero_count < common::ProcessorCore2::kSphAvgMaxNSpeakers) {
      // 非ゼロの重みの数が上限を下回っていた場合はすべてのスライダーを有効化する
      for (int i = 0; i < common::kMaxNSpeakers; ++i) {
        auto* const slider =
            static_cast<Slider*>(controls_.at(static_cast<ParamID>(
                static_cast<int>(ParameterID::kVoiceMorphWeights) + i)));
        slider->SetEnabled(true);
      }
    } else {
      // 非ゼロの重みの数が上限に達していた場合、重みゼロのスライダーのみ無効化して
      // それ以上非ゼロの重みが増えないようにする
      // また、上限を超えていた分については、それ以降の重みを強制的にゼロにする
      int counter = 0;
      for (int i = 0; i < common::kMaxNSpeakers; ++i) {
        auto* const slider =
            static_cast<Slider*>(controls_.at(static_cast<ParamID>(
                static_cast<int>(ParameterID::kVoiceMorphWeights) + i)));
        if (slider->getValue() >= (0.01f - FLT_EPSILON)) {
          if (counter++ < common::ProcessorCore2::kSphAvgMaxNSpeakers) {
            slider->SetEnabled(true);
          } else {
            f_set_zero(slider);
            slider->SetEnabled(false);
          }
        } else {
          slider->SetEnabled(false);
        }
      }
    }
  }
}

}  // namespace beatrice::vst
