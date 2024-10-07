// Copyright (c) 2024 Project Beatrice

#ifndef BEATRICE_VST_CONSTANTS_H_
#define BEATRICE_VST_CONSTANTS_H_

#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

namespace beatrice::vst {

static constexpr auto kParamsPerGroup = 1000;

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_CONSTANTS_H_
