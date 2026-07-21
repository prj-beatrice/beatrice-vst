// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_DESCRIPTION_TEXT_LAYOUT_H_
#define BEATRICE_VST_DESCRIPTION_TEXT_LAYOUT_H_

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beatrice::vst {

// 成功時は段落末を含む各行末の UTF-8 バイト位置、失敗時は nullopt とする。
using TextLineEnds = std::optional<std::vector<std::size_t>>;
using LayoutTextLines = std::function<TextLineEnds(std::u8string_view, double)>;

// 本文を折り返し、既存の CR、LF、CRLF は '\n' に統一する。
[[nodiscard]] auto LayoutDescriptionText(std::u8string_view text,
                                         double max_width,
                                         const LayoutTextLines& layout_lines)
    -> std::u8string;

// UTF-16 の境界を UTF-8 へ変換できないことを表す。
inline constexpr auto kInvalidUtf8Offset = static_cast<std::size_t>(-1);

// 妥当な UTF-8 について、UTF-16 のコード単位境界をバイト位置へ対応付ける。
// サロゲートペア内部には kInvalidUtf8Offset、不正 UTF-8 では nullopt を返す。
[[nodiscard]] auto BuildUtf16ToUtf8BoundaryMap(std::u8string_view text)
    -> std::optional<std::vector<std::size_t>>;

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_DESCRIPTION_TEXT_LAYOUT_H_
