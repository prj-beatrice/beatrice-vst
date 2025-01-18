// Copyright (c) 2024-2025 Project Beatrice and Contributors

#ifndef BEATRICE_COMMON_ERROR_H_
#define BEATRICE_COMMON_ERROR_H_

#include <cstdint>

#include "beatricelib/beatrice.h"

namespace beatrice::common {
enum class [[nodiscard]] ErrorCode : std::uint8_t {
  kSuccess = 0,
  kFileOpenError = Beatrice_kFileOpenError,
  kFileTooSmall = Beatrice_kFileTooSmall,
  kFileTooLarge = Beatrice_kFileTooLarge,
  kInvalidFileSize = Beatrice_kInvalidFileSize,
  kTOMLSyntaxError,
  kSpeakerIDOutOfRange,
  kInvalidPitchCorrectionType,
  kModelNotLoaded,
  kResamplerNotReady,
  kGainNotReady,
  kUnknownError,
};
}  // namespace beatrice::common

#endif  // BEATRICE_COMMON_ERROR_H_
