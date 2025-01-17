// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "vst3sdk/public.sdk/source/main/pluginfactory.h"

// Beatrice
#include "vst/controller.h"
#include "vst/processor.h"
#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

#define stringSubCategory "Fx|Vocals"

// NOLINTNEXTLINE(modernize-use-trailing-return-type,google-build-using-namespace)
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

// NOLINTNEXTLINE(readability/fn_size)
DEF_CLASS2(INLINE_UID_FROM_FUID(beatrice::vst::kProcessorUID),
           PClassInfo::kManyInstances, kVstAudioEffectClass, stringPluginName,
           Vst::kDistributable, stringSubCategory, FULL_VERSION_STR,
           kVstVersionString, beatrice::vst::Processor::createInstance)

// NOLINTNEXTLINE(readability/fn_size)
DEF_CLASS2(INLINE_UID_FROM_FUID(beatrice::vst::kControllerUID),
           PClassInfo::kManyInstances, kVstComponentControllerClass,
           stringPluginName "Controller", 0, "", FULL_VERSION_STR,
           kVstVersionString, beatrice::vst::Controller::createInstance)

END_FACTORY
