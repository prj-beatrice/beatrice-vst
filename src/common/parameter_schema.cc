// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "common/parameter_schema.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "toml11/single_include/toml.hpp"

// Beatrice
#include "common/controller_core.h"
#include "common/error.h"
#include "common/model_config.h"
#include "common/processor_core.h"
#include "common/processor_proxy.h"
#include "common/voice_morph_parameter.h"
#include "common/voice_morph_state.h"

namespace beatrice::common {

using namespace std::string_literals;

static constexpr auto kMaxAbsPitchShift = 24.0;

namespace {

auto SetVoiceMorphParameterOnController(ControllerCore&, double) -> ErrorCode {
  return ErrorCode::kSuccess;
}

auto SetVoiceMorphParameterOnProcessor(ProcessorProxy& processor, double)
    -> ErrorCode {
  return processor.GetCore()->SetSpeakerMorphingWeights(
      GetVoiceMorphState(processor.GetParameterState()).CalculateWeights());
}

}  // namespace

// パラメータの追加には以下 3 箇所の変更が必要
// * parameter_schema.h, parameter_schema.cc (メタデータの設定)
// * processor_core.h, processor_core_*.cc   (信号処理)
// * editor.cc                               (GUI)

// パラメータ ID に対して、そのパラメータはどのような名前で、
// どのような値域を持ち、どのような操作に対応するのかを保持する。
const ParameterSchema kSchema = [] {
  constexpr auto kDefaultVoiceMorphState = VoiceMorphState{};
  auto schema = ParameterSchema({
      {ParameterID::kModel,
       StringParameter(
           u8"Model"s, u8""s, false,
           [](ControllerCore& controller, const std::u8string& value) {
             if (value.empty()) {
               return ErrorCode::kSuccess;
             }
             ModelConfig model_config;
             try {
               const auto toml_data = toml::parse(std::filesystem::path(value));
               model_config = toml::get<ModelConfig>(toml_data);
             } catch (const toml::file_io_error&) {
               return ErrorCode::kFileOpenError;
             } catch (const toml::syntax_error&) {
               return ErrorCode::kTOMLSyntaxError;
             } catch (const toml::type_error&) {
               return ErrorCode::kInvalidModelConfig;
             } catch (const std::invalid_argument&) {
               return ErrorCode::kInvalidModelConfig;
             } catch (const std::out_of_range&) {
               return ErrorCode::kInvalidModelConfig;
             } catch (const std::exception&) {
               return ErrorCode::kUnknownError;
             }
             if (model_config.model.VersionInt() < 0) {
               return ErrorCode::kInvalidModelConfig;
             }

             // Voice
             controller.parameter_state_.SetValue(ParameterID::kVoice, 0);
             controller.updated_parameters_.push_back(ParameterID::kVoice);
             // FormantShift
             controller.parameter_state_.SetValue(ParameterID::kFormantShift,
                                                  0.0);
             controller.updated_parameters_.push_back(
                 ParameterID::kFormantShift);

             // AverageTargetPitches
             for (auto i = 0; i < kMaxNSpeakers; ++i) {
               controller.parameter_state_.SetValue(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                       i),
                   model_config.voices[i].average_pitch);
               controller.updated_parameters_.push_back(
                   static_cast<ParameterID>(
                       static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                       i));
             }

             // Voice Morph の AverageTargetPitch を計算
             // 今のところは各 Voice の値の単純平均を採用することとする
             const auto voice_count = GetVoiceCount(model_config);
             double morphed_average_pitch = 0;
             for (auto i = 0; i < voice_count; ++i) {
               morphed_average_pitch += model_config.voices[i].average_pitch;
             }
             morphed_average_pitch /= voice_count;
             controller.parameter_state_.SetValue(
                 static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     voice_count),
                 morphed_average_pitch);
             controller.updated_parameters_.push_back(static_cast<ParameterID>(
                 static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                 voice_count));

             // Voice Morph
             auto voice_morph_state = VoiceMorphState();
             voice_morph_state.marker_count =
                 std::min(voice_count, kDefaultNVoiceMorphMarkers);
             for (const auto [param_id, param_value] :
                  GetVoiceMorphParameterValues(voice_morph_state)) {
               controller.parameter_state_.SetValue(param_id, param_value);
               controller.updated_parameters_.push_back(param_id);
             }

             const auto average_target_pitch =
                 model_config.voices[0].average_pitch;
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift =
                     std::clamp(average_target_pitch - average_source_pitch,
                                -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {  // PitchShift 固定
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const std::u8string& value) {
             return vc.LoadModel(value);
           })},
      {ParameterID::kVoice,
       ListParameter(
           u8"Voice"s,
           [] {
             auto v = std::vector<std::u8string>();
             for (auto i = 0; i < kMaxNSpeakers; ++i) {
               const auto i_ascii = std::to_string(i);
               const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
               v.push_back(u8"ID "s + i_u8);
             }
             return v;
           }(),
           0, u8"Voi"s, parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const int value) {
             if (value < 0 || value > kMaxNSpeakers) {
               return ErrorCode::kSpeakerIDOutOfRange;
             }
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     value)));
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift =
                     std::clamp(average_target_pitch + formant_shift -
                                    average_source_pitch,
                                -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {  // PitchShift 固定
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch + formant_shift - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const int value) {
             return vc.GetCore()->SetTargetSpeaker(value);
           })},
      {ParameterID::kFormantShift,
       NumberParameter(
           u8"Formant Shift"s, 0.0, -2.0, 2.0, u8"st"s, 8, u8"For"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const double value) {
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             switch (std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kLock))) {
               case 0: {  // AverageSourcePitch 固定
                 // ピッチシフト量を変更する
                 const auto average_source_pitch =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kAverageSourcePitch));
                 const auto shift = std::clamp(
                     average_target_pitch + value - average_source_pitch,
                     -kMaxAbsPitchShift, kMaxAbsPitchShift);
                 controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                      shift);
                 controller.updated_parameters_.push_back(
                     ParameterID::kPitchShift);
               } break;
               case 1: {
                 // AverageSourcePitch を変更する
                 const auto pitch_shift =
                     std::get<double>(controller.parameter_state_.GetValue(
                         ParameterID::kPitchShift));
                 // clamp するべき？
                 const auto average_source_pitch =
                     average_target_pitch + value - pitch_shift;
                 controller.parameter_state_.SetValue(
                     ParameterID::kAverageSourcePitch, average_source_pitch);
                 controller.updated_parameters_.push_back(
                     ParameterID::kAverageSourcePitch);
                 break;
               }
             }
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetFormantShift(value);
           })},
      {ParameterID::kPitchShift,
       NumberParameter(
           u8"Pitch Shift"s, 0.0, -kMaxAbsPitchShift, kMaxAbsPitchShift,
           u8"st"s, 48 * 8, u8"Pit"s, parameter_flag::kCanAutomate,
           [](ControllerCore& controller, const double value) {
             // AverageSourcePitch を変更する
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             // clamp するべき？
             const auto average_source_pitch =
                 average_target_pitch + formant_shift - value;
             controller.parameter_state_.SetValue(
                 ParameterID::kAverageSourcePitch, average_source_pitch);
             controller.updated_parameters_.push_back(
                 ParameterID::kAverageSourcePitch);
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetPitchShift(value);
           })},
      {ParameterID::kAverageSourcePitch,
       NumberParameter(
           u8"Average Source Pitch"s, 52.0, 0.0, 128.0, u8""s, 128 * 8,
           u8"SrcPit"s, parameter_flag::kNoFlags,
           [](ControllerCore& controller, const double value) {
             // PitchShift を変更する
             const auto target_speaker = std::get<int>(
                 controller.parameter_state_.GetValue(ParameterID::kVoice));
             const auto formant_shift =
                 std::get<double>(controller.parameter_state_.GetValue(
                     ParameterID::kFormantShift));
             const auto average_target_pitch = std::get<double>(
                 controller.parameter_state_.GetValue(static_cast<ParameterID>(
                     static_cast<int>(ParameterID::kAverageTargetPitchBase) +
                     target_speaker)));
             const auto pitch_shift =
                 std::clamp(average_target_pitch + formant_shift - value,
                            -kMaxAbsPitchShift, kMaxAbsPitchShift);
             controller.parameter_state_.SetValue(ParameterID::kPitchShift,
                                                  pitch_shift);
             controller.updated_parameters_.push_back(ParameterID::kPitchShift);
             return ErrorCode::kSuccess;
           },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetAverageSourcePitch(value);
           })},
      {ParameterID::kLock,
       ListParameter(
           u8"Lock"s, {u8"Average Source Pitch"s, u8"Pitch Shift"s}, 0,
           u8"Loc"s, parameter_flag::kIsList,
           [](ControllerCore&, int) { return ErrorCode::kSuccess; },
           [](ProcessorProxy&, int) { return ErrorCode::kSuccess; })},
      {ParameterID::kInputGain,
       NumberParameter(
           u8"Input Gain"s, 0.0, -60.0, 20.0, u8"dB"s, 0, u8"Gain/In"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetInputGain(value);
           })},
      {ParameterID::kOutputGain,
       NumberParameter(
           u8"Output Gain"s, 0.0, -60.0, 20.0, u8"dB"s, 0, u8"Gain/Out"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetOutputGain(value);
           })},
      {ParameterID::kIntonationIntensity,
       NumberParameter(
           u8"Intonation Intensity"s, 1.0, -1.0, 3.0, u8""s, 40, u8"Inton"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetIntonationIntensity(value);
           })},
      {ParameterID::kPitchCorrection,
       NumberParameter(
           u8"Pitch Correction"s, 0.0, 0.0, 1.0, u8""s, 10, u8"PitCor"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetPitchCorrection(value);
           })},
      {ParameterID::kPitchCorrectionType,
       ListParameter(
           u8"Pitch Correction Type"s, {u8"Hard 0"s, u8"Hard 1"s}, 0,
           u8"CorTyp"s, parameter_flag::kCanAutomate,
           [](ControllerCore&, int) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const int value) {
             return vc.GetCore()->SetPitchCorrectionType(value);
           })},
      {ParameterID::kMinSourcePitch,
       NumberParameter(
           u8"Min Source Pitch"s, 33.125, 0.0, 128.0, u8""s, 128 * 8,
           u8"MinPit"s, parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetMinSourcePitch(value);
           })},
      {ParameterID::kMaxSourcePitch,
       NumberParameter(
           u8"Max Source Pitch"s, 80.875, 0.0, 128.0, u8""s, 128 * 8,
           u8"MaxPit"s, parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetMaxSourcePitch(value);
           })},
      {ParameterID::kVQNumNeighbors,
       NumberParameter(
           u8"VQ Neighbor Count"s, 0.0, 0.0, 8.0, u8""s, 8, u8"VQNbr"s,
           parameter_flag::kCanAutomate,
           [](ControllerCore&, double) { return ErrorCode::kSuccess; },
           [](ProcessorProxy& vc, const double value) {
             return vc.GetCore()->SetVQNumNeighbors(
                 static_cast<int>(std::round(value)));
           })},
      {ParameterID::kVoiceMorphCursorX,
       NumberParameter(u8"Morph Cursor X"s, kDefaultVoiceMorphState.cursor_x,
                       0.0, 1.0, u8""s, 1000, u8"MrphCX"s,
                       parameter_flag::kCanAutomate,
                       SetVoiceMorphParameterOnController,
                       SetVoiceMorphParameterOnProcessor)},
      {ParameterID::kVoiceMorphCursorY,
       NumberParameter(u8"Morph Cursor Y"s, kDefaultVoiceMorphState.cursor_y,
                       0.0, 1.0, u8""s, 1000, u8"MrphCY"s,
                       parameter_flag::kCanAutomate,
                       SetVoiceMorphParameterOnController,
                       SetVoiceMorphParameterOnProcessor)},
      {ParameterID::kVoiceMorphFalloff,
       NumberParameter(u8"Morph Falloff"s, kDefaultVoiceMorphState.falloff,
                       kVoiceMorphFalloffMin, kVoiceMorphFalloffMax, u8""s,
                       kVoiceMorphFalloffDivisions, u8"MrphFo"s,
                       parameter_flag::kCanAutomate,
                       SetVoiceMorphParameterOnController,
                       SetVoiceMorphParameterOnProcessor)},
      {ParameterID::kVoiceMorphMarkerCount,
       NumberParameter(
           u8"Morph Marker Count"s, kDefaultVoiceMorphState.marker_count, 1.0,
           kMaxNVoiceMorphMarkers, u8""s, kMaxNVoiceMorphMarkers - 1,
           u8"MrphCt"s, parameter_flag::kCanAutomate,
           SetVoiceMorphParameterOnController,
           SetVoiceMorphParameterOnProcessor)},
  });

  for (auto i = 0; i < kMaxNVoiceMorphMarkers; ++i) {
    const auto& default_marker = kDefaultVoiceMorphState.markers[i];
    const auto i_ascii = std::to_string(i);
    const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kVoiceMorphMarkerVoiceBase) + i),
        NumberParameter(
            u8"Morph Marker "s + i_u8 + u8" Voice"s, default_marker.voice_id,
            0.0, static_cast<double>(kMaxNSpeakers - 1), u8""s,
            kMaxNSpeakers - 1, u8"MrphV"s, parameter_flag::kCanAutomate,
            SetVoiceMorphParameterOnController,
            SetVoiceMorphParameterOnProcessor));
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kVoiceMorphMarkerXBase) + i),
        NumberParameter(u8"Morph Marker "s + i_u8 + u8" X"s, default_marker.x,
                        0.0, 1.0, u8""s, 1000, u8"MrphX"s,
                        parameter_flag::kCanAutomate,
                        SetVoiceMorphParameterOnController,
                        SetVoiceMorphParameterOnProcessor));
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kVoiceMorphMarkerYBase) + i),
        NumberParameter(u8"Morph Marker "s + i_u8 + u8" Y"s, default_marker.y,
                        0.0, 1.0, u8""s, 1000, u8"MrphY"s,
                        parameter_flag::kCanAutomate,
                        SetVoiceMorphParameterOnController,
                        SetVoiceMorphParameterOnProcessor));
  }

  for (auto i = 0; i < kMaxNSpeakers + 1;
       ++i) {  // Voice Morphing Mode の分も格納するため、要素数は (
               // kMaxNSpeakers + 1 ) となる
    const auto i_ascii = std::to_string(i);
    const auto i_u8 = std::u8string(i_ascii.begin(), i_ascii.end());
    schema.AddParameter(
        static_cast<ParameterID>(
            static_cast<int>(ParameterID::kAverageTargetPitchBase) + i),
        NumberParameter(
            u8"Speaker "s + i_u8, 60.0, 0.0, 128.0, u8""s, 128 * 8, u8"TgtPit"s,
            parameter_flag::kIsReadOnly | parameter_flag::kIsHidden,
            [](ControllerCore&, double) -> ErrorCode {
              return ErrorCode::kSuccess;
            },
            [](ProcessorProxy&, double) -> ErrorCode {
              return ErrorCode::kSuccess;
            }));
  }
  return schema;
}();
}  // namespace beatrice::common
