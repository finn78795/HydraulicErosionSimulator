cmake_minimum_required(VERSION 3.16)
project(HydraulicErosion VERSION 1.0 LANGUAGES CXX)

# ── C++17 required ────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ────────────────────────────────────────────────
if (MSVC)
    add_compile_options(/O2 /W4 /arch:AVX2)
else()
    add_compile_options(
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -ffast-math
    )
endif()

# ── Source files ──────────────────────────────────────────────────
set(SOURCES
    main.cpp
)

set(HEADERS
    include/noise.hpp
    include/heightmap.hpp
    include/droplet.hpp
    include/erosion_simulator.hpp
    include/image_export.hpp
    include/stb_image_write.h
)

# ── Executable ────────────────────────────────────────────────────
add_executable(erosion ${SOURCES} ${HEADERS})

target_include_directories(erosion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ── Threading ─────────────────────────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(erosion PRIVATE Threads::Threads)

# ── Output directory ──────────────────────────────────────────────
# Create output dir next to the binary
add_custom_command(TARGET erosion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:erosion>/output
    COMMENT "Creating output directory"
)

# ── Install ───────────────────────────────────────────────────────
install(TARGETS erosion DESTINATION bin)

message(STATUS "")
message(STATUS "  Hydraulic Erosion Simulator")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "")
