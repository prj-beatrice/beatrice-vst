// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_VST_PROCESSOR_H_
#define BEATRICE_VST_PROCESSOR_H_

#include <map>
#include <mutex>  // NOLINT(build/c++11)

#include "vst3sdk/pluginterfaces/base/ibstream.h"
#include "vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h"
#include "vst3sdk/pluginterfaces/vst/vsttypes.h"
#include "vst3sdk/public.sdk/source/vst/vstaudioeffect.h"

// Beatrice
#include "common/processor_proxy.h"

namespace beatrice::vst {

class Processor : public Steinberg::Vst::AudioEffect {
  using tresult = Steinberg::tresult;
  using int32 = Steinberg::int32;
  using uint32 = Steinberg::uint32;
  using TBool = Steinberg::TBool;
  using IBStream = Steinberg::IBStream;
  using SpeakerArrangement = Steinberg::Vst::SpeakerArrangement;
  using ProcessSetup = Steinberg::Vst::ProcessSetup;
  using ProcessData = Steinberg::Vst::ProcessData;
  using IMessage = Steinberg::Vst::IMessage;
  using IAudioProcessor = Steinberg::Vst::IAudioProcessor;
  using ParamID = Steinberg::Vst::ParamID;
  using ParamValue = Steinberg::Vst::ParamValue;

 public:
  Processor();

  auto PLUGIN_API initialize(FUnknown* context) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                     SpeakerArrangement* outputs, int32 numOuts)
      -> tresult SMTG_OVERRIDE;

  auto PLUGIN_API setupProcessing(ProcessSetup& setup) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API setActive(TBool state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API process(ProcessData& data) -> tresult SMTG_OVERRIDE;

  auto PLUGIN_API setState(IBStream* state) -> tresult SMTG_OVERRIDE;
  auto PLUGIN_API getState(IBStream* state) -> tresult SMTG_OVERRIDE;

  auto PLUGIN_API notify(IMessage* message) -> tresult SMTG_OVERRIDE;

  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto createInstance(void* /*context*/) -> FUnknown* {
    return static_cast<IAudioProcessor*>(new Processor());
  }

 private:
  std::mutex mtx_;
  common::ProcessorProxy vc_core_;
  // メモリ確保が挟まるのが望ましくないが……
  std::map<ParamID, ParamValue> unreflected_params_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_PROCESSOR_H_
