// Copyright (c) 2024-2026 Project Beatrice and Contributors

#include "vst/description_text_layout.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Beatrice
#include "vst/description_text_utf8.h"

namespace beatrice::vst {
namespace {

// 行末が昇順の UTF-8 コードポイント境界で、段落末まで達することを確認する。
[[nodiscard]] auto ValidateLineEnds(const std::vector<std::size_t>& line_ends,
                                    const std::u8string_view text) -> bool {
  if (line_ends.empty()) {
    return false;
  }
  auto previous = std::size_t{0};
  for (const auto line_end : line_ends) {
    if (line_end <= previous || line_end > text.size() ||
        (line_end < text.size() &&
         description_text_detail::IsContinuationByte(
             static_cast<unsigned char>(text[line_end])))) {
      return false;
    }
    previous = line_end;
  }
  return previous == text.size();
}

// 指定範囲の末尾にある ASCII の空白とタブを除外する。
[[nodiscard]] auto TrimTrailingSpaces(const std::u8string_view text,
                                      const std::size_t begin, std::size_t end)
    -> std::size_t {
  while (end > begin && (text[end - 1] == u8' ' || text[end - 1] == u8'\t')) {
    --end;
  }
  return end;
}

// 指定範囲の先頭にある ASCII の空白とタブを読み飛ばす。
[[nodiscard]] auto SkipLeadingSpaces(const std::u8string_view text,
                                     std::size_t begin, const std::size_t end)
    -> std::size_t {
  while (begin < end && (text[begin] == u8' ' || text[begin] == u8'\t')) {
    ++begin;
  }
  return begin;
}

// 1 段落について、行分割処理が返した行末へ表示用改行を挿入する。
[[nodiscard]] auto WrapParagraph(const std::u8string_view paragraph,
                                 const double max_width,
                                 const LayoutTextLines& layout_lines)
    -> std::u8string {
  if (paragraph.empty()) {
    return {};
  }

  auto line_ends = layout_lines(paragraph, max_width);
  if (!line_ends || !ValidateLineEnds(*line_ends, paragraph)) {
    return std::u8string(paragraph);
  }

  auto result = std::u8string{};
  auto line_start = std::size_t{0};
  for (auto index = std::size_t{0}; index < line_ends->size(); ++index) {
    const auto line_end = (*line_ends)[index];
    // ソフト改行に使われた空白を次行へ残さない。段落先頭の字下げは保つ。
    const auto display_start =
        index == 0 ? line_start
                   : SkipLeadingSpaces(paragraph, line_start, line_end);
    const auto display_end =
        line_end == paragraph.size()
            ? line_end
            : TrimTrailingSpaces(paragraph, display_start, line_end);
    if (index != 0) {
      result.push_back(u8'\n');
    }
    result.append(paragraph.substr(display_start, display_end - display_start));
    line_start = line_end;
  }
  return result;
}

}  // namespace

auto BuildUtf16ToUtf8BoundaryMap(const std::u8string_view text)
    -> std::optional<std::vector<std::size_t>> {
  auto result = std::vector<std::size_t>{0};
  result.reserve(text.size() + 1);

  auto pos = std::size_t{0};
  while (pos < text.size()) {
    const auto length =
        description_text_detail::GetUtf8SequenceLength(text, pos);
    if (!length) {
      return std::nullopt;
    }
    pos += *length;
    // 出典: [Unicode 17] §3.9。4 バイト UTF-8 列は UTF-16 では
    // サロゲートペアになる。本実装ではペア内部がスカラー値境界でないことを
    // kInvalidUtf8Offset で表す。
    if (*length == 4) {
      result.push_back(kInvalidUtf8Offset);
    }
    result.push_back(pos);
  }
  return result;
}

auto LayoutDescriptionText(const std::u8string_view text,
                           const double max_width,
                           const LayoutTextLines& layout_lines)
    -> std::u8string {
  if (!layout_lines || !std::isfinite(max_width) || max_width <= 0.0) {
    return std::u8string(text);
  }

  auto result = std::u8string{};
  auto paragraph_start = std::size_t{0};
  while (paragraph_start < text.size()) {
    auto paragraph_end = paragraph_start;
    while (paragraph_end < text.size() && text[paragraph_end] != u8'\r' &&
           text[paragraph_end] != u8'\n') {
      ++paragraph_end;
    }
    result += WrapParagraph(
        text.substr(paragraph_start, paragraph_end - paragraph_start),
        max_width, layout_lines);
    if (paragraph_end == text.size()) {
      break;
    }

    result.push_back(u8'\n');
    if (text[paragraph_end] == u8'\r' && paragraph_end + 1 < text.size() &&
        text[paragraph_end + 1] == u8'\n') {
      paragraph_start = paragraph_end + 2;
    } else {
      paragraph_start = paragraph_end + 1;
    }
  }
  return result;
}

}  // namespace beatrice::vst
