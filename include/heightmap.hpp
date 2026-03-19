#pragma once
/**
 * heightmap.hpp
 * ─────────────────────────────────────────────────────────────────
 * 2-D floating-point heightmap with boundary-safe accessors, gradient
 * query, blur, and thermal-erosion pass.
 *
 * Heights are stored as float in [0, 1] (or slightly beyond during
 * active erosion).  The grid is row-major: index(x,y) = y*width + x.
 * ─────────────────────────────────────────────────────────────────
 */

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstdint>

#include "noise.hpp"

// ─── Heightmap ───────────────────────────────────────────────────
class Heightmap {
public:
    // ── Construction ─────────────────────────────────────────────
    Heightmap() = default;
    Heightmap(int width, int height) : width_(width), height_(height) {
        data_.resize(static_cast<size_t>(width * height), 0.f);
    }

    // ── Dimensions ───────────────────────────────────────────────
    int   width()  const noexcept { return width_; }
    int   height() const noexcept { return height_; }
    bool  valid()  const noexcept { return !data_.empty(); }

    // ── Raw data access ──────────────────────────────────────────
    float*       data()       noexcept { return data_.data(); }
    const float* data() const noexcept { return data_.data(); }

    // ── Element access (clamped to borders) ───────────────────────
    float& at(int x, int y) {
        x = std::clamp(x, 0, width_  - 1);
        y = std::clamp(y, 0, height_ - 1);
        return data_[static_cast<size_t>(y * width_ + x)];
    }
    float at(int x, int y) const {
        x = std::clamp(x, 0, width_  - 1);
        y = std::clamp(y, 0, height_ - 1);
        return data_[static_cast<size_t>(y * width_ + x)];
    }

    // Bilinear-interpolated height at a fractional position
    float sampleBilinear(float fx, float fy) const {
        int   x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
        float tx = fx - x0, ty = fy - y0;
        float h00 = at(x0,     y0    );
        float h10 = at(x0 + 1, y0    );
        float h01 = at(x0,     y0 + 1);
        float h11 = at(x0 + 1, y0 + 1);
        return (1 - tx) * (1 - ty) * h00
             +      tx  * (1 - ty) * h10
             + (1 - tx) *      ty  * h01
             +      tx  *      ty  * h11;
    }

    // Gradient at (x, y) using central differences
    void gradient(float fx, float fy, float& gx, float& gy) const {
        // Central difference with bilinear sampling
        constexpr float d = 1.f;
        gx = sampleBilinear(fx + d, fy) - sampleBilinear(fx - d, fy);
        gy = sampleBilinear(fx, fy + d) - sampleBilinear(fx, fy - d);
    }

    // ── Initialisation ───────────────────────────────────────────
    enum class NoiseType { Perlin, Simplex };

    void generateNoise(const NoiseGenerator& ng,
                       NoiseType  type        = NoiseType::Simplex,
                       float      scale       = 0.003f,
                       int        octaves     = 8,
                       float      persistence = 0.5f,
                       float      lacunarity  = 2.0f) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                float nx = x * scale, ny = y * scale;
                float h = (type == NoiseType::Simplex)
                    ? ng.fractalSimplex(nx, ny, octaves, persistence, lacunarity)
                    : ng.fractalPerlin (nx, ny, octaves, persistence, lacunarity);
                at(x, y) = h;
            }
        }
        // Remap to [0, 1]
        normalise();
    }

    // Load from raw float buffer (e.g., from external heightmap import)
    void loadFromBuffer(const std::vector<float>& buf, int w, int h) {
        if (static_cast<int>(buf.size()) != w * h)
            throw std::runtime_error("Buffer size mismatch");
        width_ = w; height_ = h;
        data_ = buf;
        normalise();
    }

    // ── Filters ──────────────────────────────────────────────────

    // Simple 3×3 box blur (used to smooth artefacts)
    void blur(int passes = 1) {
        std::vector<float> tmp(data_.size());
        for (int p = 0; p < passes; ++p) {
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    float sum = 0.f; int cnt = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        sum += at(x + dx, y + dy);
                        ++cnt;
                    }
                    tmp[static_cast<size_t>(y * width_ + x)] = sum / cnt;
                }
            }
            data_ = tmp;
        }
    }

    // ── Thermal erosion ──────────────────────────────────────────
    /**
     * Thermal (talus) erosion: material slides when slope > talus angle.
     * Iterations: number of full passes over the grid.
     * talusAngle: maximum stable height difference between neighbours.
     * erosionRate: fraction of excess material that moves per iteration.
     */
    void thermalErosion(int iterations = 50,
                        float talusAngle = 0.05f,
                        float erosionRate = 0.5f) {
        constexpr int ddx[4] = {1, -1,  0, 0};
        constexpr int ddy[4] = {0,  0,  1,-1};

        std::vector<float> tmp(data_.size());

        for (int iter = 0; iter < iterations; ++iter) {
            tmp = data_;
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    float h = at(x, y);
                    float totalDiff = 0.f;
                    float maxDiff   = 0.f;

                    // Find neighbours with height difference > talus
                    for (int d = 0; d < 4; ++d) {
                        float dn = h - at(x + ddx[d], y + ddy[d]);
                        if (dn > talusAngle) {
                            totalDiff += dn;
                            maxDiff = std::max(maxDiff, dn);
                        }
                    }

                    if (totalDiff == 0.f) continue;

                    // Distribute material proportionally
                    for (int d = 0; d < 4; ++d) {
                        int nx2 = std::clamp(x + ddx[d], 0, width_  - 1);
                        int ny2 = std::clamp(y + ddy[d], 0, height_ - 1);
                        float dn = h - at(nx2, ny2);
                        if (dn > talusAngle) {
                            float move = erosionRate * (dn / totalDiff)
                                       * (maxDiff - talusAngle);
                            tmp[static_cast<size_t>(y  * width_ + x )] -= move;
                            tmp[static_cast<size_t>(ny2 * width_ + nx2)] += move;
                        }
                    }
                }
            }
            data_ = tmp;
        }
    }

    // ── Statistics ───────────────────────────────────────────────
    float minHeight() const {
        return *std::min_element(data_.begin(), data_.end());
    }
    float maxHeight() const {
        return *std::max_element(data_.begin(), data_.end());
    }

    void normalise() {
        float lo = minHeight(), hi = maxHeight();
        if (hi == lo) return;
        float inv = 1.f / (hi - lo);
        for (auto& v : data_) v = (v - lo) * inv;
    }

    // Deep copy
    Heightmap clone() const {
        Heightmap out(width_, height_);
        out.data_ = data_;
        return out;
    }

private:
    int   width_  = 0;
    int   height_ = 0;
    std::vector<float> data_;
};
