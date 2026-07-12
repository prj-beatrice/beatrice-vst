// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "common/voice_morph_parameter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "common/model_config.h"
#include "common/parameter_schema.h"
#include "common/parameter_state.h"
#include "common/voice_morph_state.h"

namespace beatrice::common {

namespace {
[[nodiscard]] auto GetDoubleParameter(const ParameterState& state,
                                      const ParameterID param_id) -> double {
  return std::get<double>(state.GetValue(param_id));
}
}  // namespace

auto GetVoiceMorphState(const ParameterState& state) -> VoiceMorphState {
  auto morph_state = VoiceMorphState();
  morph_state.cursor_x = static_cast<float>(std::clamp(
      GetDoubleParameter(state, ParameterID::kVoiceMorphCursorX), 0.0, 1.0));
  morph_state.cursor_y = static_cast<float>(std::clamp(
      GetDoubleParameter(state, ParameterID::kVoiceMorphCursorY), 0.0, 1.0));
  morph_state.falloff =
      std::clamp(static_cast<float>(GetDoubleParameter(
                     state, ParameterID::kVoiceMorphFalloff)),
                 kVoiceMorphFalloffMin, kVoiceMorphFalloffMax);

  const auto marker_count =
      std::clamp(static_cast<int>(std::round(GetDoubleParameter(
                     state, ParameterID::kVoiceMorphMarkerCount))),
                 1, kMaxNVoiceMorphMarkers);
  morph_state.marker_count = marker_count;
  for (auto i = 0; i < marker_count; ++i) {
    const auto voice_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerVoiceBase) + i);
    const auto x_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerXBase) + i);
    const auto y_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerYBase) + i);
    auto marker = VoiceMorphMarker();
    marker.voice_id = std::clamp(
        static_cast<int>(std::round(GetDoubleParameter(state, voice_param_id))),
        0, static_cast<int>(kMaxNSpeakers) - 1);
    marker.x = static_cast<float>(
        std::clamp(GetDoubleParameter(state, x_param_id), 0.0, 1.0));
    marker.y = static_cast<float>(
        std::clamp(GetDoubleParameter(state, y_param_id), 0.0, 1.0));
    morph_state.markers[i] = marker;
  }
  return morph_state;
}

auto GetVoiceMorphParameterValues(const VoiceMorphState& morph_state)
    -> std::array<std::pair<ParameterID, double>, kNVoiceMorphParameters> {
  auto values =
      std::array<std::pair<ParameterID, double>, kNVoiceMorphParameters>{};
  auto value_index = 0;
  values[value_index++] = {
      ParameterID::kVoiceMorphCursorX,
      static_cast<double>(std::clamp(morph_state.cursor_x, 0.0f, 1.0f))};
  values[value_index++] = {
      ParameterID::kVoiceMorphCursorY,
      static_cast<double>(std::clamp(morph_state.cursor_y, 0.0f, 1.0f))};
  values[value_index++] = {
      ParameterID::kVoiceMorphFalloff,
      static_cast<double>(std::clamp(morph_state.falloff, kVoiceMorphFalloffMin,
                                     kVoiceMorphFalloffMax))};

  const auto marker_count =
      std::clamp(morph_state.marker_count, 1, kMaxNVoiceMorphMarkers);
  values[value_index++] = {ParameterID::kVoiceMorphMarkerCount,
                           static_cast<double>(marker_count)};
  for (auto i = 0; i < kMaxNVoiceMorphMarkers; ++i) {
    const auto voice_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerVoiceBase) + i);
    const auto x_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerXBase) + i);
    const auto y_param_id = static_cast<ParameterID>(
        static_cast<int>(ParameterID::kVoiceMorphMarkerYBase) + i);
    const auto marker =
        i < marker_count ? morph_state.markers[i] : VoiceMorphMarker{};
    values[value_index++] = {
        voice_param_id,
        static_cast<double>(std::clamp(marker.voice_id, 0,
                                       static_cast<int>(kMaxNSpeakers) - 1))};
    values[value_index++] = {
        x_param_id, static_cast<double>(std::clamp(marker.x, 0.0f, 1.0f))};
    values[value_index++] = {
        y_param_id, static_cast<double>(std::clamp(marker.y, 0.0f, 1.0f))};
  }
  return values;
}

}  // namespace beatrice::common
