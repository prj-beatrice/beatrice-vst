// Copyright (c) 2024 Project Beatrice

#include "common/parameter_schema.h"

#include <filesystem>
#include <tuple>

#include "common/controller_core.h"
#include "common/model_config.h"
#include "common/processor_core.h"
#include "common/processor_proxy.h"

namespace beatrice::common {

using std::operator""s;

static constexpr auto kMaxAbsPitchShift = 24.0;

// パラメータの追加には以下 3 箇所の変更が必要
// * parameter_schema.cpp                   (メタデータの設定)
// * processor_core.h, processor_core_*.cpp (信号処理)
// * editor.cpp                             (GUI)

// パラメータ ID に対して、そのパラメータはどのような名前で、
// どのような値域を持ち、どのような操作に対応するのかを保持する。
// グループ分けする意味無かった感あるので直したい。
const ParameterSchema kSchema = ParameterSchema({
    {0,
     ParameterGroup(
         "General",
         {
             // 0: バイパスに予約
             {1,
              StringParameter(
                  u8"Model"s, u8""s, false,
                  [](ControllerCore& controller, const std::u8string& value) {
                    ModelConfig model_config;
                    try {
                      const auto toml_data =
                          toml::parse(std::filesystem::path(value));
                      model_config = toml::get<ModelConfig>(toml_data);
                    } catch (const std::exception& e) {
                      return 1;
                    }

                    // Voice
                    controller.parameter_state_.SetValue(1, 0, 0);
                    controller.updated_parameters_.emplace_back(1, 0);
                    // FormantShift
                    controller.parameter_state_.SetValue(1, 6, 0.0);
                    controller.updated_parameters_.emplace_back(1, 6);
                    // AverageTargetPitches
                    for (auto i = 0; i < kMaxNSpeakers; ++i) {
                      controller.parameter_state_.SetValue(
                          100, i, model_config.voices[i].average_pitch);
                      controller.updated_parameters_.emplace_back(100, i);
                    }
                    const auto average_target_pitch =
                        model_config.voices[0].average_pitch;
                    switch (std::get<int>(
                        controller.parameter_state_.GetValue(1, 9))) {
                      case 0: {  // AverageSourcePitch 固定
                        // ピッチシフト量を変更する
                        const auto average_source_pitch = std::get<double>(
                            controller.parameter_state_.GetValue(1, 8));
                        const auto shift = std::clamp(
                            average_target_pitch - average_source_pitch,
                            -kMaxAbsPitchShift, kMaxAbsPitchShift);
                        controller.parameter_state_.SetValue(1, 7, shift);
                        controller.updated_parameters_.emplace_back(1, 7);
                      } break;
                      case 1: {  // PitchShift 固定
                        // AverageSourcePitch を変更する
                        const auto pitch_shift = std::get<double>(
                            controller.parameter_state_.GetValue(1, 7));
                        // clamp するべき？
                        const auto average_source_pitch =
                            average_target_pitch - pitch_shift;
                        controller.parameter_state_.SetValue(
                            1, 8, average_source_pitch);
                        controller.updated_parameters_.emplace_back(1, 8);
                        break;
                      }
                    }
                    return 0;
                  },
                  [](ProcessorProxy& vc, const std::u8string& value) {
                    vc.LoadModel(value);
                    return 0;
                  })},
         })},
    {1,
     ParameterGroup(
         "Target",
         {
             {0, ListParameter(
                     u8"Voice"s,
                     [] {
                       auto v = std::vector<std::u8string>();
                       for (auto i = 0; i < kMaxNSpeakers; ++i) {
                         const auto i_ascii = std::to_string(i);
                         const auto i_u8 =
                             std::u8string(i_ascii.begin(), i_ascii.end());
                         v.push_back(u8"ID "s + i_u8);
                       }
                       return v;
                     }(),
                     0, u8"Voi"s, parameter_flag::kCanAutomate,
                     [](ControllerCore& controller, const int value) {
                       if (value < 0 || value >= kMaxNSpeakers) {
                         return 1;
                       }
                       const auto formant_shift = std::get<double>(
                           controller.parameter_state_.GetValue(1, 6));
                       const auto average_target_pitch = std::get<double>(
                           controller.parameter_state_.GetValue(100, value));
                       switch (std::get<int>(
                           controller.parameter_state_.GetValue(1, 9))) {
                         case 0: {  // AverageSourcePitch 固定
                           // ピッチシフト量を変更する
                           const auto average_source_pitch = std::get<double>(
                               controller.parameter_state_.GetValue(1, 8));
                           const auto shift = std::clamp(
                               average_target_pitch + formant_shift -
                                   average_source_pitch,
                               -kMaxAbsPitchShift, kMaxAbsPitchShift);
                           controller.parameter_state_.SetValue(1, 7, shift);
                           controller.updated_parameters_.emplace_back(1, 7);
                         } break;
                         case 1: {  // PitchShift 固定
                           // AverageSourcePitch を変更する
                           const auto pitch_shift = std::get<double>(
                               controller.parameter_state_.GetValue(1, 7));
                           // clamp するべき？
                           const auto average_source_pitch =
                               average_target_pitch + formant_shift -
                               pitch_shift;
                           controller.parameter_state_.SetValue(
                               1, 8, average_source_pitch);
                           controller.updated_parameters_.emplace_back(1, 8);
                           break;
                         }
                       }
                       return 0;
                     },
                     [](ProcessorProxy& vc, const int value) {
                       return vc.GetCore()->SetTargetSpeaker(value);
                     })},
             // 1 から 5 は話者マージに予約
             {6, NumberParameter(
                     u8"FormantShift"s, 0.0, -2.0, 2.0, u8"semitones"s, 8,
                     u8"For"s, parameter_flag::kCanAutomate,
                     [](ControllerCore& controller, const double value) {
                       const auto target_speaker = std::get<int>(
                           controller.parameter_state_.GetValue(1, 0));
                       const auto average_target_pitch = std::get<double>(
                           controller.parameter_state_.GetValue(
                               100, target_speaker));
                       switch (std::get<int>(
                           controller.parameter_state_.GetValue(1, 9))) {
                         case 0: {  // AverageSourcePitch 固定
                           // ピッチシフト量を変更する
                           const auto average_source_pitch = std::get<double>(
                               controller.parameter_state_.GetValue(1, 8));
                           const auto shift = std::clamp(
                               average_target_pitch + value -
                                   average_source_pitch,
                               -kMaxAbsPitchShift, kMaxAbsPitchShift);
                           controller.parameter_state_.SetValue(1, 7, shift);
                           controller.updated_parameters_.emplace_back(1, 7);
                         } break;
                         case 1: {
                           // AverageSourcePitch を変更する
                           const auto pitch_shift = std::get<double>(
                               controller.parameter_state_.GetValue(1, 7));
                           // clamp するべき？
                           const auto average_source_pitch =
                               average_target_pitch + value - pitch_shift;
                           controller.parameter_state_.SetValue(
                               1, 8, average_source_pitch);
                           controller.updated_parameters_.emplace_back(1, 8);
                           break;
                         }
                       }
                       return 0;
                     },
                     [](ProcessorProxy& vc, const double value) {
                       return vc.GetCore()->SetFormantShift(value);
                     })},
             {7, NumberParameter(
                     u8"PitchShift"s, 0.0, -kMaxAbsPitchShift,
                     kMaxAbsPitchShift, u8"semitones"s, 48 * 8, u8"Pit"s,
                     parameter_flag::kCanAutomate,
                     [](ControllerCore& controller, const double value) {
                       // AverageSourcePitch を変更する
                       const auto target_speaker = std::get<int>(
                           controller.parameter_state_.GetValue(1, 0));
                       const auto formant_shift = std::get<double>(
                           controller.parameter_state_.GetValue(1, 6));
                       const auto average_target_pitch = std::get<double>(
                           controller.parameter_state_.GetValue(
                               100, target_speaker));
                       // clamp するべき？
                       const auto average_source_pitch =
                           average_target_pitch + formant_shift - value;
                       controller.parameter_state_.SetValue(
                           1, 8, average_source_pitch);
                       controller.updated_parameters_.emplace_back(1, 8);
                       return 0;
                     },
                     [](ProcessorProxy& vc, const double value) {
                       return vc.GetCore()->SetPitchShift(value);
                     })},
             {8, NumberParameter(
                     u8"AverageSourcePitch"s, 52.0, 0.0, 128.0, u8""s, 128 * 8,
                     u8"SrcPit"s, parameter_flag::kNoFlags,
                     [](ControllerCore& controller, const double value) {
                       // PitchShift を変更する
                       const auto target_speaker = std::get<int>(
                           controller.parameter_state_.GetValue(1, 0));
                       const auto formant_shift = std::get<double>(
                           controller.parameter_state_.GetValue(1, 6));
                       const auto average_target_pitch = std::get<double>(
                           controller.parameter_state_.GetValue(
                               100, target_speaker));
                       const auto pitch_shift = std::clamp(
                           average_target_pitch + formant_shift - value,
                           -kMaxAbsPitchShift, kMaxAbsPitchShift);
                       controller.parameter_state_.SetValue(1, 7, pitch_shift);
                       controller.updated_parameters_.emplace_back(1, 7);
                       return 0;
                     },
                     [](ProcessorProxy&, double) { return 0; })},
             {9, ListParameter(
                     u8"Lock"s, {u8"AverageSourcePitch"s, u8"PitchShift"s}, 0,
                     u8"Loc"s, parameter_flag::kIsList,
                     [](ControllerCore&, int) { return 0; },
                     [](ProcessorProxy&, int) { return 0; })},
         })},
    {2, ParameterGroup(
            "Gain",
            {
                {0, NumberParameter(
                        u8"InputGain"s, 0.0, -60.0, 20.0, u8"dB"s, 0,
                        u8"Gain/In"s, parameter_flag::kCanAutomate,
                        [](ControllerCore&, double) { return 0; },
                        [](ProcessorProxy& vc, const double value) {
                          return vc.GetCore()->SetInputGain(value);
                        })},
                {1, NumberParameter(
                        u8"OutputGain"s, 0.0, -60.0, 20.0, u8"dB"s, 0,
                        u8"Gain/Out"s, parameter_flag::kCanAutomate,
                        [](ControllerCore&, double) { return 0; },
                        [](ProcessorProxy& vc, const double value) {
                          return vc.GetCore()->SetOutputGain(value);
                        })},
            })},
    {100,
     ParameterGroup("AverageTargetPitches",
                    [] {
                      auto v = std::vector<std::tuple<int, ParameterVariant>>();
                      for (auto i = 0; i < kMaxNSpeakers; ++i) {
                        const auto i_ascii = std::to_string(i);
                        const auto i_u8 =
                            std::u8string(i_ascii.begin(), i_ascii.end());
                        v.emplace_back(
                            i, NumberParameter(
                                   u8"Speaker "s + i_u8, 60.0, 0.0, 128.0,
                                   u8""s, 128 * 8, u8"TgtPit"s,
                                   parameter_flag::kIsReadOnly |
                                       parameter_flag::kIsHidden,
                                   [](ControllerCore&, double) { return 0; },
                                   [](ProcessorProxy&, double) { return 0; }));
                      }
                      return v;
                    }())},
});

}  // namespace beatrice::common
