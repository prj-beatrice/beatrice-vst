// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_DESCRIPTION_TEXT_UTF8_H_
#define BEATRICE_VST_DESCRIPTION_TEXT_UTF8_H_

#include <cstddef>
#include <optional>
#include <string_view>

namespace beatrice::vst::description_text_detail {

// 文字コード変換は次の仕様に従う。
// [RFC 3629] UTF-8, §4 Syntax of UTF-8 Byte Sequences
//   https://www.rfc-editor.org/rfc/rfc3629#section-4
// [Unicode 17] The Unicode Standard 17.0.0, §3.9 Unicode Encoding Forms
//   https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/

// 指定したバイトが UTF-8 の継続バイトかを返す。
[[nodiscard]] inline auto IsContinuationByte(const unsigned char value)
    -> bool {
  return (value & 0xc0U) == 0x80U;
}

// 指定位置から始まる妥当な UTF-8 符号化列のバイト長を返す。
[[nodiscard]] inline auto GetUtf8SequenceLength(const std::u8string_view text,
                                                const std::size_t pos)
    -> std::optional<std::size_t> {
  if (pos >= text.size()) {
    return std::nullopt;
  }
  const auto first = static_cast<unsigned char>(text[pos]);
  if (first < 0x80U) {
    return 1;
  }

  auto length = std::size_t{0};
  auto second_min = static_cast<unsigned char>(0x80);
  auto second_max = static_cast<unsigned char>(0xbf);
  if (first >= 0xc2U && first <= 0xdfU) {
    length = 2;
  } else if (first >= 0xe0U && first <= 0xefU) {
    length = 3;
    second_min = first == 0xe0U ? 0xa0U : 0x80U;
    second_max = first == 0xedU ? 0x9fU : 0xbfU;
  } else if (first >= 0xf0U && first <= 0xf4U) {
    length = 4;
    second_min = first == 0xf0U ? 0x90U : 0x80U;
    second_max = first == 0xf4U ? 0x8fU : 0xbfU;
  } else {
    return std::nullopt;
  }
  if (length > text.size() - pos) {
    return std::nullopt;
  }

  for (auto offset = std::size_t{1}; offset < length; ++offset) {
    const auto byte = static_cast<unsigned char>(text[pos + offset]);
    if (!IsContinuationByte(byte) ||
        (offset == 1 && (byte < second_min || byte > second_max))) {
      return std::nullopt;
    }
  }
  return length;
}

}  // namespace beatrice::vst::description_text_detail

#endif  // BEATRICE_VST_DESCRIPTION_TEXT_UTF8_H_
