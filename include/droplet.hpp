#pragma once
/**
 * droplet.hpp
 * ─────────────────────────────────────────────────────────────────
 * Represents a single rainfall droplet used in the particle-based
 * hydraulic erosion model.
 *
 * Each droplet:
 *   • Has a 2-D position (floating-point, sub-pixel) on the heightmap
 *   • Accumulates velocity under gravity along the steepest descent
 *   • Carries water (mass) that evaporates each step
 *   • Carries suspended sediment and erodes / deposits terrain
 *
 * The droplet does NOT modify the heightmap directly – that is the
 * responsibility of ErosionSimulator so thread-safety can be handled
 * at a higher level.
 * ─────────────────────────────────────────────────────────────────
 */

#include <cstdint>

// ─── Simulation parameters (tweakable at runtime) ────────────────
struct ErosionParams {
    // --- Droplet physics ---
    float inertia          = 0.05f;  ///< [0-1] How much old direction is kept
    float gravity          = 4.0f;   ///< Acceleration of gravity (world units)
    float minSlope         = 0.01f;  ///< Minimum slope for capacity calculation
    float friction         = 0.05f;  ///< Velocity dampening per step

    // --- Sediment transport ---
    float erosionRate      = 0.3f;   ///< Fraction of deficit eroded per step
    float depositionRate   = 0.3f;   ///< Fraction of excess deposited per step
    float sedimentCapK     = 4.0f;   ///< Sediment capacity constant (K)

    // --- Water ---
    float evaporationRate  = 0.02f;  ///< Fraction of water lost per step [0-1]
    float initialWater     = 1.0f;   ///< Starting water volume per droplet
    float initialSpeed     = 1.0f;   ///< Starting speed

    // --- Erosion brush ---
    int   erosionRadius    = 3;      ///< Pixel radius of deposition brush
    float erosionBlur      = 1.0f;   ///< Gaussian sigma of brush

    // --- Lifetime ---
    int   maxLifetime      = 60;     ///< Steps before droplet is destroyed

    // --- Thermal ---
    float talusAngle       = 0.05f;  ///< Thermal erosion max stable slope
    float thermalRate      = 0.5f;   ///< Thermal erosion rate

    // --- Simulation volume ---
    int   numDroplets      = 200'000; ///< Total droplets per simulation run
    uint32_t seed          = 42;      ///< RNG seed (0 = non-deterministic)

    // --- Noise settings ---
    float noiseScale       = 0.003f;
    int   noiseOctaves     = 8;
    float noisePersistence = 0.5f;
    float noiseLacunarity  = 2.0f;
};

// ─── Droplet ─────────────────────────────────────────────────────
struct Droplet {
    float posX = 0.f;      ///< World-space X position
    float posY = 0.f;      ///< World-space Y position
    float dirX = 0.f;      ///< Normalised movement direction X
    float dirY = 0.f;      ///< Normalised movement direction Y
    float speed   = 1.f;   ///< Current speed
    float water   = 1.f;   ///< Current water volume
    float sediment = 0.f;  ///< Suspended sediment
    int   lifetime = 0;    ///< Steps taken so far

    // Spawn a droplet at a given position with default parameters
    static Droplet spawn(float x, float y, const ErosionParams& p) {
        Droplet d;
        d.posX     = x;
        d.posY     = y;
        d.dirX     = 0.f;
        d.dirY     = 0.f;
        d.speed    = p.initialSpeed;
        d.water    = p.initialWater;
        d.sediment = 0.f;
        d.lifetime = 0;
        return d;
    }

    // Compute sediment capacity given local slope and speed
    float capacity(float slope, const ErosionParams& p) const {
        float s = std::max(std::abs(slope), p.minSlope);
        return s * speed * water * p.sedimentCapK;
    }

    bool alive(const ErosionParams& p) const {
        return water > 0.001f && lifetime < p.maxLifetime;
    }
};
