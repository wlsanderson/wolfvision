# wolfvision

Digital video and telemetry for an airborne transmitter and ground receiver.

The working **simulation milestone** sends prerecorded H.264 and clearly labelled
simulated UART measurements over localhost UDP. `simulation_rx` records complete
video access units, telemetry CSV, and per-session JSON statistics. It handles
loss, reordering, duplicates, restarts, and bounded buffering.

**Hardware v0.1 remains planned:** live camera and H.264 encoding on Luckfox Pico
Max (256 MB), the SC3336 camera, real UART data, and a link through two
E103-RTL8812CU modules to a USB-connected laptop. A microSD image is selected;
SDK matching, UART protocol, and USB radio integration still need verification.

## Setup

Use Ubuntu/WSL, a C++17 compiler, and CMake 3.20+ for the commands below.
Integration tests also need uv, Python 3.12, FFmpeg with libx264, and FFprobe.
Follow [development setup](docs/DEVELOPMENT.md#setup) once before building.
The C++ applications have no third-party library dependencies.

## Build and test

Run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CTest runs three quick C++ test executables, normally in under a second.
For the standard localhost video/telemetry check, normally about 12 seconds:

```sh
uv run --locked tools/simulation_smoke.py build
```

This checks receiver rejection/sequence behavior, a clean 60-frame recording,
reordering/duplicates, and deterministic loss with keyframe recovery. It prints
the fresh `build/simulation-*` directory containing recordings and reports.
See [testing](docs/DEVELOPMENT.md#testing) for the extended suite and tooling checks.

## Guides

- [Simulation](docs/SIMULATION.md): run TX/RX manually, create input, inspect outputs, and inject faults.
- [Development](docs/DEVELOPMENT.md): setup, tests, dependency troubleshooting, and source layout.
- [Luckfox SDK setup](docs/LUCKFOX_SDK.md): resume SDK installation on Ubuntu 22.04 WSL.
- [Luckfox camera preparation](docs/LUCKFOX_CAMERA.md): SC3336/H.264 API review, adapter plan, and future board checks.
- [Formatting and linting](docs/DEVELOPMENT.md#formatting-and-linting): required checks for C++ and Python changes.
- [Protocol](docs/PROTOCOL.md): wire fields, units, validation, sequence handling, and recovery.
- [Project context](docs/PROJECT_CONTEXT.md): confirmed hardware goals, verification evidence, and open decisions.

Simulation uses a constrained H.264 file profile and synthetic UART framing.
It provides recorded output; live capture, board encoding/execution, real UART,
radio integration, FEC, and a graphical interface are future work. Host results
do not establish RF performance or hardware v0.1 completion.
