// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_MODEL_CONFIG_H_
#define BEATRICE_COMMON_MODEL_CONFIG_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "toml11/single_include/toml.hpp"

namespace beatrice::common {

static constexpr auto kMaxNSpeakers = 256;

// モデル情報の TOML ファイルを読み込むための構造体
struct ModelConfig {
  struct Model {
    std::string version;
    std::u8string name;
    std::u8string description;
    [[nodiscard]] auto VersionInt() const -> int {
      if (version == "2.0.0-alpha.2") {
        return 0;
      } else if (version == "2.0.0-beta.1") {
        return 1;
      } else if (version == "2.0.0-rc.0") {
        return 2;
      } else {
        return -1;
      }
    }
  } model;
  struct Voice {
    struct Portrait {
      std::u8string path;
      std::u8string description;
    };
    std::u8string name;
    std::u8string description;
    double average_pitch;
    Portrait portrait;
  };
  std::array<Voice, kMaxNSpeakers> voices;
};

[[nodiscard]] inline auto GetVoiceCount(const ModelConfig& model_config)
    -> int {
  for (auto i = 0; i < kMaxNSpeakers; ++i) {
    const auto& voice = model_config.voices[i];
    if (voice.name.empty() && voice.description.empty() &&
        voice.portrait.path.empty() && voice.portrait.description.empty()) {
      return i;
    }
  }
  return kMaxNSpeakers;
}

// TOML の表示文字列を読み込み、NUL を空白へ置き換える。
[[nodiscard]] inline auto ReadDisplayText(const toml::value& value,
                                          const char* const key)
    -> std::u8string {
  auto text = toml::find<std::u8string>(value, key);
  std::ranges::replace(text, u8'\0', u8' ');
  return text;
}

}  // namespace beatrice::common

namespace toml {

using beatrice::common::ModelConfig;

template <>
struct from<ModelConfig::Model> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Model {
    return ModelConfig::Model{
        .version = find<std::string>(v, "version"),
        .name = beatrice::common::ReadDisplayText(v, "name"),
        .description = beatrice::common::ReadDisplayText(v, "description")};
  }
};
template <>
struct from<ModelConfig::Voice::Portrait> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Voice::Portrait {
    return ModelConfig::Voice::Portrait{
        .path = find<std::u8string>(v, "path"),
        .description = beatrice::common::ReadDisplayText(v, "description")};
  }
};

template <>
struct from<ModelConfig::Voice> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig::Voice {
    const auto voice = ModelConfig::Voice{
        .name = beatrice::common::ReadDisplayText(v, "name"),
        .description = beatrice::common::ReadDisplayText(v, "description"),
        .average_pitch = find<double>(v, "average_pitch"),
        .portrait = get<ModelConfig::Voice::Portrait>(find(v, "portrait"))};
    if (!std::isfinite(voice.average_pitch) || voice.average_pitch < 0.0 ||
        voice.average_pitch > 128.0) {
      throw std::invalid_argument(
          "average_pitch must be finite and between 0 and 128");
    }
    return voice;
  }
};
template <>
struct from<ModelConfig> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  static auto from_toml(const value& v) -> ModelConfig {
    auto target_speakers =
        std::array<ModelConfig::Voice, beatrice::common::kMaxNSpeakers>();
    const auto& voices = find<toml::table>(v, "voice");
    for (const auto& [key, value] : voices) {
      const auto id = std::stoi(key);
      if (id < 0 || id >= beatrice::common::kMaxNSpeakers) {
        throw std::out_of_range("speaker id out of range");
      }
      target_speakers[id] = get<ModelConfig::Voice>(value);
    }
    auto model_config =
        ModelConfig{.model = find<ModelConfig::Model>(v, "model"),
                    .voices = target_speakers};
    const auto voice_count = beatrice::common::GetVoiceCount(model_config);
    if (voice_count == 0 || std::cmp_not_equal(voice_count, voices.size())) {
      throw std::invalid_argument(
          "voice ids must start at zero and be contiguous");
    }
    return model_config;
  }
};
}  // namespace toml

#endif  // BEATRICE_COMMON_MODEL_CONFIG_H_
