# Makefile — Quick build without CMake
# Usage: make           (optimised build)
#        make debug     (debug build)
#        make run       (build + run with defaults)
#        make clean

CXX      ?= g++
CXXFLAGS  = -std=c++17 -O3 -march=native -ffast-math -Wall -Wextra -pthread
TARGET    = erosion
SRCS      = main.cpp
OUTDIR    = output

.PHONY: all debug run clean

all: $(TARGET)
	@mkdir -p $(OUTDIR)
	@echo "Build complete: ./$(TARGET)"

$(TARGET): $(SRCS) include/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

debug: CXXFLAGS = -std=c++17 -g -O0 -Wall -Wextra -pthread -fsanitize=address,undefined
debug: $(TARGET)

run: all
	./$(TARGET) --width 512 --height 512 --droplets 150000 \
	            --seed 42 --erosion 0.3 --deposition 0.3 \
	            --evaporation 0.02 --radius 3 --output $(OUTDIR)

run-large: all
	./$(TARGET) --width 1024 --height 1024 --droplets 500000 \
	            --seed 42 --thermal --output $(OUTDIR)

run-step: all
	./$(TARGET) --width 512 --height 512 --droplets 100000 \
	            --step 5000 --output $(OUTDIR)

clean:
	rm -f $(TARGET)
	rm -rf $(OUTDIR)/*.png $(OUTDIR)/frame_*.png
