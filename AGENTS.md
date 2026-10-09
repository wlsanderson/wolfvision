# Wolfvision project instructions

Before starting work, read `docs/PROJECT_CONTEXT.md` for the system scope, confirmed decisions, proposed milestones, and unresolved hardware choices.

Inspect the existing repository and relevant tests before making changes. Preserve existing work and distinguish planned features from verified implementation.

The current recommended baseline is C++17 with CMake for the core transmitter/receiver applications and Python for test automation and analysis. Follow newer explicit user decisions or documented project choices if they differ.

The user requires uv for repository Python tooling. Use the root `pyproject.toml`, `.python-version`, and `uv.lock`, run scripts with `uv run --locked`, and add dependencies with `uv add` (test/analysis dependencies use `--dev`). Do not introduce ad hoc pip installs. Python subprocesses should use `sys.executable` to retain the uv-selected environment. FFmpeg/FFprobe are separate system dependencies; document their Ubuntu installation and check their availability before media tests start.

## Required formatting and linting

Whenever you write or modify C++ or Python code, use the repository's locked
tools before finishing the task. clang-format formats C++, clang-tidy checks
C++ correctness/performance using the host compilation database, and Ruff
formats/lints Python. Configuration lives in `.clang-format`, `.clang-tidy`,
and `pyproject.toml`; tool versions are pinned by `uv.lock`.

From the repository root:

```sh
uv sync --locked
cmake -S . -B build -DBUILD_TESTING=ON
uv run --locked tools/check_code.py --fix
uv run --locked tools/check_code.py
```

The final command must pass. `--fix` applies C++/Python formatting and safe
Ruff fixes; review the changes and resolve remaining lint findings manually.
Do not use clang-tidy automatic fixes or Ruff unsafe fixes without reviewing
their behavior changes. Do not weaken the gate merely to make findings disappear;
document any justified, narrowly scoped exception. Run the tests appropriate
to the change as well; lint is not a substitute for tests.

The checker covers `.cpp`/`.hpp` under `src` and `tests`, plus repository Python,
excluding build/cache/virtual-environment directories. Use `--build PATH` for
another configured host build. Reconfigure when adding/removing C++ sources;
the checker rejects missing compilation entries. If a tool/check is blocked,
report the exact blocker instead of claiming it passed. Setup and troubleshooting
are documented in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#formatting-and-linting).

## Architecture and validation

Keep packet handling, transport, input sources, and hardware adapters separate. Develop and test portable code in Ubuntu/WSL; use the matching board SDK toolchain for Luckfox builds. A host build does not establish successful board execution.

The confirmed v0.1 goal is an integrated hardware bench system: live camera -> H.264 encoding on Luckfox Pico Max (256 MB RAM; shared Pro/Max board family), additional data into Luckfox over UART, video/data packetization, airborne E103-RTL8812CU -> ground E103-RTL8812CU -> USB-connected laptop. The user selected the Luckfox SC3336 3 MP camera, downloaded `Luckfox_Pico_ProMax_MicroSD_250607`, and specified USB from Luckfox to the airborne module with Luckfox in host mode. These are intended choices, not verified flashing/boot or hardware integration. SDK/image compatibility, exact SDK revision/toolchain, UART protocol, and radio driver/mode remain unresolved. Use the official `LuckfoxTECH/luckfox-pico` SDK with the shared RV1106 Pro/Max, SD_CARD, Buildroot configuration as the documented starting point; do not invent an SDK commit matching the image date. Keep the SDK checkout outside this repository and record the verified revision later.

Test portable software on Ubuntu/WSL and provide a board build for Luckfox Pico Max (256 MB) using the selected matching SDK. Prerecorded inputs are intermediate test fixtures; the v0.1 video source must be the real camera. The original telemetry demo was the first milestone and is now retired; it did not complete v0.1. Hardware-free video/telemetry tests do not prove RF range, antenna performance, physical latency, power consumption, or temperature behavior. Detailed completion thresholds still need agreement.

The authorized simulation intermediate milestone is implemented in `src/simulation`, `simulation_tx`, and `simulation_rx`, with usage in `docs/SIMULATION.md`, protocol details in `docs/PROTOCOL.md`, and setup/testing in `docs/DEVELOPMENT.md`. The user approved retiring the original telemetry-only applications and smoke runner while retaining the telemetry codec and useful test coverage. Generic UDP, clock, and bounded sequence facilities live in `src/common`. Share core code and use source/transport adapters for future hardware integration; avoid duplicating a separate hardware codebase. Normal CTest tests need no sockets, Python, or media tools. Run integration explicitly through uv: the default standard suite covers core behavior; `--suite extended` adds outages, restarts, sustained reception, and buffer pressure. Do not duplicate integration registration in CMake. `tools/simulation_smoke.py` requires localhost sockets, Python 3, FFmpeg with libx264, and FFprobe. New packet handling must retain explicit serialization, bounded state, whole-access-unit output, and restart/loss tests. Host simulation success is not a verified Luckfox build.

When the user confirms a material architecture decision or a milestone is verified, update the relevant sections of `docs/PROJECT_CONTEXT.md` with the decision or test evidence. Label untested work clearly and preserve unresolved questions.
