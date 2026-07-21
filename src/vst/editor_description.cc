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
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <cstdint>
#elif defined(__APPLE__)
#include <memory>
#include <type_traits>
#endif

#include "vst3sdk/vstgui4/vstgui/lib/cfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/cframe.h"
#include "vst3sdk/vstgui4/vstgui/lib/controls/ctextlabel.h"
#include "vst3sdk/vstgui4/vstgui/lib/cstring.h"
#include "vst3sdk/vstgui4/vstgui/lib/events.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/iplatformfont.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguifwd.h"

#if defined(_WIN32)
#include "vst3sdk/vstgui4/vstgui/lib/platform/win32/comptr.h"
#include "vst3sdk/vstgui4/vstgui/lib/platform/win32/direct2d/d2dfont.h"
#elif defined(__APPLE__)
#include "vst3sdk/vstgui4/vstgui/lib/platform/mac/cfontmac.h"
#endif

// Beatrice
#include "vst/description_text_layout.h"
#include "vst/surface_texture.h"

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
    -> DescriptionTextLayout {
  const auto layout_lines = [font](const std::u8string_view paragraph,
                                   const double width) -> TextLineEnds {
    return LayoutNativeDescriptionLines(paragraph, width, font);
  };
  return LayoutDescriptionText(text, max_width, layout_lines);
}

}  // namespace

DescriptionTextLabel::DescriptionTextLabel(const CRect& rect,
                                           OpenUrlAction open_url)
    : CMultiLineTextLabel(rect), open_url_(std::move(open_url)) {}

void DescriptionTextLabel::SetDescriptionLayout(DescriptionTextLayout layout) {
  layout_ = std::move(layout);
  setText(ToVstGuiString(layout_.text));
  RebuildLinkAreas();
}

void DescriptionTextLabel::drawRect(CDrawContext* const context,
                                    const CRect& update_rect) {
  CMultiLineTextLabel::drawRect(context, update_rect);
  if (link_areas_.empty()) {
    return;
  }

  context->saveGlobalState();
  context->setDrawMode(kAntiAliasing);
  context->setFrameColor(getFontColor());
  context->setLineWidth(1.0);
  const auto origin = getViewSize().getTopLeft();
  const auto inset = getTextInset();
  for (const auto& area : link_areas_) {
    auto rect = area.rect;
    rect.offset(origin.x, origin.y);
    if (rect.rectOverlap(update_rect)) {
      const auto underline_y = rect.bottom - inset.y - context->getLineWidth();
      context->drawLine(CPoint(rect.left, underline_y),
                        CPoint(rect.right, underline_y));
    }
  }
  context->restoreGlobalState();
}

void DescriptionTextLabel::onMouseDownEvent(VSTGUI::MouseDownEvent& event) {
  if (!event.consumed && event.buttonState.isLeft()) {
    pressed_link_ = LinkAt(event.mousePosition);
    if (pressed_link_) {
      event.consumed = true;
      return;
    }
  }
  CMultiLineTextLabel::onMouseDownEvent(event);
}

void DescriptionTextLabel::onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) {
  const auto over_link = LinkAt(event.mousePosition).has_value();
  UpdateCursor(over_link);
  if (over_link || pressed_link_) {
    event.consumed = true;
    return;
  }
  CMultiLineTextLabel::onMouseMoveEvent(event);
}

void DescriptionTextLabel::onMouseUpEvent(VSTGUI::MouseUpEvent& event) {
  if (!pressed_link_) {
    CMultiLineTextLabel::onMouseUpEvent(event);
    return;
  }

  const auto pressed_link = std::exchange(pressed_link_, std::nullopt);
  const auto released_link = LinkAt(event.mousePosition);
  UpdateCursor(released_link.has_value());
  event.consumed = true;
  if (released_link != pressed_link) {
    return;
  }

  const auto url = layout_.links[*pressed_link].url;
  const auto open_url = open_url_;
  open_url(url);
}

void DescriptionTextLabel::onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) {
  if (pressed_link_) {
    pressed_link_.reset();
    event.consumed = true;
  }
}

void DescriptionTextLabel::onMouseExitEvent(VSTGUI::MouseExitEvent& event) {
  if (auto* const frame = getFrame()) {
    frame->setCursor(VSTGUI::kCursorDefault);
  }
  CMultiLineTextLabel::onMouseExitEvent(event);
}

void DescriptionTextLabel::RebuildLinkAreas() {
  pressed_link_.reset();
  link_areas_.clear();
  setDirty();
  auto* const font = getFont();
  if (!font || layout_.links.empty()) {
    return;
  }
  const auto platform_font = font->getPlatformFont();
  const auto* const painter = font->getFontPainter();
  if (!platform_font || !painter) {
    return;
  }

  const auto line_height = platform_font->getAscent() +
                           platform_font->getDescent() +
                           platform_font->getLeading();
  if (!std::isfinite(line_height) || line_height <= 0.0) {
    return;
  }

  const auto inset = getTextInset();
  auto line_starts = std::vector<std::size_t>{0};
  for (auto pos = std::size_t{0}; pos < layout_.text.size(); ++pos) {
    if (layout_.text[pos] == u8'\n') {
      line_starts.push_back(pos + 1);
    }
  }
  for (auto link_index = std::size_t{0}; link_index < layout_.links.size();
       ++link_index) {
    const auto& link = layout_.links[link_index];
    for (const auto& fragment : link.fragments) {
      if (fragment.begin >= fragment.end ||
          fragment.end > layout_.text.size()) {
        continue;
      }
      const auto next_line =
          std::ranges::upper_bound(line_starts, fragment.begin);
      const auto line_index = static_cast<std::size_t>(
          std::distance(line_starts.begin(), next_line) - 1);
      const auto line_start = line_starts[line_index];
      const auto line_end = line_index + 1 < line_starts.size()
                                ? line_starts[line_index + 1] - 1
                                : layout_.text.size();
      if (fragment.end > line_end) {
        continue;
      }

      const auto prefix =
          layout_.text.substr(line_start, fragment.begin - line_start);
      const auto through_link =
          layout_.text.substr(line_start, fragment.end - line_start);
      const auto prefix_string = ToVstGuiString(prefix);
      const auto through_link_string = ToVstGuiString(through_link);
      const auto left = painter->getStringWidth(
          nullptr, prefix_string.getPlatformString(), true);
      const auto right = painter->getStringWidth(
          nullptr, through_link_string.getPlatformString(), true);
      if (!std::isfinite(left) || !std::isfinite(right) || right <= left) {
        continue;
      }

      const auto top =
          inset.y + (static_cast<double>(line_index) * line_height);
      link_areas_.push_back({.rect = CRect(inset.x + left, top, inset.x + right,
                                           top + line_height + inset.y),
                             .link_index = link_index});
    }
  }
}

auto DescriptionTextLabel::LinkAt(const CPoint& position) const
    -> std::optional<std::size_t> {
  auto local_position = position;
  local_position -= getViewSize().getTopLeft();
  for (const auto& area : link_areas_) {
    if (area.rect.pointInside(local_position)) {
      return area.link_index;
    }
  }
  return std::nullopt;
}

void DescriptionTextLabel::UpdateCursor(const bool over_link) {
  if (auto* const frame = getFrame()) {
    frame->setCursor(over_link ? VSTGUI::kCursorPointingHand
                               : VSTGUI::kCursorDefault);
  }
}

void SetScrollableDescription(CScrollView* const scroll,
                              DescriptionTextLabel* const label,
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
    auto wrapped = WrapDescriptionText(text, max_line_width, label->getFont());
    label->SetDescriptionLayout(std::move(wrapped));
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
