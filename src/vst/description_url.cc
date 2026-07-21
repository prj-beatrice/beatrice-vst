// Copyright (c) 2024-2026 Project Beatrice and Contributors

// URL の検出・検証方針:
//
// RFC 3986 に基づく処理:
// - 予約文字・非予約文字の分類は RFC 3986 §2.2・§2.3 に従う。
// - scheme の ASCII 大文字・小文字を区別しない（RFC 3986 §3.1）。
// - authority は `/`、`?`、`#`、または文字列末尾までとする（RFC 3986 §3.2）。
//
// この実装の方針:
// - 対応 scheme は HTTP(S) に限定し、userinfo は受理しない。
// - macOS/Linux では VST3 SDK が URL をシェルコマンドに埋め込むため、
//   シェルに解釈される `$` は受理しない。
// - パーセントエンコード、host、port、path、query、fragment の詳細な構文は
//   検証しない。
// - 本文中の境界と末尾処理は GFM §6.9 を参考にした独自規則とする。
//
// 出典:
// [RFC 3986] https://www.rfc-editor.org/rfc/rfc3986
// [GFM §6.9] https://github.github.com/gfm/#autolinks-extension-
// [Unicode 17] https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt

#include "vst/description_url.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Beatrice
#include "vst/description_text_utf8.h"

namespace beatrice::vst {
namespace {

constexpr auto kHttp = std::u8string_view{u8"http://"};
constexpr auto kHttps = std::u8string_view{u8"https://"};

// ASCII 英字を大文字・小文字を区別せず比較する。
[[nodiscard]] auto EqualAsciiIgnoreCase(const char8_t lhs, const char8_t rhs)
    -> bool {
  constexpr auto kCaseDifference = u8'a' - u8'A';
  const auto lower = [](const char8_t value) -> char8_t {
    return value >= u8'A' && value <= u8'Z'
               ? static_cast<char8_t>(value + kCaseDifference)
               : value;
  };
  return lower(lhs) == lower(rhs);
}

// ASCII 文字列を大文字・小文字を区別せず前方一致で比較する。
[[nodiscard]] auto StartsWithAsciiIgnoreCase(const std::u8string_view text,
                                             const std::u8string_view expected)
    -> bool {
  return expected.size() <= text.size() &&
         std::ranges::equal(text.substr(0, expected.size()), expected,
                            EqualAsciiIgnoreCase);
}

// ASCII の英数字かを確認する。
[[nodiscard]] auto IsAsciiAlphanumeric(const char8_t value) -> bool {
  return (value >= u8'a' && value <= u8'z') ||
         (value >= u8'A' && value <= u8'Z') ||
         (value >= u8'0' && value <= u8'9');
}

// RFC 3986 §2.3 の非予約文字かを返す。
[[nodiscard]] auto IsUriUnreserved(const char8_t value) -> bool {
  return IsAsciiAlphanumeric(value) ||
         std::u8string_view{u8"-._~"}.find(value) != std::u8string_view::npos;
}

// RFC 3986 §2.2 の sub-delims かを返す。
[[nodiscard]] auto IsUriSubDelimiter(const char8_t value) -> bool {
  return std::u8string_view{u8"!$&'()*+,;="}.find(value) !=
         std::u8string_view::npos;
}

// RFC 3986 の文字分類を基準に、URL 候補として読み取る ASCII 文字かを返す。
[[nodiscard]] auto IsUriTokenByte(const char8_t value) -> bool {
  constexpr auto kUriGeneralDelimitersAndPercent =
      std::u8string_view{u8":/?#[]@%"};
  return IsUriUnreserved(value) || IsUriSubDelimiter(value) ||
         kUriGeneralDelimitersAndPercent.find(value) !=
             std::u8string_view::npos;
}

// VST3 SDK のブラウザ起動処理は、macOS/Linux では URL を `system()` に
// 渡すコマンドの二重引用符内へ埋め込む。
// `$` は URI では有効だがシェル展開されるため、安全な文字とはみなさない。
[[nodiscard]] auto IsLaunchSafeUrlByte(const char8_t value) -> bool {
  return value != u8'$' && IsUriTokenByte(value);
}

// ASCII 空白文字かを返す。
[[nodiscard]] auto IsAsciiWhitespace(const char8_t value) -> bool {
  return value == u8' ' || value == u8'\t' || value == u8'\n' ||
         value == u8'\r' || value == u8'\f' || value == u8'\v';
}

// URL の開始位置として扱える文章上の境界かを返す。
[[nodiscard]] auto IsDescriptionLinkStart(const std::u8string_view text,
                                          const std::size_t pos) -> bool {
  if (pos == 0) {
    return true;
  }
  const auto previous = text[pos - 1];
  if (IsAsciiWhitespace(previous)) {
    return true;
  }
  if (static_cast<unsigned char>(previous) >= 0x80U) {
    // 日本語本文では、非 ASCII 文字の直後も URL の開始境界になる。
    return true;
  }
  // GFM §6.9 で定められた開始境界に加え、`[`、`<`、`:`、`"` の直後も
  // URL の開始境界とする。
  constexpr auto kOpeningDelimiters = std::u8string_view{u8"([<:\"*_~"};
  return kOpeningDelimiters.find(previous) != std::u8string_view::npos;
}

// ASCII URL の直後で本文の区切りとなる非 ASCII 文字かを返す。
[[nodiscard]] auto IsJapaneseDescriptionUrlTerminator(
    const std::u8string_view text, const std::size_t pos) -> bool {
  const auto length = description_text_detail::GetUtf8SequenceLength(text, pos);
  if (!length) {
    return false;
  }
  // UnicodeData の空白・閉じ記号・句読点（Zs、Pe、Pf、Po）のうち、
  // DESCRIPTION で URL の区切りとする文字。
  constexpr auto kTerminators = std::u8string_view{
      u8"»’”‥…　、。〉》」』】〕〗〙〛〞〟・"
      u8"！），．：；？］｝｡｣､･"};
  return kTerminators.find(text.substr(pos, *length)) !=
         std::u8string_view::npos;
}

// authority に host があり、userinfo がないことを確認する。
// port や IP-literal の詳細な構文検証は、この関数では行わない。
[[nodiscard]] auto HasDescriptionUrlHost(const std::u8string_view authority)
    -> bool {
  if (authority.empty() || authority.find(u8'@') != std::u8string_view::npos) {
    // userinfo は host の誤認を招くため受理しない。
    return false;
  }

  if (authority.front() == u8'[') {
    const auto closing_bracket = authority.find(u8']');
    if (closing_bracket == std::u8string_view::npos || closing_bracket == 1 ||
        authority.find(u8'[', 1) != std::u8string_view::npos) {
      return false;
    }
    const auto suffix = authority.substr(closing_bracket + 1);
    return suffix.empty() || suffix.front() == u8':';
  }

  if (authority.find_first_of(u8"[]") != std::u8string_view::npos) {
    return false;
  }

  // URL 全体が起動経路で許可する文字だけであることは確認済み。
  const auto host = authority.substr(0, authority.find(u8':'));
  return std::ranges::any_of(host, [](const char8_t value) -> bool {
    return IsAsciiAlphanumeric(value) || value == u8'%';
  });
}

// URL 末尾から文章側の記号を除く。
void TrimUrlEnd(std::u8string& url) {
  // GFM §6.9 では文末記号と対応しない末尾の `)` を除く。
  // ここでは、対応しない末尾の `]` も除く。
  constexpr auto kTrailingPunctuation = std::u8string_view{u8"?!.,:*_~"};
  const auto opening_parentheses = std::count(url.begin(), url.end(), u8'(');
  auto closing_parentheses = std::count(url.begin(), url.end(), u8')');
  const auto opening_brackets = std::count(url.begin(), url.end(), u8'[');
  auto closing_brackets = std::count(url.begin(), url.end(), u8']');
  while (!url.empty()) {
    if (kTrailingPunctuation.find(url.back()) != std::u8string_view::npos) {
      url.pop_back();
      continue;
    }
    if (url.back() == u8')' && closing_parentheses > opening_parentheses) {
      url.pop_back();
      --closing_parentheses;
      continue;
    }
    if (url.back() == u8']' && closing_brackets > opening_brackets) {
      url.pop_back();
      --closing_brackets;
      continue;
    }
    break;
  }
}

}  // namespace

auto IsSafeDescriptionUrl(const std::u8string_view url) -> bool {
  auto scheme_size = std::size_t{0};
  if (StartsWithAsciiIgnoreCase(url, kHttps)) {
    scheme_size = kHttps.size();
  } else if (StartsWithAsciiIgnoreCase(url, kHttp)) {
    scheme_size = kHttp.size();
  } else {
    return false;
  }

  if (!std::ranges::all_of(url, IsLaunchSafeUrlByte)) {
    return false;
  }

  const auto authority_end = url.find_first_of(u8"/?#", scheme_size);
  const auto authority =
      url.substr(scheme_size, authority_end == std::u8string_view::npos
                                  ? url.size() - scheme_size
                                  : authority_end - scheme_size);
  return HasDescriptionUrlHost(authority);
}

namespace description_url_detail {

auto FindDescriptionLinks(const std::u8string_view text,
                          const std::vector<std::size_t>& joinable_breaks)
    -> std::vector<DescriptionLink> {
  auto joined_text = std::u8string{};
  auto display_positions = std::vector<std::size_t>{};
  joined_text.reserve(text.size());
  display_positions.reserve(text.size());
  auto break_index = std::size_t{0};
  for (auto display_pos = std::size_t{0}; display_pos < text.size();
       ++display_pos) {
    if (break_index < joinable_breaks.size() &&
        display_pos == joinable_breaks[break_index]) {
      ++break_index;
      continue;
    }
    joined_text.push_back(text[display_pos]);
    display_positions.push_back(display_pos);
  }

  auto links = std::vector<DescriptionLink>{};
  auto pos = std::size_t{0};
  while (pos < joined_text.size()) {
    const auto remaining_text = std::u8string_view{joined_text}.substr(pos);
    if (!IsDescriptionLinkStart(joined_text, pos) ||
        (!StartsWithAsciiIgnoreCase(remaining_text, kHttps) &&
         !StartsWithAsciiIgnoreCase(remaining_text, kHttp))) {
      ++pos;
      continue;
    }

    auto scan_pos = pos;
    while (scan_pos < joined_text.size() &&
           IsUriTokenByte(joined_text[scan_pos])) {
      ++scan_pos;
    }

    auto url = std::u8string{
        std::u8string_view{joined_text}.substr(pos, scan_pos - pos)};
    TrimUrlEnd(url);
    const auto followed_by_unsupported_non_ascii =
        scan_pos < joined_text.size() &&
        static_cast<unsigned char>(joined_text[scan_pos]) >= 0x80U &&
        !IsJapaneseDescriptionUrlTerminator(joined_text, scan_pos);
    if (!followed_by_unsupported_non_ascii && IsSafeDescriptionUrl(url)) {
      auto fragments = std::vector<DescriptionTextRange>{};
      for (auto link_pos = pos; link_pos < pos + url.size(); ++link_pos) {
        const auto display_pos = display_positions[link_pos];
        if (fragments.empty() || fragments.back().end != display_pos) {
          fragments.push_back({.begin = display_pos, .end = display_pos + 1});
        } else {
          fragments.back().end = display_pos + 1;
        }
      }
      links.push_back(
          {.url = std::move(url), .fragments = std::move(fragments)});
    }
    pos = scan_pos;
  }
  return links;
}

}  // namespace description_url_detail
}  // namespace beatrice::vst
