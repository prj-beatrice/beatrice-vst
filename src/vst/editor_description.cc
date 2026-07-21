// Copyright (c) 2024-2026 Project Beatrice and Contributors

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "vst/editor_description.h"

#if defined(_WIN32)
#include <dwrite.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#else
#error Unsupported platform
#endif

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <cstdint>
#elif defined(__APPLE__)
#include <cmath>
#include <memory>
#include <optional>
#include <type_traits>
#endif

#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/cstring.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"

#if defined(_WIN32)
#include "vst3sdk/vstgui4/vstgui/lib/platform/win32/comptr.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/win32/direct2d/d2dfont.h"
#elif defined(__APPLE__)
#include "vst3sdk/vstgui4/vstgui/lib/platform/mac/cfontmac.h"
#endif

// Beatrice
#include "vst/description_text_layout.h"

namespace beatrice::vst {
namespace {

// UTF-16 の行末を対応する UTF-8 の行末として追加する。
[[nodiscard]] auto AppendMappedLineEnd(
    const std::vector<std::size_t>& utf16_to_utf8,
    const std::size_t utf16_offset, std::vector<std::size_t>& destination)
    -> bool {
  if (utf16_offset >= utf16_to_utf8.size()) {
    return false;
  }
  const auto utf8_offset = utf16_to_utf8[utf16_offset];
  if (utf8_offset == kInvalidUtf8Offset) {
    return false;
  }
  destination.push_back(utf8_offset);
  return true;
}

// UTF-8 のバイト列を VSTGUI の文字列へ変換する。
[[nodiscard]] auto ToVstGuiString(std::u8string_view text)
    -> VSTGUI::UTF8String {
  return {std::string(reinterpret_cast<const char*>(text.data()), text.size())};
}

#if defined(_WIN32)

// DirectWrite に表示幅とフォントを渡し、段落の最終的な行末を取得する。
// 出典: DWRITE_WORD_WRAPPING / IDWriteTextLayout::SetWordWrapping /
// IDWriteTextLayout::GetLineMetrics。
//   https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_word_wrapping
//   https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextformat-setwordwrapping
//   https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextlayout-getlinemetrics
// DWRITE_WORD_WRAPPING_WRAP との差分: 最大幅より長い単語にも緊急改行を
// 許可するため、DWRITE_WORD_WRAPPING_EMERGENCY_BREAK を指定する。
[[nodiscard]] auto LayoutNativeDescriptionLines(const std::u8string_view text,
                                                const double max_width,
                                                VSTGUI::CFontDesc* const font)
    -> TextLineEnds {
  if (!font ||
      max_width > static_cast<double>(std::numeric_limits<float>::max()) ||
      text.size() > std::numeric_limits<std::uint32_t>::max()) {
    return {};
  }

  const auto utf16_to_utf8 = BuildUtf16ToUtf8BoundaryMap(text);
  if (!utf16_to_utf8) {
    return {};
  }

  const auto platform_font = font->getPlatformFont().cast<VSTGUI::D2DFont>();
  if (!platform_font) {
    return {};
  }

  const auto utf8 = ToVstGuiString(text);
  auto layout = VSTGUI::COM::adopt(
      platform_font->createTextLayout(utf8.getPlatformString()));
  if (!layout) {
    return {};
  }

  // VSTGUI の実描画と同じ locale 設定を使い、折り返し判定時と描画時の
  // 字形・幅を一致させる。
  if (layout->SetMaxWidth(static_cast<float>(max_width)) < 0 ||
      layout->SetMaxHeight(std::numeric_limits<float>::max()) < 0 ||
      layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK) < 0) {
    return {};
  }

  auto line_count = std::uint32_t{0};
  layout->GetLineMetrics(nullptr, 0, &line_count);
  if (line_count == 0) {
    return {};
  }

  auto lines = std::vector<DWRITE_LINE_METRICS>(line_count);
  if (layout->GetLineMetrics(lines.data(), line_count, &line_count) < 0) {
    return {};
  }
  lines.resize(line_count);

  auto line_ends = std::vector<std::size_t>{};
  auto utf16_offset = std::size_t{0};
  for (const auto& line : lines) {
    utf16_offset += line.length;
    if (!AppendMappedLineEnd(*utf16_to_utf8, utf16_offset, line_ends)) {
      return {};
    }
  }
  return line_ends;
}

#elif defined(__APPLE__)

struct CFReleaser {
  // 所有する Core Foundation オブジェクトをスコープ終了時に解放する。
  void operator()(CFTypeRef value) const noexcept { CFRelease(value); }
};

template <typename T>
using ScopedCFRef = std::unique_ptr<std::remove_pointer_t<T>, CFReleaser>;

// Create/Copy 規則で得た Core Foundation オブジェクトの所有権を受け取る。
template <typename T>
[[nodiscard]] auto AdoptCF(const T value) -> ScopedCFRef<T> {
  return ScopedCFRef<T>(value);
}

// Core Text が提案した範囲から、行末空白を除く表示幅を返す。
// 出典: CTLineGetTrailingWhitespaceWidth。提案幅を越え得る行末空白は
// 描画上問題にならず、共通層でもソフト改行時に除去するため差し引く。
//   https://developer.apple.com/documentation/coretext/ctlinegettrailingwhitespacewidth
[[nodiscard]] auto MeasureTypesetterLine(const CTTypesetterRef typesetter,
                                         const CFIndex start,
                                         const CFIndex length)
    -> std::optional<double> {
  const auto line =
      AdoptCF(CTTypesetterCreateLine(typesetter, CFRangeMake(start, length)));
  if (!line) {
    return std::nullopt;
  }
  const auto width =
      CTLineGetTypographicBounds(line.get(), nullptr, nullptr, nullptr);
  return std::max(0.0, width - CTLineGetTrailingWhitespaceWidth(line.get()));
}

// クラスタ改行の長さを返し、正の候補がない場合も合成文字シーケンス
// 1 個の境界まで前進する。
// 出典: CTTypesetterSuggestClusterBreak /
// CFStringGetRangeOfComposedCharactersAtIndex。
//   https://developer.apple.com/documentation/coretext/cttypesettersuggestclusterbreak%28_%3A_%3A_%3A%29
//   https://developer.apple.com/documentation/corefoundation/cfstringgetrangeofcomposedcharactersatindex%28_%3A_%3A%29
[[nodiscard]] auto SuggestEmergencyLineLength(const CTTypesetterRef typesetter,
                                              const CFStringRef string,
                                              const CFIndex start,
                                              const double max_width)
    -> CFIndex {
  auto length = CTTypesetterSuggestClusterBreak(typesetter, start, max_width);
  if (length > 0) {
    return length;
  }

  const auto cluster =
      CFStringGetRangeOfComposedCharactersAtIndex(string, start);
  return cluster.location == start ? cluster.length : 0;
}

// Core Text に表示幅とフォントを渡し、段落の最終的な行末を取得する。
// 出典: CTTypesetterSuggestLineBreak / CTTypesetterSuggestClusterBreak。
//   https://developer.apple.com/documentation/coretext/cttypesettersuggestlinebreak%28_%3A_%3A_%3A%29
//   https://developer.apple.com/documentation/coretext/cttypesettersuggestclusterbreak%28_%3A_%3A_%3A%29
// 通常改行のみとの差分: 候補が得られない、残り範囲を超える、または
// 実測幅が最大幅を超える場合は、クラスタ改行候補へ切り替える。
[[nodiscard]] auto LayoutNativeDescriptionLines(const std::u8string_view text,
                                                const double max_width,
                                                VSTGUI::CFontDesc* const font)
    -> TextLineEnds {
  if (!font || text.size() > static_cast<std::size_t>(
                                 std::numeric_limits<CFIndex>::max())) {
    return {};
  }

  const auto platform_font =
      font->getPlatformFont().cast<VSTGUI::CoreTextFont>();
  if (!platform_font) {
    return {};
  }

  const auto* const bytes = reinterpret_cast<const UInt8*>(text.data());
  const auto string = AdoptCF(CFStringCreateWithBytes(
      kCFAllocatorDefault, bytes, static_cast<CFIndex>(text.size()),
      kCFStringEncodingUTF8, false));
  if (!string) {
    return {};
  }

  const auto utf16_to_utf8 = BuildUtf16ToUtf8BoundaryMap(text);
  if (!utf16_to_utf8) {
    return {};
  }
  const auto utf16_length = CFStringGetLength(string.get());

  // VSTGUI の実描画と同じフォント属性を使い、折り返し判定時と描画時の
  // 字形・幅を一致させる。
  const void* keys[] = {kCTFontAttributeName};
  const void* values[] = {platform_font->getFontRef()};
  const auto attributes = AdoptCF(CFDictionaryCreate(
      kCFAllocatorDefault, keys, values, 1,
      &kCFCopyStringDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  if (!attributes) {
    return {};
  }

  const auto attributed = AdoptCF(CFAttributedStringCreate(
      kCFAllocatorDefault, string.get(), attributes.get()));
  if (!attributed) {
    return {};
  }
  const auto typesetter =
      AdoptCF(CTTypesetterCreateWithAttributedString(attributed.get()));
  if (!typesetter) {
    return {};
  }

  auto line_ends = std::vector<std::size_t>{};
  auto start = CFIndex{0};
  while (start < utf16_length) {
    auto length =
        CTTypesetterSuggestLineBreak(typesetter.get(), start, max_width);
    const auto remaining = utf16_length - start;
    auto use_emergency_break = length <= 0 || length > remaining;
    if (!use_emergency_break) {
      const auto line_width =
          MeasureTypesetterLine(typesetter.get(), start, length);
      if (!line_width || !std::isfinite(*line_width)) {
        return {};
      }
      use_emergency_break = *line_width > max_width;
    }
    if (use_emergency_break) {
      length = SuggestEmergencyLineLength(typesetter.get(), string.get(), start,
                                          max_width);
    }
    if (length <= 0 || length > remaining) {
      return {};
    }

    start += length;
    if (!AppendMappedLineEnd(*utf16_to_utf8, static_cast<std::size_t>(start),
                             line_ends)) {
      return {};
    }
  }
  return line_ends;
}

#endif

// OS の文字処理を使って本文を折り返す。
[[nodiscard]] auto WrapDescriptionText(std::u8string_view text,
                                       const double max_width,
                                       VSTGUI::CFontDesc* font)
    -> std::u8string {
  const auto layout_lines = [font](const std::u8string_view paragraph,
                                   const double width) -> TextLineEnds {
    return LayoutNativeDescriptionLines(paragraph, width, font);
  };
  return LayoutDescriptionText(text, max_width, layout_lines);
}

}  // namespace

void SetScrollableDescription(CScrollView* const scroll,
                              CMultiLineTextLabel* const label,
                              const std::u8string& text) {
  if (!scroll || !label) {
    return;
  }
  const auto viewport = scroll->getViewSize();
  // スクロールバーとの視覚的な右余白。
  constexpr auto kTextRightPadding = 6.0;
  // 極端に狭い表示を避けるための本文幅の下限。
  constexpr auto kMinimumTextWidth = 20.0;
  scroll->resetScrollOffset();

  auto content_width = viewport.getWidth();
  // 現在の本文幅に合わせて折り返し直し、ラベルに必要な高さを返す。
  const auto measure_height = [&]() -> VSTGUI::CCoord {
    const auto text_width =
        std::max(kMinimumTextWidth, content_width - kTextRightPadding);
    label->setViewSize(CRect(0, 0, text_width, viewport.getHeight()));
    const auto inset = label->getTextInset();
    const auto max_line_width = text_width - (inset.x * 2.0);
    const auto wrapped =
        WrapDescriptionText(text, max_line_width, label->getFont());
    label->setText(ToVstGuiString(wrapped));
    label->setAutoHeight(true);
    const auto height = label->getViewSize().getHeight();
    label->setAutoHeight(false);
    return height;
  };
  auto content_height = measure_height();
  if (content_height > viewport.getHeight()) {
    content_width -= scroll->getScrollbarWidth();
    content_height = measure_height();
  }
  content_height = std::max(viewport.getHeight(), content_height);
  label->setViewSize(CRect(
      0, 0, std::max(kMinimumTextWidth, content_width - kTextRightPadding),
      content_height));
  label->setMouseableArea(label->getViewSize());
  scroll->setContainerSize(CRect(0, 0, content_width, content_height));
  scroll->setDirty();
  label->setDirty();
}

}  // namespace beatrice::vst
