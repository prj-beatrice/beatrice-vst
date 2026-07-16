// Copyright (c) 2024-2026 Project Beatrice and Contributors

#ifndef BEATRICE_VST_SURFACE_TEXTURE_H_
#define BEATRICE_VST_SURFACE_TEXTURE_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "vst3sdk/vstgui4/vstgui/lib/cbitmap.h"
#include "vst3sdk/vstgui4/vstgui/lib/ccolor.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawcontext.h"
#include "vst3sdk/vstgui4/vstgui/lib/cdrawdefs.h"
#include "vst3sdk/vstgui4/vstgui/lib/cgraphicspath.h"
#include "vst3sdk/vstgui4/vstgui/lib/cpoint.h"
#include "vst3sdk/vstgui4/vstgui/lib/crect.h"
#include "vst3sdk/vstgui4/vstgui/lib/cviewcontainer.h"
#include "vst3sdk/vstgui4/vstgui/lib/vstguibase.h"

namespace beatrice::vst {

using VSTGUI::CBitmap;
using VSTGUI::CBitmapPixelAccess;
using VSTGUI::CColor;
using VSTGUI::CCoord;
using VSTGUI::CDrawContext;
using VSTGUI::CGraphicsPath;
using VSTGUI::CPoint;
using VSTGUI::CRect;
using VSTGUI::CViewContainer;
using VSTGUI::kAntiAliasing;
using VSTGUI::kDrawStroked;
using VSTGUI::kTransparentCColor;
using VSTGUI::SharedPointer;

struct SurfaceTextureParams {
  CColor base;
  float low_frequency_strength = 1.0f;
  float fine_grain_strength = 1.0f;
  float baked_grain_strength = 1.0f;
};

struct SurfaceNoiseParams {
  int grain_cells = 64;
  int seed = 4217;
  int base_cells = 2;
  int octaves = 3;
  float persistence = 0.34f;
};

class SurfaceNoiseMaps {
 public:
  explicit SurfaceNoiseMaps(const SurfaceNoiseParams& params,
                            const int tile_period = 512)
      : tile_period_(tile_period) {
    const auto n = static_cast<std::size_t>(tile_period_) *
                   static_cast<std::size_t>(tile_period_);
    low_.resize(n);
    grain_.resize(n);
    baked_.resize(n);
    dither_r_.resize(n);
    dither_g_.resize(n);
    dither_b_.resize(n);

    auto low_grids = std::vector<TileGrid>{};
    auto cells = params.base_cells;
    low_grids.reserve(params.octaves);
    for (auto octave = 0; octave < params.octaves; ++octave) {
      low_grids.push_back(
          BuildTileGrid(cells, params.seed + octave * 97, tile_period_));
      cells *= 2;
    }
    const auto grain_grid =
        params.grain_cells > 0 ? BuildTileGrid(params.grain_cells,
                                               params.seed + 1309, tile_period_)
                               : TileGrid{};
    const auto baked_grid = BuildTileGrid(96, params.seed + 9137, tile_period_);

    for (auto y = 0; y < tile_period_; ++y) {
      for (auto x = 0; x < tile_period_; ++x) {
        const auto index = Index(x, y);
        low_[index] = FractalNoise(low_grids, x, y, params) - 0.5f;
        grain_[index] =
            params.grain_cells > 0 ? TileNoise(grain_grid, x, y) - 0.5f : 0.0f;
        baked_[index] = (Hash01(x, y, params.seed + 9127) - 0.5f) +
                        (TileNoise(baked_grid, x, y) - 0.5f);
        dither_r_[index] = Hash01(x, y, params.seed + 8811);
        dither_g_[index] = Hash01(x, y, params.seed + 8812);
        dither_b_[index] = Hash01(x, y, params.seed + 8813);
      }
    }
  }

  [[nodiscard]] auto Index(const int x, const int y) const -> std::size_t {
    return static_cast<std::size_t>(y % tile_period_) * tile_period_ +
           static_cast<std::size_t>(x % tile_period_);
  }

  [[nodiscard]] auto Low(const std::size_t index) const -> float {
    return low_[index];
  }

  [[nodiscard]] auto Grain(const std::size_t index) const -> float {
    return grain_[index];
  }

  [[nodiscard]] auto Baked(const std::size_t index) const -> float {
    return baked_[index];
  }

  [[nodiscard]] auto DitherR(const std::size_t index) const -> float {
    return dither_r_[index];
  }

  [[nodiscard]] auto DitherG(const std::size_t index) const -> float {
    return dither_g_[index];
  }

  [[nodiscard]] auto DitherB(const std::size_t index) const -> float {
    return dither_b_[index];
  }

 private:
  struct AxisSample {
    int index0 = 0;
    int index1 = 0;
    float t = 0.0f;
  };

  struct TileGrid {
    int cells = 0;
    std::vector<float> values;
    std::vector<AxisSample> x_samples;
    std::vector<AxisSample> y_samples;
  };

  int tile_period_;
  std::vector<float> low_;
  std::vector<float> grain_;
  std::vector<float> baked_;
  std::vector<float> dither_r_;
  std::vector<float> dither_g_;
  std::vector<float> dither_b_;

  static auto Hash01(int x, int y, int seed) -> float {
    auto n = (static_cast<uint32_t>(x) * 0x1F123BB5u) ^
             (static_cast<uint32_t>(y) * 0x5F356495u) ^
             (static_cast<uint32_t>(seed) * 0x9E3779B9u);
    n ^= n >> 16u;
    n *= 0x7FEB352Du;
    n ^= n >> 15u;
    n *= 0x846CA68Bu;
    n ^= n >> 16u;
    return static_cast<float>(n) / 4294967296.0f;
  }

  static auto SmoothStep(float t) -> float { return t * t * (3.0f - 2.0f * t); }

  static auto Lerp(float a, float b, float t) -> float {
    return a + (b - a) * t;
  }

  static auto BuildAxisSamples(const int cells, const int tile_period)
      -> std::vector<AxisSample> {
    auto samples = std::vector<AxisSample>{};
    samples.reserve(tile_period);
    for (auto i = 0; i < tile_period; ++i) {
      const auto value =
          static_cast<float>(i) / static_cast<float>(tile_period) * cells;
      auto index0 = static_cast<int>(std::floor(value));
      const auto t = SmoothStep(value - std::floor(value));
      const auto index1 = (index0 + 1) % cells;
      index0 %= cells;
      samples.push_back(AxisSample{.index0 = index0, .index1 = index1, .t = t});
    }
    return samples;
  }

  static auto BuildTileGrid(const int cells, const int seed,
                            const int tile_period) -> TileGrid {
    auto grid = TileGrid{.cells = cells};
    grid.values.resize(static_cast<std::size_t>(cells) * cells);
    for (auto y = 0; y < cells; ++y) {
      for (auto x = 0; x < cells; ++x) {
        grid.values[static_cast<std::size_t>(y) * cells + x] =
            Hash01(x, y, seed);
      }
    }
    grid.x_samples = BuildAxisSamples(cells, tile_period);
    grid.y_samples = BuildAxisSamples(cells, tile_period);
    return grid;
  }

  static auto TileNoise(const TileGrid& grid, const int x, const int y)
      -> float {
    const auto& xs = grid.x_samples[x];
    const auto& ys = grid.y_samples[y];
    const auto row0 = static_cast<std::size_t>(ys.index0) * grid.cells;
    const auto row1 = static_cast<std::size_t>(ys.index1) * grid.cells;
    const auto a = grid.values[row0 + xs.index0];
    const auto b = grid.values[row0 + xs.index1];
    const auto c = grid.values[row1 + xs.index0];
    const auto d = grid.values[row1 + xs.index1];
    const auto tx = xs.t;
    const auto ty = ys.t;
    return Lerp(Lerp(a, b, tx), Lerp(c, d, tx), ty);
  }

  static auto FractalNoise(const std::vector<TileGrid>& grids, const int x,
                           const int y, const SurfaceNoiseParams& params)
      -> float {
    auto total = 0.0f;
    auto amplitude = 1.0f;
    auto amplitude_sum = 0.0f;
    for (const auto& grid : grids) {
      total += TileNoise(grid, x, y) * amplitude;
      amplitude_sum += amplitude;
      amplitude *= params.persistence;
    }
    return total / amplitude_sum;
  }
};

class SurfaceBitmap : public CBitmap {
 public:
  SurfaceBitmap(const SurfaceTextureParams& params,
                std::shared_ptr<const SurfaceNoiseMaps> noise_maps,
                int width = 512, int height = 512)
      : CBitmap(width, height),
        width_(width),
        height_(height),
        noise_maps_(std::move(noise_maps)) {
    Fill(params);
  }

  void draw(CDrawContext* const context, const CRect& rect,
            const CPoint& offset, const float alpha) override {
    const auto width = getWidth();
    const auto height = getHeight();
    const auto rows = static_cast<int>(
        std::ceil((rect.bottom - rect.top) / static_cast<double>(height)));
    const auto columns = static_cast<int>(
        std::ceil((rect.right - rect.left) / static_cast<double>(width)));
    for (auto row = 0; row < rows; ++row) {
      const auto y = rect.top + static_cast<double>(row) * height;
      for (auto column = 0; column < columns; ++column) {
        const auto x = rect.left + static_cast<double>(column) * width;
        auto tile = CRect(x, y, std::min(x + width, rect.right),
                          std::min(y + height, rect.bottom));
        CBitmap::draw(context, tile, offset, alpha);
      }
    }
  }

 private:
  int width_;
  int height_;
  std::shared_ptr<const SurfaceNoiseMaps> noise_maps_;

  static auto Quantize(float value, float dither) -> uint8_t {
    return static_cast<uint8_t>(
        std::clamp(std::floor(value + dither), 0.0f, 255.0f));
  }

  void Fill(const SurfaceTextureParams& params) {
    auto access = VSTGUI::owned(CBitmapPixelAccess::create(this, false));
    if (!access) {
      return;
    }
    auto* const pixel_access = access->getPlatformBitmapPixelAccess();
    if (!pixel_access) {
      return;
    }
    auto* const address = pixel_access->getAddress();
    const auto bytes_per_row = pixel_access->getBytesPerRow();
    auto red_offset = 0;
    auto green_offset = 1;
    auto blue_offset = 2;
    auto alpha_offset = 3;
    switch (pixel_access->getPixelFormat()) {
      case VSTGUI::IPlatformBitmapPixelAccess::kARGB:
        red_offset = 1;
        green_offset = 2;
        blue_offset = 3;
        alpha_offset = 0;
        break;
      case VSTGUI::IPlatformBitmapPixelAccess::kRGBA:
        red_offset = 0;
        green_offset = 1;
        blue_offset = 2;
        alpha_offset = 3;
        break;
      case VSTGUI::IPlatformBitmapPixelAccess::kABGR:
        red_offset = 3;
        green_offset = 2;
        blue_offset = 1;
        alpha_offset = 0;
        break;
      case VSTGUI::IPlatformBitmapPixelAccess::kBGRA:
        red_offset = 2;
        green_offset = 1;
        blue_offset = 0;
        alpha_offset = 3;
        break;
    }
    const auto& maps = *noise_maps_;
    for (auto y = 0; y < height_; ++y) {
      auto* const row = address + static_cast<size_t>(y) * bytes_per_row;
      for (auto x = 0; x < width_; ++x) {
        const auto index = maps.Index(x, y);
        const auto value = maps.Low(index) * params.low_frequency_strength +
                           maps.Grain(index) * params.fine_grain_strength +
                           maps.Baked(index) * params.baked_grain_strength;
        const auto r = Quantize(static_cast<float>(params.base.red) + value,
                                maps.DitherR(index));
        const auto g = Quantize(static_cast<float>(params.base.green) + value,
                                maps.DitherG(index));
        const auto b = Quantize(static_cast<float>(params.base.blue) + value,
                                maps.DitherB(index));
        auto* const pixel = row + static_cast<size_t>(x) * 4;
        pixel[red_offset] = r;
        pixel[green_offset] = g;
        pixel[blue_offset] = b;
        pixel[alpha_offset] = 255;
      }
    }
  }
};

class SurfacePanel : public CViewContainer {
 public:
  SurfacePanel(const CRect& size, const SharedPointer<SurfaceBitmap>& texture,
               const CColor& border = CColor(0xff, 0xff, 0xff, 0x10),
               CCoord radius = 2.0)
      : CViewContainer(size),
        texture_(texture),
        border_(border),
        radius_(radius) {}

  void drawBackgroundRect(CDrawContext* const context,
                          const CRect& /*update_rect*/) override {
    auto rect = getViewSize();
    rect.offset(-rect.left, -rect.top);
    if (texture_) {
      texture_->draw(context, rect, CPoint(0, 0), 1.0f);
    }
    context->setDrawMode(kAntiAliasing);
    context->setLineWidth(1);
    context->setFrameColor(border_);
    context->setFillColor(kTransparentCColor);
    auto stroke = rect;
    stroke.inset(0.5, 0.5);
    if (radius_ > 0.0) {
      if (auto* const path =
              context->createRoundRectGraphicsPath(stroke, radius_)) {
        context->drawGraphicsPath(path, CDrawContext::kPathStroked);
        path->forget();
      }
    } else {
      context->drawRect(stroke, kDrawStroked);
    }
  }

 private:
  SharedPointer<SurfaceBitmap> texture_;
  CColor border_;
  CCoord radius_;
};

}  // namespace beatrice::vst

#endif  // BEATRICE_VST_SURFACE_TEXTURE_H_
