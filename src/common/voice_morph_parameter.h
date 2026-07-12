// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_VOICE_MORPH_PARAMETER_H_
#define BEATRICE_COMMON_VOICE_MORPH_PARAMETER_H_

#include <array>
#include <utility>

#include "common/parameter_schema.h"
#include "common/parameter_state.h"
#include "common/voice_morph_state.h"

namespace beatrice::common {

static constexpr auto kNVoiceMorphParameters = 4 + kMaxNVoiceMorphMarkers * 3;

[[nodiscard]] auto GetVoiceMorphState(const ParameterState& state)
    -> VoiceMorphState;
[[nodiscard]] auto GetVoiceMorphParameterValues(
    const VoiceMorphState& morph_state)
    -> std::array<std::pair<ParameterID, double>, kNVoiceMorphParameters>;

}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_VOICE_MORPH_PARAMETER_H_
