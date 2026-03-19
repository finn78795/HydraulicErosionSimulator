#pragma once
/**
 * noise.hpp
 * ─────────────────────────────────────────────────────────────────
 * Seeded Perlin & Simplex noise generators used to initialise the
 * heightmap.  Both generators are self-contained (no external libs).
 *
 * Usage:
 *   NoiseGenerator ng(seed);
 *   float h = ng.fractalPerlin(x, y, octaves, persistence, lacunarity);
 * ─────────────────────────────────────────────────────────────────
 */

#include <array>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>
#include <cstdint>

// ─── Perlin / Simplex noise ──────────────────────────────────────
class NoiseGenerator {
public:
    explicit NoiseGenerator(uint32_t seed = 42) { reseed(seed); }

    // Re-seed the permutation table
    void reseed(uint32_t seed) {
        std::mt19937 rng(seed);
        std::iota(p_.begin(), p_.begin() + 256, 0);
        std::shuffle(p_.begin(), p_.begin() + 256, rng);
        // Duplicate for overflow avoidance
        for (int i = 0; i < 256; ++i) p_[256 + i] = p_[i];
    }

    // Classic Perlin noise in [-1, 1]
    float perlin(float x, float y) const {
        int xi = static_cast<int>(std::floor(x)) & 255;
        int yi = static_cast<int>(std::floor(y)) & 255;
        float xf = x - std::floor(x);
        float yf = y - std::floor(y);

        float u = fade(xf), v = fade(yf);

        int aa = p_[p_[xi]     + yi];
        int ab = p_[p_[xi]     + yi + 1];
        int ba = p_[p_[xi + 1] + yi];
        int bb = p_[p_[xi + 1] + yi + 1];

        return lerp(v,
            lerp(u, grad(aa, xf,     yf    ),
                    grad(ba, xf - 1, yf    )),
            lerp(u, grad(ab, xf,     yf - 1),
                    grad(bb, xf - 1, yf - 1)));
    }

    // Fractal (fBm) Perlin — returns value in roughly [0,1]
    float fractalPerlin(float x, float y,
                        int   octaves     = 6,
                        float persistence = 0.5f,
                        float lacunarity  = 2.0f) const {
        float value = 0.f, amplitude = 1.f, frequency = 1.f, max = 0.f;
        for (int o = 0; o < octaves; ++o) {
            value     += perlin(x * frequency, y * frequency) * amplitude;
            max       += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        // Normalise to [0, 1]
        return (value / max + 1.f) * 0.5f;
    }

    // Simplex noise in [-1, 1] (faster, fewer directional artefacts)
    float simplex(float x, float y) const {
        constexpr float F2 = 0.366025403f; // (sqrt(3)-1)/2
        constexpr float G2 = 0.211324865f; // (3-sqrt(3))/6

        float s  = (x + y) * F2;
        int   i  = fastfloor(x + s);
        int   j  = fastfloor(y + s);
        float t  = (i + j) * G2;
        float x0 = x - (i - t);
        float y0 = y - (j - t);

        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else         { i1 = 0; j1 = 1; }

        float x1 = x0 - i1 + G2;
        float y1 = y0 - j1 + G2;
        float x2 = x0 - 1.f + 2.f * G2;
        float y2 = y0 - 1.f + 2.f * G2;

        int ii = i & 255, jj = j & 255;
        int gi0 = p_[ii      + p_[jj     ]] % 12;
        int gi1 = p_[ii + i1 + p_[jj + j1]] % 12;
        int gi2 = p_[ii + 1  + p_[jj + 1 ]] % 12;

        float n0 = corner(gi0, x0, y0);
        float n1 = corner(gi1, x1, y1);
        float n2 = corner(gi2, x2, y2);

        return 70.f * (n0 + n1 + n2); // Scaled to [-1, 1]
    }

    // fBm simplex — returns value in [0,1]
    float fractalSimplex(float x, float y,
                         int   octaves     = 6,
                         float persistence = 0.5f,
                         float lacunarity  = 2.0f) const {
        float value = 0.f, amplitude = 1.f, frequency = 1.f, max = 0.f;
        for (int o = 0; o < octaves; ++o) {
            value     += simplex(x * frequency, y * frequency) * amplitude;
            max       += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }
        return std::clamp((value / max + 1.f) * 0.5f, 0.f, 1.f);
    }

private:
    std::array<int, 512> p_{};

    static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float lerp(float t, float a, float b) { return a + t * (b - a); }
    static int   fastfloor(float x) {
        return x > 0 ? static_cast<int>(x) : static_cast<int>(x) - 1;
    }

    float grad(int hash, float x, float y) const {
        int h = hash & 3;
        float u = (h < 2) ? x : y;
        float v = (h < 2) ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    // Simplex corner contribution
    static float corner(int gi, float x, float y) {
        // 2-D simplex gradient table
        constexpr float g3[12][2] = {
            {1,1},{-1,1},{1,-1},{-1,-1},
            {1,0},{-1,0},{1, 0},{-1, 0},
            {0,1},{ 0,1},{0,-1},{ 0,-1}
        };
        float t = 0.5f - x * x - y * y;
        if (t < 0.f) return 0.f;
        t *= t;
        return t * t * (g3[gi][0] * x + g3[gi][1] * y);
    }
};
