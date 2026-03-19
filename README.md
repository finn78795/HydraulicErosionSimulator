# Hydraulic Erosion Simulator

A high-performance, particle-based hydraulic erosion simulator written in modern C++17. Produces visually realistic terrain — river channels, valley smoothing, sediment fans — suitable for game development, procedural world generation, and terrain research.

---

## Features

| Feature | Details |
|---------|---------|
| **Noise generation** | Seeded Perlin & Simplex fBm with adjustable octaves, persistence, lacunarity |
| **Hydraulic erosion** | Particle droplets following gradient descent, sediment capacity model |
| **Thermal erosion** | Talus-angle material sliding (second erosion mode) |
| **Multithreading** | `std::thread` parallelism with lock-free atomic float delta buffers |
| **Gaussian brush** | Smooth Gaussian-weighted erosion spread over a configurable radius |
| **PNG export** | Grayscale, colourised (terrain ramp + hillshading), diff, and side-by-side comparison |
| **Step-by-step mode** | Export frames every N droplets for animation or debugging |
| **Deterministic** | Fixed seed produces identical output across runs |
| **CLI** | Fully configurable from the command line, no GUI required |

---

## Project Structure

```
hydraulic_erosion/
├── main.cpp                   ← Entry point, CLI parsing, orchestration
├── CMakeLists.txt             ← CMake build (recommended)
├── Makefile                   ← Quick GNU Make build
├── include/
│   ├── noise.hpp              ← Seeded Perlin + Simplex noise generator
│   ├── heightmap.hpp          ← 2-D float grid, noise init, thermal erosion
│   ├── droplet.hpp            ← Droplet state + ErosionParams struct
│   ├── erosion_simulator.hpp  ← Multi-threaded hydraulic erosion engine
│   ├── image_export.hpp       ← PNG export (grayscale, colour, diff, side-by-side)
│   └── stb_image_write.h      ← Single-header PNG writer (stb)
└── output/                    ← Generated images written here
```

---

## Building

### Requirements

- C++17 compiler: GCC ≥ 7, Clang ≥ 5, or MSVC 2017+
- POSIX threads (`-lpthread`) — standard on Linux/macOS
- Python 3 + Pillow (for PNG output via the fallback stub; auto-detected)

### Option A — GNU Make (quickest)

```bash
make          # Optimised build
make run      # Build + run with default 512×512 / 200k droplets
make run-large # 1024×1024 / 500k droplets + thermal erosion
make clean
```

### Option B — CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./erosion --output ../output
```

### Option C — Direct compile

```bash
g++ -std=c++17 -O3 -ffast-math -pthread -o erosion main.cpp
```

---

## Usage

```
./erosion [options]
```

### All Options

| Option | Default | Description |
|--------|---------|-------------|
| `--width N` | 512 | Heightmap width (pixels) |
| `--height N` | 512 | Heightmap height (pixels) |
| `--droplets N` | 200000 | Number of erosion droplets |
| `--seed N` | 42 | RNG seed (deterministic) |
| `--erosion F` | 0.3 | Erosion rate [0–1] |
| `--deposition F` | 0.3 | Deposition rate [0–1] |
| `--evaporation F` | 0.02 | Evaporation per step [0–1] |
| `--inertia F` | 0.05 | Droplet direction inertia [0–1] |
| `--gravity F` | 4.0 | Gravity strength |
| `--lifetime N` | 60 | Max droplet age (steps) |
| `--radius N` | 3 | Erosion Gaussian brush radius |
| `--scale F` | 0.003 | Noise frequency scale |
| `--octaves N` | 8 | Noise fBm octaves |
| `--thermal` | off | Enable thermal erosion pass |
| `--thermal-iter N` | 50 | Thermal erosion iterations |
| `--step N` | off | Step mode: save frame every N droplets |
| `--output DIR` | output | Output directory |
| `--no-color` | off | Skip colourised PNG (grayscale only) |
| `--noise perlin\|simplex` | simplex | Noise type |

### Examples

**Default 512×512 run:**
```bash
./erosion
```

**High-detail 1024×1024 with thermal erosion:**
```bash
./erosion --width 1024 --height 1024 --droplets 500000 --thermal
```

**Aggressive erosion (deeper channels):**
```bash
./erosion --erosion 0.6 --deposition 0.1 --evaporation 0.01 --lifetime 120
```

**Flat terrain with gentle erosion:**
```bash
./erosion --octaves 4 --scale 0.005 --erosion 0.2 --droplets 100000
```

**Step-by-step animation frames:**
```bash
./erosion --step 5000 --droplets 100000 --output frames
# Produces frames/frame_0.png, frame_1.png, ... every 50,000 droplets
```

**Different seed for variety:**
```bash
./erosion --seed 1337
./erosion --seed 9999 --noise perlin
```

---

## Output Files

| File | Description |
|------|-------------|
| `before_gray.png` | Raw Simplex/Perlin noise heightmap (grayscale) |
| `before_color.png` | Colourised terrain before erosion |
| `after_gray.png` | Post-erosion heightmap (grayscale) |
| `after_color.png` | Post-erosion colourised terrain with hillshading |
| `diff.png` | Change map: **blue** = eroded, **red** = deposited |
| `comparison.png` | Side-by-side before / after (same colour scale) |
| `frame_N.png` | Animation frames (step mode only) |

---

## Physics Model

### Droplet Lifecycle

```
Spawn at random position
        │
        ▼
Compute gradient (central differences + bilinear sampling)
        │
        ▼
Blend direction with gradient (inertia)
        │
        ▼
Compute sediment capacity: C = max(slope, minSlope) × speed × water × K
        │
        ├─ sediment > capacity → DEPOSIT (depositionRate × excess)
        │
        └─ sediment < capacity → ERODE  (erosionRate × deficit)
              │
              ▼ (applied via Gaussian brush)
Update speed: speed = √(speed² + slope × gravity) × (1 - friction)
Update water:  water  = water × (1 - evaporationRate)
        │
        ▼
Move to new position
        │
        ▼
Repeat until water < 0.001 or lifetime > maxLifetime
        │
        ▼
Deposit remaining sediment at final position
```

### Sediment Capacity

Capacity is proportional to slope, velocity, and water volume:

```
C = max(|slope|, minSlope) × speed × water × K
```

- High slope + high speed → droplet can carry more sediment
- When carrying less than capacity → erodes terrain
- When carrying more than capacity → deposits sediment

### Thermal Erosion (Talus Sliding)

Material slides to lower neighbours when height difference exceeds `talusAngle`.
Distribution is weighted proportionally to the deficit, producing realistic talus fans.

---

## Performance

| Map Size | Droplets | Threads | Time |
|----------|----------|---------|------|
| 512×512  | 200,000  | 2       | ~5s  |
| 512×512  | 200,000  | 8       | ~2s  |
| 1024×1024| 500,000  | 8       | ~8s  |

### Thread Safety

Each thread maintains a **local atomic float delta buffer** (CAS-loop atomic add). Deltas are flushed to the heightmap between batches — no mutex contention during simulation.

---

## Extending the Simulator

### Loading an external heightmap

```cpp
// In your own code:
Heightmap hmap;
std::vector<float> buf = /* load 16-bit grayscale and normalise to [0,1] */;
hmap.loadFromBuffer(buf, width, height);
```

### Custom erosion parameters

```cpp
ErosionParams p;
p.erosionRate     = 0.5f;
p.depositionRate  = 0.15f;
p.numDroplets     = 300000;
p.erosionRadius   = 5;

ErosionSimulator sim(hmap, p);
sim.runAll([](int done, int total){ /* progress */ });
```

### Programmatic step loop

```cpp
ErosionSimulator sim(hmap, params);
for (int i = 0; i < 20; ++i) {
    sim.simulateStep(10000);           // 10k droplets
    ImageExport::saveColorised(hmap, "frame_" + std::to_string(i) + ".png");
}
```

---

## License

MIT — free for personal, academic, and commercial use.
