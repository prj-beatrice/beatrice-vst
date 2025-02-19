// Copyright (c) 2024-2025 Project Beatrice and Contributors

#include "vst/processor.h"

#include <cstring>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <string>

#include "vst3sdk/pluginterfaces/vst/ivstparameterchanges.h"
#include "vst3sdk/pluginterfaces/vst/vstspeaker.h"

// Beatrice
#include "common/error.h"
#include "common/parameter_schema.h"
#include "vst/parameter.h"

#ifdef BEATRICE_ONLY_FOR_LINTER_DO_NOT_COMPILE_WITH_THIS
#include "vst/metadata.h.in"
#else
#include "metadata.h"  // NOLINT(build/include_subdir)
#endif

namespace beatrice::vst {

using Steinberg::kResultFalse;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
namespace SpeakerArr = Steinberg::Vst::SpeakerArr;

// コンストラクタ
Processor::Processor() : vc_core_(common::kSchema) {
  // 対応するコントローラクラスを設定する
  setControllerClass(kControllerUID);
}

// "Initialized" の状態に遷移する
// チャンネル数の指定など
auto PLUGIN_API Processor::initialize(FUnknown* const context) -> tresult {
  // 親クラスの初期化
  const tresult result = AudioEffect::initialize(context);
  if (result != kResultTrue) {
    return kResultFalse;
  }

  // In/Out バスの生成
  addAudioInput(STR16("AudioInput"), SpeakerArr::kMono);
  addAudioOutput(STR16("AudioOutput"), SpeakerArr::kMono);

  return kResultTrue;
}

// バスの設定
// "Initialized" または "Setup Done" の時に呼ばれる
// VST の起動時にも呼ばれて勝手にチャンネル数変更しようとして来たりするので
// ちゃんと防ぐ
auto PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* const inputs,
                                              const int32 numIns,
                                              SpeakerArrangement* const outputs,
                                              const int32 numOuts) -> tresult {
  // 入力バス・出力バスの数はいずれも 1
  if (numIns == 1 && numOuts == 1 &&
      (inputs[0] == SpeakerArr::kMono || inputs[0] == SpeakerArr::kStereo) &&
      (outputs[0] == SpeakerArr::kMono || outputs[0] == SpeakerArr::kStereo)) {
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
  }

  return kResultFalse;
}

// "Setup Done" の状態に遷移する
// 必ず setActive(false) の状態で呼ばれる
// setup.maxSamplesPerBlock  最大ブロックサイズ
// setup.sampleRate          サンプリング周波数
// setup.processMode         kRealtime or kPrefetch or kOffline
// setup.symbolicSampleSize  kSample32 or kSample64
auto PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) -> tresult {
  std::lock_guard<std::mutex> lock(mtx_);
  if (setup.symbolicSampleSize == Steinberg::Vst::kSample64) {
    return kResultFalse;
  }
  const auto error_code = vc_core_.SetSampleRate(setup.sampleRate);
  assert(error_code == common::ErrorCode::kSuccess);
  return AudioEffect::setupProcessing(setup);
}

auto PLUGIN_API Processor::setActive(const TBool state) -> tresult {
  if (state) {
    // メモリの確保など
  } else {
    // メモリの解放など
    std::lock_guard<std::mutex> lock(mtx_);
    const auto error_code = vc_core_.GetCore()->ResetContext();
    assert(error_code == common::ErrorCode::kSuccess);
  }
  return AudioEffect::setActive(state);
}

// TODO(bug): tail を設定する

// メイン処理
auto PLUGIN_API Processor::process(ProcessData& data) -> tresult {
  // パラメータの変更があった場合
  if (data.inputParameterChanges != nullptr) {
    const auto n_parameter_changed =
        data.inputParameterChanges->getParameterCount();
    for (auto index = 0; index < n_parameter_changed; ++index) {
      // バッファの中で複数回同じパラメータの変更があることが考慮される
      auto* const param_queue =
          data.inputParameterChanges->getParameterData(index);
      if (param_queue == nullptr) {
        continue;
      }
      ParamValue value;
      int sample_offset;
      const auto n_points = param_queue->getPointCount();
      if (param_queue->getPoint(n_points - 1, sample_offset, value) !=
          kResultTrue) {
        continue;
      }
      unreflected_params_[param_queue->getParameterId()] = value;
    }
  }

  std::unique_lock<std::mutex> lock(mtx_, std::try_to_lock);
  // ファイルの読み込み中はパラメータ変更の処理を先送りにし、
  // 無音を出力する
  if (!lock.owns_lock()) {
    for (auto bus = 0; bus < data.numOutputs; ++bus) {
      for (auto ch = 0; ch < data.outputs[bus].numChannels; ++ch) {
        std::memset(data.outputs[bus].channelBuffers32[ch], 0,
                    data.numSamples * sizeof(float));
      }
      data.outputs[bus].silenceFlags = 1U;
    }
    return kResultTrue;
  }

  for (const auto [vst_param_id, value] : unreflected_params_) {
    const auto param_id = static_cast<common::ParameterID>(vst_param_id);
    const auto& param = common::kSchema.GetParameter(param_id);
    if (const auto* const num_param =
            std::get_if<common::NumberParameter>(&param)) {
      const auto denormalized_value = Denormalize(*num_param, value);
      const auto error_code =
          vc_core_.SetParameter(param_id, denormalized_value);
      assert(error_code == common::ErrorCode::kSuccess);
      assert(denormalized_value ==
             std::get<double>(vc_core_.GetParameterState().GetValue(param_id)));
    } else if (const auto* const list_param =
                   std::get_if<common::ListParameter>(&param)) {
      const auto denormalized_value = Denormalize(*list_param, value);
      const auto error_code =
          vc_core_.SetParameter(param_id, denormalized_value);
      assert(error_code == common::ErrorCode::kSuccess);
    }
  }
  unreflected_params_.clear();

  if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0) {
    // 何もしない
    return kResultOk;
  }

  // double は処理しない
  if (data.symbolicSampleSize == Steinberg::Vst::kSample64) {
    return kResultOk;
  }

  // チャンネル数を確認
  if (data.inputs[0].numChannels < 1) {
    return kResultOk;
  }
  if (data.outputs[0].numChannels < 1) {
    return kResultOk;
  }

  // 出力バス 0 のチャンネル 0 に入力をコピー
  const float* const in0 = data.inputs[0].channelBuffers32[0];
  float* const out0 = data.outputs[0].channelBuffers32[0];
  std::memmove(out0, in0, data.numSamples * sizeof(float));
  if (data.inputs[0].numChannels >= 2) {
    auto* const in1 = data.inputs[0].channelBuffers32[1];
    for (auto i = 0; i < data.numSamples; ++i) {
      out0[i] += in1[i];
      out0[i] *= 0.5;
    }
  }

  // サイレンスフラグの確認
  if (data.inputs[0].silenceFlags) {
    data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
    if (in0 != out0) {
      std::memset(out0, 0, data.numSamples * sizeof(float));
    }
    return kResultOk;
  }

  // 無音チェック
  auto sil = true;
  for (auto i = 0; i < data.numSamples; ++i) {
    if (out0[i] != 0.0F) {
      sil = false;
      break;
    }
  }
  // TODO(bug): 遅延させる
  if (sil) {
    data.outputs[0].silenceFlags = 1U;
  } else {
    // VC
    [[maybe_unused]] const auto error_code =
        vc_core_.GetCore()->Process(out0, out0, data.numSamples);
    // TODO(bug): error_code に基づいてサイレンスフラグを立てる
  }

  // 出力がステレオなら複製する
  if (data.outputs[0].numChannels >= 2) {
    auto* const out1 = data.outputs[0].channelBuffers32[1];
    memcpy(out1, out0, data.numSamples * sizeof(float));
  }

  return kResultOk;
}

// プロジェクトやプリセットをロードした時に呼ばれる。
// kResultFalse を返した場合、StudioRack などでは
// Controller::setComponentState が呼ばれなくなるため注意が必要。
auto PLUGIN_API Processor::setState(IBStream* const state) -> tresult {
  std::lock_guard<std::mutex> lock(mtx_);
  int siz;
  if (state->read(&siz, sizeof(siz)) != kResultTrue) {
    return kResultFalse;
  }
  auto state_string = std::string();
  state_string.resize(siz);
  if (state->read(std::to_address(state_string.begin()), siz) != kResultTrue) {
    return kResultFalse;
  }
  auto iss = std::istringstream(state_string, std::ios::binary);
  if (vc_core_.Read(iss) != common::ErrorCode::kSuccess) {
    return kResultFalse;
  }
  return kResultTrue;
}

auto PLUGIN_API Processor::getState(IBStream* const state) -> tresult {
  std::lock_guard<std::mutex> lock(mtx_);
  auto oss = std::ostringstream(std::ios::binary);
  if (vc_core_.Write(oss) != common::ErrorCode::kSuccess) {
    return kResultFalse;
  }
  auto state_string = oss.str();
  auto siz = static_cast<int>(state_string.size());
  if (state->write(&siz, sizeof(siz)) != kResultTrue) {
    return kResultFalse;
  }
  if (state->write(state_string.data(),
                   static_cast<int>(state_string.size())) != kResultTrue) {
    return kResultFalse;
  }
  return kResultTrue;
}

auto PLUGIN_API Processor::notify(IMessage* const message) -> tresult {
  const auto* const message_id = message->getMessageID();
  if (std::strcmp(message_id, "param_change") == 0) {
    std::lock_guard<std::mutex> lock(mtx_);
    uint32 siz;
    const void* data;
    if (message->getAttributes()->getBinary("param_id", data, siz) !=
        kResultTrue) {
      return kResultFalse;
    }
    if (siz != sizeof(ParamID)) {
      return kResultFalse;
    }
    ParamID vst_param_id;
    std::memcpy(&vst_param_id, data, sizeof(vst_param_id));
    if (message->getAttributes()->getBinary("data", data, siz) != kResultTrue) {
      return kResultFalse;
    }
    auto value = std::u8string();
    value.resize(siz);
    std::memcpy(value.data(), data, siz);
    const auto param_id = static_cast<common::ParameterID>(vst_param_id);
    const auto error_code = vc_core_.SetParameter(param_id, value);
    assert(error_code == common::ErrorCode::kSuccess);
    return kResultTrue;
  }
  return AudioEffect::notify(message);
}

}  // namespace beatrice::vst
