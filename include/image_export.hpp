#pragma once
/**
 * image_export.hpp
 * ─────────────────────────────────────────────────────────────────
 * Exports heightmaps to grayscale or colorised PNG files using
 * stb_image_write (single-header, no extra dependencies).
 *
 * Also provides a simple "diff" export that visualises the change
 * between two heightmaps (blue = lower, red = higher).
 * ─────────────────────────────────────────────────────────────────
 */

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "heightmap.hpp"

// stb_image_write – implementation compiled in image_export.cpp
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../include/stb_image_write.h"

// ─── Colour-map helpers ───────────────────────────────────────────
struct RGB { uint8_t r, g, b; };

// Terrain colour ramp (water → sand → grass → rock → snow)
inline RGB terrainColormap(float t) {
    // Breakpoints: deep water, shallow water, sand, grass, rock, snow
    struct Stop { float pos; RGB color; };
    static const Stop stops[] = {
        {0.00f, {  0,  20, 100}},  // deep water
        {0.20f, {  0,  60, 160}},  // shallow water
        {0.28f, {195, 185, 130}},  // sand / beach
        {0.35f, { 80, 140,  60}},  // low grass
        {0.55f, { 60, 110,  40}},  // forest
        {0.70f, {120, 100,  80}},  // rocky
        {0.85f, {155, 150, 145}},  // high rock
        {1.00f, {240, 240, 255}},  // snow
    };
    constexpr int N = sizeof(stops) / sizeof(stops[0]);

    t = std::clamp(t, 0.f, 1.f);
    for (int i = 0; i < N - 1; ++i) {
        if (t <= stops[i + 1].pos) {
            float blend = (t - stops[i].pos) /
                          (stops[i + 1].pos - stops[i].pos);
            auto lerp8 = [&](uint8_t a, uint8_t b) {
                return static_cast<uint8_t>(a + blend * (b - a));
            };
            return { lerp8(stops[i].color.r, stops[i+1].color.r),
                     lerp8(stops[i].color.g, stops[i+1].color.g),
                     lerp8(stops[i].color.b, stops[i+1].color.b) };
        }
    }
    return stops[N - 1].color;
}

// Hillshading: combine height with a directional light
inline float hillshade(const Heightmap& hm, int x, int y, float strength = 6.f) {
    float gx, gy;
    hm.gradient(static_cast<float>(x), static_cast<float>(y), gx, gy);
    // Normalise
    float len = std::sqrt(gx*gx + gy*gy + 1.f);
    // Light direction (NW, top-left)
    float shade = ((-gx - gy) / len + 1.f) * 0.5f;
    // Blend with flat
    return std::clamp(0.5f + strength * (shade - 0.5f), 0.f, 1.f);
}

// ─── ImageExport ─────────────────────────────────────────────────
class ImageExport {
public:
    // Grayscale PNG [0 = black, 255 = white]
    static void saveGrayscale(const Heightmap& hm, const std::string& path) {
        int W = hm.width(), H = hm.height();
        std::vector<uint8_t> pixels(static_cast<size_t>(W * H));
        float lo = hm.minHeight(), hi = hm.maxHeight();
        float range = (hi > lo) ? (hi - lo) : 1.f;

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float t = (hm.at(x, y) - lo) / range;
                pixels[static_cast<size_t>(y * W + x)] =
                    static_cast<uint8_t>(std::clamp(t * 255.f, 0.f, 255.f));
            }

        if (!stbi_write_png(path.c_str(), W, H, 1, pixels.data(), W))
            throw std::runtime_error("Failed to write: " + path);
    }

    // Colourised terrain PNG with hillshading
    static void saveColorised(const Heightmap& hm, const std::string& path,
                               bool shade = true) {
        int W = hm.width(), H = hm.height();
        std::vector<uint8_t> pixels(static_cast<size_t>(W * H * 3));
        float lo = hm.minHeight(), hi = hm.maxHeight();
        float range = (hi > lo) ? (hi - lo) : 1.f;

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float t = (hm.at(x, y) - lo) / range;
                RGB c = terrainColormap(t);
                if (shade) {
                    float s = hillshade(hm, x, y);
                    c.r = static_cast<uint8_t>(c.r * s);
                    c.g = static_cast<uint8_t>(c.g * s);
                    c.b = static_cast<uint8_t>(c.b * s);
                }
                size_t idx = static_cast<size_t>((y * W + x) * 3);
                pixels[idx + 0] = c.r;
                pixels[idx + 1] = c.g;
                pixels[idx + 2] = c.b;
            }
        }

        if (!stbi_write_png(path.c_str(), W, H, 3, pixels.data(), W * 3))
            throw std::runtime_error("Failed to write: " + path);
    }

    // Diff image: blue=eroded, red=deposited, black=unchanged
    static void saveDiff(const Heightmap& before,
                          const Heightmap& after,
                          const std::string& path,
                          float amplify = 200.f) {
        int W = before.width(), H = before.height();
        if (W != after.width() || H != after.height())
            throw std::runtime_error("Heightmap size mismatch in diff");

        std::vector<uint8_t> pixels(static_cast<size_t>(W * H * 3));

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float diff = (after.at(x, y) - before.at(x, y)) * amplify;
                uint8_t r = 0, g = 0, b = 0;
                if (diff < 0) {  // erosion → blue
                    b = static_cast<uint8_t>(std::min(-diff, 1.f) * 255.f);
                } else {          // deposition → red
                    r = static_cast<uint8_t>(std::min(diff,  1.f) * 255.f);
                }
                size_t idx = static_cast<size_t>((y * W + x) * 3);
                pixels[idx + 0] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
            }
        }

        if (!stbi_write_png(path.c_str(), W, H, 3, pixels.data(), W * 3))
            throw std::runtime_error("Failed to write: " + path);
    }

    // Side-by-side comparison PNG
    static void saveSideBySide(const Heightmap& before,
                                const Heightmap& after,
                                const std::string& path) {
        int W = before.width(), H = before.height();
        int outW = W * 2;
        std::vector<uint8_t> pixels(static_cast<size_t>(outW * H * 3));

        auto putPixel = [&](int x, int y, RGB c) {
            size_t idx = static_cast<size_t>((y * outW + x) * 3);
            pixels[idx+0] = c.r; pixels[idx+1] = c.g; pixels[idx+2] = c.b;
        };

        // Helper lambda to get coloured pixel from a heightmap
        auto getPixel = [&](const Heightmap& hm, int x, int y, float lo, float range) -> RGB {
            float t = (hm.at(x,y) - lo) / range;
            RGB c = terrainColormap(t);
            float s = hillshade(hm, x, y);
            c.r = static_cast<uint8_t>(c.r * s);
            c.g = static_cast<uint8_t>(c.g * s);
            c.b = static_cast<uint8_t>(c.b * s);
            return c;
        };

        float loB = before.minHeight(), hiB = before.maxHeight();
        float loA = after.minHeight(),  hiA = after.maxHeight();
        float lo  = std::min(loB, loA), hi = std::max(hiB, hiA);
        float range = (hi > lo) ? (hi - lo) : 1.f;

        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                putPixel(x,     y, getPixel(before, x, y, lo, range));
                putPixel(x + W, y, getPixel(after,  x, y, lo, range));
            }
        }

        if (!stbi_write_png(path.c_str(), outW, H, 3, pixels.data(), outW * 3))
            throw std::runtime_error("Failed to write: " + path);
    }
};
