// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_DESCRIPTION_URL_H_
#define BEATRICE_VST_DESCRIPTION_URL_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace beatrice::vst {

// 折り返し済み本文内の UTF-8 バイト範囲。
struct DescriptionTextRange {
  std::size_t begin = 0;
  std::size_t end = 0;
};

// ブラウザへ渡す URL と、各表示行に分かれた範囲。
struct DescriptionLink {
  std::u8string url;
  std::vector<DescriptionTextRange> fragments;
};

// ブラウザ起動経路へ安全に渡せる ASCII の HTTP(S) URL かを返す。
// パーセントエンコード、host、port、path、query、fragment の詳細な構文は
// 検証しない。
[[nodiscard]] auto IsSafeDescriptionUrl(std::u8string_view url) -> bool;

namespace description_url_detail {

// 折り返し済み本文から、リンク先と各行の表示範囲を検出する。
// joinable_breaks は URL の復元時に除く折り返し改行のバイト位置を表す。
[[nodiscard]] auto FindDescriptionLinks(
    std::u8string_view text, const std::vector<std::size_t>& joinable_breaks)
    -> std::vector<DescriptionLink>;

}  // namespace description_url_detail
}  // namespace beatrice::vst

#endif  // BEATRICE_VST_DESCRIPTION_URL_H_
