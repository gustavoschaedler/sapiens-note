# Build SapiensNote (Phase 1) with zero local setup.
#   docker build -t sapiensnote .
# The image builds the core + tests + CLI + SDL2 simulator and runs the test
# suite during the build, so a successful `docker build` already proves it works.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Toolchain + libs: C++23 compiler, CMake, SDL2 (simulator + audio), sqlite3
# (local persistence), git (for the optional LVGL fetch).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        libsdl2-dev \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

# Configure + build everything, then run the tests as part of the image build.
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j "$(nproc)" \
    && ctest --test-dir build --output-on-failure

# Default: re-run the tests + a headless smoke of the simulator.
CMD ["bash", "-lc", "ctest --test-dir build --output-on-failure && ./build/simulator/sapiens-sim --headless"]
