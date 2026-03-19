#pragma once
/**
 * erosion_simulator.hpp
 * ─────────────────────────────────────────────────────────────────
 * Particle-based hydraulic erosion engine.
 *
 * Key design decisions
 * ────────────────────
 * 1. THREAD SAFETY via per-cell std::atomic<float> deposits.
 *    Each thread accumulates erosion/deposition deltas in a
 *    local buffer; at the end of the batch they are flushed with
 *    fetch_add on an atomic<float> shadow grid (C++20 or
 *    CAS-loop for C++17).  This avoids a global mutex.
 *
 * 2. EROSION BRUSH: Instead of modifying a single cell, a
 *    Gaussian-weighted brush of radius R spreads the erosion
 *    across neighbouring cells, producing smooth river channels.
 *
 * 3. STEP-BY-STEP mode: call simulateStep(n) to advance n
 *    droplets at a time, allowing external visualisation hooks.
 * ─────────────────────────────────────────────────────────────────
 */

#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <random>
#include <cmath>
#include <functional>
#include <algorithm>
#include <numeric>

#include "heightmap.hpp"
#include "droplet.hpp"

// ─── ErosionSimulator ────────────────────────────────────────────
class ErosionSimulator {
public:
    // Progress callback: called after each batch of droplets
    using ProgressCB = std::function<void(int done, int total)>;

    explicit ErosionSimulator(Heightmap& hmap, const ErosionParams& params)
        : hmap_(hmap), params_(params) {
        precomputeBrush();
    }

    // ── Full run (multi-threaded) ─────────────────────────────────
    void runAll(ProgressCB cb = nullptr) {
        dropletsProcessed_ = 0;
        std::mt19937 rng(params_.seed ? params_.seed : std::random_device{}());

        int total   = params_.numDroplets;
        int threads = std::max(1u, std::thread::hardware_concurrency());
        int batch   = std::max(1, 1024);  // droplets per batch

        std::uniform_real_distribution<float> rx(0.f, static_cast<float>(hmap_.width()  - 1));
        std::uniform_real_distribution<float> ry(0.f, static_cast<float>(hmap_.height() - 1));

        // Pre-generate spawn positions
        std::vector<std::pair<float,float>> spawns(total);
        for (auto& s : spawns) s = {rx(rng), ry(rng)};

        // Atomic delta buffer (CAS-based float atomics for C++17)
        int sz = hmap_.width() * hmap_.height();
        std::vector<std::atomic<float>> delta(sz);
        for (auto& a : delta) a.store(0.f, std::memory_order_relaxed);

        // Process in parallel chunks
        int processed = 0;
        while (processed < total) {
            int chunkSize = std::min(batch * threads, total - processed);

            // Divide the chunk evenly across threads
            std::vector<std::thread> workers;
            int perThread = (chunkSize + threads - 1) / threads;

            for (int t = 0; t < threads; ++t) {
                int start = processed + t * perThread;
                int end   = std::min(start + perThread, processed + chunkSize);
                if (start >= processed + chunkSize) break;

                workers.emplace_back([&, start, end](){
                    // Each thread gets its own sub-RNG seeded from global params
                    std::mt19937 trng(params_.seed ^ static_cast<uint32_t>(start));
                    for (int i = start; i < end; ++i) {
                        simulateDroplet(spawns[i].first, spawns[i].second, delta);
                    }
                });
            }

            for (auto& w : workers) w.join();
            processed += chunkSize;

            // Flush delta into heightmap
            flushDelta(delta);
            // Reset delta for next chunk
            for (auto& a : delta) a.store(0.f, std::memory_order_relaxed);

            dropletsProcessed_ = processed;
            if (cb) cb(processed, total);
        }
    }

    // ── Step-by-step mode ────────────────────────────────────────
    /**
     * Simulate exactly 'count' more droplets.
     * Spawns are drawn from an internal RNG so steps are
     * consistent with a full runAll() call.
     */
    void simulateStep(int count) {
        if (!stepRng_) {
            stepRng_ = std::make_unique<std::mt19937>(params_.seed);
            stepRx_ = std::uniform_real_distribution<float>(
                0.f, static_cast<float>(hmap_.width()  - 1));
            stepRy_ = std::uniform_real_distribution<float>(
                0.f, static_cast<float>(hmap_.height() - 1));
        }

        int sz = hmap_.width() * hmap_.height();
        std::vector<std::atomic<float>> delta(sz);
        for (auto& a : delta) a.store(0.f, std::memory_order_relaxed);

        for (int i = 0; i < count; ++i) {
            float x = stepRx_(*stepRng_);
            float y = stepRy_(*stepRng_);
            simulateDroplet(x, y, delta);
        }

        flushDelta(delta);
        dropletsProcessed_ += count;
    }

    int dropletsProcessed() const { return dropletsProcessed_; }

    // ── Parameters ────────────────────────────────────────────────
    void setParams(const ErosionParams& p) {
        params_ = p;
        precomputeBrush();
    }
    const ErosionParams& params() const { return params_; }

private:
    Heightmap&    hmap_;
    ErosionParams params_;

    // Pre-computed Gaussian erosion brush
    std::vector<std::pair<int,int>> brushOffsets_; // (dx, dy)
    std::vector<float>              brushWeights_; // normalised Gaussian weight

    int  dropletsProcessed_ = 0;

    // Step-by-step state
    std::unique_ptr<std::mt19937> stepRng_;
    std::uniform_real_distribution<float> stepRx_, stepRy_;

    // ── Brush pre-computation ─────────────────────────────────────
    void precomputeBrush() {
        brushOffsets_.clear();
        brushWeights_.clear();

        int R = params_.erosionRadius;
        float sigma = params_.erosionBlur > 0.f ? params_.erosionBlur
                                                : 1.f;
        float sigma2 = sigma * sigma;
        float sum = 0.f;

        for (int dy = -R; dy <= R; ++dy) {
            for (int dx = -R; dx <= R; ++dx) {
                float dist2 = static_cast<float>(dx*dx + dy*dy);
                if (dist2 > R * R) continue;
                float w = std::exp(-dist2 / (2.f * sigma2));
                brushOffsets_.push_back({dx, dy});
                brushWeights_.push_back(w);
                sum += w;
            }
        }
        // Normalise
        for (auto& w : brushWeights_) w /= sum;
    }

    // ── Single droplet simulation ─────────────────────────────────
    void simulateDroplet(float spawnX, float spawnY,
                         std::vector<std::atomic<float>>& delta) const {
        Droplet d = Droplet::spawn(spawnX, spawnY, params_);

        while (d.alive(params_)) {
            int   cx = static_cast<int>(d.posX);
            int   cy = static_cast<int>(d.posY);

            // Out-of-bounds check
            if (cx < 0 || cx >= hmap_.width()  - 1 ||
                cy < 0 || cy >= hmap_.height() - 1) break;

            float oldH = hmap_.sampleBilinear(d.posX, d.posY);

            // ── Gradient descent direction ────────────────────────
            float gx, gy;
            hmap_.gradient(d.posX, d.posY, gx, gy);

            // Blend old direction with new (inertia)
            d.dirX = d.dirX * params_.inertia - gx * (1.f - params_.inertia);
            d.dirY = d.dirY * params_.inertia - gy * (1.f - params_.inertia);

            // Normalise direction
            float len = std::sqrt(d.dirX * d.dirX + d.dirY * d.dirY);
            if (len < 1e-6f) {
                // Stuck on flat terrain – pick a random direction and stop
                break;
            }
            d.dirX /= len;
            d.dirY /= len;

            // Move
            float newX = d.posX + d.dirX;
            float newY = d.posY + d.dirY;

            // Out-of-bounds after move?
            if (newX < 0 || newX >= hmap_.width()  - 1 ||
                newY < 0 || newY >= hmap_.height() - 1) break;

            float newH  = hmap_.sampleBilinear(newX, newY);
            float slope = oldH - newH;          // positive = going downhill

            // ── Sediment capacity ─────────────────────────────────
            float cap = d.capacity(slope, params_);

            // ── Erode or deposit ──────────────────────────────────
            if (d.sediment > cap) {
                // Deposit excess sediment at current position
                float deposit = (d.sediment - cap) * params_.depositionRate;
                d.sediment -= deposit;
                // Spread over brush at old position
                applyBrush(cx, cy, +deposit, delta);
            } else {
                // Erode from current position
                float erode = std::min(
                    (cap - d.sediment) * params_.erosionRate,
                    slope > 0.f ? slope : 0.f);   // Can't erode below neighbours
                if (erode < 0.f) erode = 0.f;
                d.sediment += erode;
                applyBrush(cx, cy, -erode, delta);
            }

            // ── Physics update ────────────────────────────────────
            d.speed = std::sqrt(std::max(0.f,
                d.speed * d.speed + slope * params_.gravity));
            d.speed = d.speed * (1.f - params_.friction);
            d.water *= (1.f - params_.evaporationRate);

            d.posX = newX;
            d.posY = newY;
            ++d.lifetime;
        }

        // On death, deposit remaining sediment at final position
        if (d.sediment > 0.f) {
            int cx = std::clamp(static_cast<int>(d.posX), 0, hmap_.width()  - 1);
            int cy = std::clamp(static_cast<int>(d.posY), 0, hmap_.height() - 1);
            applyBrush(cx, cy, +d.sediment, delta);
        }
    }

    // ── Apply Gaussian brush to delta buffer ──────────────────────
    void applyBrush(int cx, int cy, float amount,
                    std::vector<std::atomic<float>>& delta) const {
        int W = hmap_.width(), H = hmap_.height();
        for (size_t k = 0; k < brushOffsets_.size(); ++k) {
            int bx = cx + brushOffsets_[k].first;
            int by = cy + brushOffsets_[k].second;
            if (bx < 0 || bx >= W || by < 0 || by >= H) continue;

            float change = amount * brushWeights_[k];
            // Atomic float add via CAS loop (C++17 portable)
            auto& a = delta[static_cast<size_t>(by * W + bx)];
            float expected = a.load(std::memory_order_relaxed);
            while (!a.compare_exchange_weak(expected, expected + change,
                    std::memory_order_relaxed, std::memory_order_relaxed)) { }
        }
    }

    // ── Flush accumulated delta into heightmap ────────────────────
    void flushDelta(std::vector<std::atomic<float>>& delta) {
        float* h = hmap_.data();
        for (int i = 0; i < hmap_.width() * hmap_.height(); ++i) {
            h[i] += delta[i].load(std::memory_order_relaxed);
            // Clamp to avoid extreme values
            h[i] = std::max(0.f, h[i]);
        }
    }
};
