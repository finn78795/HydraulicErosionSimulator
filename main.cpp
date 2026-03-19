/**
 * main.cpp
 * ─────────────────────────────────────────────────────────────────
 * Hydraulic Erosion Simulator — Command-Line Entry Point
 *
 * Usage:
 *   ./erosion [options]
 *
 * Options:
 *   --width N           Heightmap width  (default: 512)
 *   --height N          Heightmap height (default: 512)
 *   --droplets N        Number of erosion droplets (default: 200000)
 *   --seed N            RNG seed (default: 42)
 *   --erosion F         Erosion rate      [0-1] (default: 0.3)
 *   --deposition F      Deposition rate   [0-1] (default: 0.3)
 *   --evaporation F     Evaporation rate  [0-1] (default: 0.02)
 *   --inertia F         Droplet inertia   [0-1] (default: 0.05)
 *   --gravity F         Gravity strength       (default: 4.0)
 *   --lifetime N        Max droplet lifetime   (default: 60)
 *   --radius N          Erosion brush radius   (default: 3)
 *   --scale F           Noise scale            (default: 0.003)
 *   --octaves N         Noise octaves          (default: 8)
 *   --thermal           Also run thermal erosion pass
 *   --thermal-iter N    Thermal erosion iterations (default: 50)
 *   --step N            Step-by-step mode: print progress every N drops
 *   --output DIR        Output directory (default: ./output)
 *   --no-color          Save grayscale only (skip colorised output)
 *   --noise perlin|simplex   Noise type (default: simplex)
 * ─────────────────────────────────────────────────────────────────
 */

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <stdexcept>
#include <cstring>

#include "include/noise.hpp"
#include "include/heightmap.hpp"
#include "include/droplet.hpp"
#include "include/erosion_simulator.hpp"
#include "include/image_export.hpp"

namespace fs = std::filesystem;

// ─── CLI parsing helpers ─────────────────────────────────────────
static float getFloat(int argc, char* argv[], int i, float def) {
    return (i + 1 < argc) ? std::stof(argv[i + 1]) : def;
}
static int getInt(int argc, char* argv[], int i, int def) {
    return (i + 1 < argc) ? std::stoi(argv[i + 1]) : def;
}
static bool hasFlag(int argc, char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}
static std::string getStr(int argc, char* argv[], const char* flag, std::string def) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    return def;
}

// ─── Progress bar ────────────────────────────────────────────────
static void printProgress(int done, int total, std::chrono::steady_clock::time_point start) {
    float pct = static_cast<float>(done) / total;
    int bar   = static_cast<int>(pct * 40);

    auto now  = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    double eta = (pct > 0.01f) ? elapsed / pct - elapsed : 0.0;

    std::cout << "\r  [";
    for (int i = 0; i < 40; ++i) std::cout << (i < bar ? '#' : '.');
    std::cout << "] " << std::setw(3) << static_cast<int>(pct * 100) << "%"
              << "  ETA:" << std::fixed << std::setprecision(1) << eta << "s  "
              << std::flush;
}

// ─── Main ────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::cout << "╔══════════════════════════════════════════════╗\n"
              << "║   Hydraulic Erosion Simulator  v1.0          ║\n"
              << "╚══════════════════════════════════════════════╝\n\n";

    // ── Parse parameters ──────────────────────────────────────────
    ErosionParams params;
    int mapW = 512, mapH = 512;
    bool doThermal    = false;
    int  thermalIter  = 50;
    int  stepMode     = 0;
    bool colorOutput  = !hasFlag(argc, argv, "--no-color");
    std::string outDir = "output";
    Heightmap::NoiseType noiseType = Heightmap::NoiseType::Simplex;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width")       mapW                   = getInt  (argc, argv, i, 512);
        if (arg == "--height")      mapH                   = getInt  (argc, argv, i, 512);
        if (arg == "--droplets")    params.numDroplets     = getInt  (argc, argv, i, 200000);
        if (arg == "--seed")        params.seed            = static_cast<uint32_t>(getInt(argc, argv, i, 42));
        if (arg == "--erosion")     params.erosionRate     = getFloat(argc, argv, i, 0.3f);
        if (arg == "--deposition")  params.depositionRate  = getFloat(argc, argv, i, 0.3f);
        if (arg == "--evaporation") params.evaporationRate = getFloat(argc, argv, i, 0.02f);
        if (arg == "--inertia")     params.inertia         = getFloat(argc, argv, i, 0.05f);
        if (arg == "--gravity")     params.gravity         = getFloat(argc, argv, i, 4.f);
        if (arg == "--lifetime")    params.maxLifetime     = getInt  (argc, argv, i, 60);
        if (arg == "--radius")      params.erosionRadius   = getInt  (argc, argv, i, 3);
        if (arg == "--scale")       params.noiseScale      = getFloat(argc, argv, i, 0.003f);
        if (arg == "--octaves")     params.noiseOctaves    = getInt  (argc, argv, i, 8);
        if (arg == "--thermal")     doThermal              = true;
        if (arg == "--thermal-iter")thermalIter            = getInt  (argc, argv, i, 50);
        if (arg == "--step")        stepMode               = getInt  (argc, argv, i, 10000);
        if (arg == "--output")      outDir                 = getStr  (argc, argv, "--output", "output");
        if (arg == "--noise" && i + 1 < argc) {
            std::string nt = argv[i + 1];
            if (nt == "perlin") noiseType = Heightmap::NoiseType::Perlin;
        }
    }

    // ── Output directory ──────────────────────────────────────────
    fs::create_directories(outDir);
    std::cout << "  Output directory : " << outDir << "\n";

    // ── Print config ──────────────────────────────────────────────
    std::cout << "  Map size         : " << mapW << " × " << mapH << "\n"
              << "  Droplets         : " << params.numDroplets << "\n"
              << "  Seed             : " << params.seed << "\n"
              << "  Erosion rate     : " << params.erosionRate << "\n"
              << "  Deposition rate  : " << params.depositionRate << "\n"
              << "  Evaporation rate : " << params.evaporationRate << "\n"
              << "  Brush radius     : " << params.erosionRadius << "\n"
              << "  Thermal erosion  : " << (doThermal ? "yes" : "no") << "\n"
              << "  Threads          : " << std::thread::hardware_concurrency() << "\n\n";

    try {
        // ── 1. Generate terrain ───────────────────────────────────
        std::cout << "► Generating terrain with "
                  << (noiseType == Heightmap::NoiseType::Simplex ? "Simplex" : "Perlin")
                  << " noise...\n";
        auto t0 = std::chrono::steady_clock::now();

        Heightmap hmap(mapW, mapH);
        NoiseGenerator ng(params.seed);
        hmap.generateNoise(ng, noiseType,
                           params.noiseScale, params.noiseOctaves,
                           params.noisePersistence, params.noiseLacunarity);

        double genTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "  ✓ Done (" << std::fixed << std::setprecision(2) << genTime << "s)\n\n";

        // ── 2. Save "before" images ───────────────────────────────
        std::cout << "► Saving before images...\n";
        Heightmap before = hmap.clone();
        ImageExport::saveGrayscale(before, outDir + "/before_gray.png");
        if (colorOutput)
            ImageExport::saveColorised(before, outDir + "/before_color.png");
        std::cout << "  ✓ Saved before_gray.png" << (colorOutput ? " + before_color.png" : "") << "\n\n";

        // ── 3. Hydraulic erosion ──────────────────────────────────
        std::cout << "► Running hydraulic erosion ("
                  << params.numDroplets << " droplets)...\n";
        auto t1 = std::chrono::steady_clock::now();

        ErosionSimulator sim(hmap, params);

        if (stepMode > 0) {
            // Step-by-step mode: save frames periodically
            int processed = 0;
            int frameIdx  = 0;
            std::cout << "  [Step mode: " << stepMode << " droplets/step]\n";
            while (processed < params.numDroplets) {
                int chunk = std::min(stepMode, params.numDroplets - processed);
                sim.simulateStep(chunk);
                processed += chunk;
                printProgress(processed, params.numDroplets, t1);

                // Save frame every 10 steps
                if (frameIdx % 10 == 0) {
                    std::string framePath = outDir + "/frame_"
                        + std::to_string(frameIdx / 10) + ".png";
                    if (colorOutput)
                        ImageExport::saveColorised(hmap, framePath);
                    else
                        ImageExport::saveGrayscale(hmap, framePath);
                }
                ++frameIdx;
            }
        } else {
            // Full multi-threaded run
            sim.runAll([&](int done, int total) {
                printProgress(done, total, t1);
            });
        }

        double erosTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t1).count();
        std::cout << "\n  ✓ Done (" << std::fixed << std::setprecision(2)
                  << erosTime << "s, "
                  << static_cast<int>(params.numDroplets / erosTime)
                  << " drops/s)\n\n";

        // ── 4. Thermal erosion (optional) ─────────────────────────
        if (doThermal) {
            std::cout << "► Running thermal erosion (" << thermalIter << " passes)...\n";
            auto t2 = std::chrono::steady_clock::now();
            hmap.thermalErosion(thermalIter, params.talusAngle, params.thermalRate);
            double thermTime = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t2).count();
            std::cout << "  ✓ Done (" << std::fixed << std::setprecision(2)
                      << thermTime << "s)\n\n";
        }

        // ── 5. Save "after" images ────────────────────────────────
        std::cout << "► Saving output images...\n";
        ImageExport::saveGrayscale (hmap, outDir + "/after_gray.png");
        if (colorOutput)
            ImageExport::saveColorised(hmap, outDir + "/after_color.png");

        // Diff and side-by-side comparison
        ImageExport::saveDiff       (before, hmap, outDir + "/diff.png");
        ImageExport::saveSideBySide (before, hmap, outDir + "/comparison.png");

        std::cout << "  ✓ after_gray.png\n"
                  << (colorOutput ? "  ✓ after_color.png\n" : "")
                  << "  ✓ diff.png          (blue=eroded, red=deposited)\n"
                  << "  ✓ comparison.png    (before | after)\n\n";

        // ── 6. Stats ──────────────────────────────────────────────
        float lo = hmap.minHeight(), hi = hmap.maxHeight();
        std::cout << "  Height range (after): [" << std::fixed << std::setprecision(4)
                  << lo << ", " << hi << "]\n";

        double totalTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "\n╔══════════════════════════════════════╗\n"
                  << "║  Simulation complete in "
                  << std::setw(6) << std::fixed << std::setprecision(2)
                  << totalTime << "s  ║\n"
                  << "╚══════════════════════════════════════╝\n";

    } catch (const std::exception& e) {
        std::cerr << "\n  ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
