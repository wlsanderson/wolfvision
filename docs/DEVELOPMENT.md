# Development

Run commands from the repository root in Ubuntu/WSL. The main
[README](../README.md) contains the normal build and standard integration commands.

## Setup

Install build and media tools in the Ubuntu distribution where Wolfvision runs:

```sh
sudo apt update
sudo apt install cmake g++ curl ffmpeg
```

The `ffmpeg` package supplies FFmpeg, FFprobe, and FFplay. The integration fixture
requires the libx264 encoder. Media tools are system executables; uv does not
install them. C++ builds and CTest do not require media tools or Python.
CMake supports core builds from 3.16; the documented `ctest --test-dir` command
requires 3.20 or newer.

If `uv --version` fails, use the [official uv installer](https://docs.astral.sh/uv/getting-started/installation/):

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh
source "$HOME/.local/bin/env"
```

Create the Python environment and check media executables:

```sh
uv sync --locked
ffmpeg -version
ffprobe -version
```

Python tools use `.venv` through `uv run --locked`. `pyproject.toml`,
`.python-version`, and `uv.lock` define the environment: Python 3.12, currently
standard library only at runtime, with formatting/lint tools as development
dependencies. No activation or manual pip installation is needed.
Use `uv add --dev PACKAGE` for future test/analysis dependencies and retain the
updated lockfile. Python subprocesses use `sys.executable` to keep this environment.

## Formatting and linting

Development tools are installed by `uv sync --locked`: [clang-format](https://clang.llvm.org/docs/ClangFormat.html)
for C++, [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) for C++ analysis,
and [Ruff](https://docs.astral.sh/ruff/) for Python formatting/linting. The LLVM
executables are supplied by uv-managed PyPI packages, so separate system LLVM
installation is unnecessary. `uv.lock` records the exact tool versions.

Configure the host compilation database, then format and check:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
uv run --locked tools/check_code.py --fix
uv run --locked tools/check_code.py
```

The checker exits nonzero on formatting differences or lint findings. `--fix`
formats C++ and Python and applies safe Ruff fixes; remaining findings require
manual review. It does not automatically apply clang-tidy or unsafe Ruff fixes.
Run it whenever C++ or Python changes, then run the relevant tests.

C++ uses an LLVM-based style with four-space indentation and a 100-column
limit. Analysis enables Clang's analyzer, bug-prone, performance, and portability
checks, treating project findings as errors. Existing `#pragma once` headers
are supported by our GCC/Clang targets; API parameter-order redesign is outside
the gate. Those two rules are explicitly excluded in `.clang-tidy`.
Python uses a 100-column Ruff style with syntax/unused-code, import ordering,
bug-prone, and Python 3.12 modernization checks in `pyproject.toml`.

CMake exports `build/compile_commands.json`, giving clang-tidy the actual C++17
flags and include paths. Pass `--build PATH` for another configured host build.
If the database is missing/stale or omits tests, reconfigure with
`-DBUILD_TESTING=ON`. Formatting/lint tools are development dependencies;
C++ build/CTest and application runtime do not require them. Generated builds,
caches, and the virtual environment are excluded from Python scans.

## Testing

There are three independent test layers:

| Check | Command | Coverage / typical duration |
| --- | --- | --- |
| C++ unit tests | `ctest --test-dir build --output-on-failure` | Packet bytes/malformed inputs, UART parsing, recovery, deadlines, bounds, sequence classification; under 1 second |
| Standard integration | `uv run --locked tools/simulation_smoke.py build` | Receiver guards, clean recording, reorder/duplicates, deterministic loss; about 12 seconds |
| Extended integration | `uv run --locked tools/simulation_smoke.py build --suite extended` | Standard plus random/burst loss, outage, both restarts, sustained reception, buffer pressure; about 50 seconds |

CTest registers only C++ tests. Integration runs explicitly through the Python
script; no CMake integration switches or media-path cache settings are needed.
The extended suite includes standard checks, so running both is unnecessary for
a full validation. Use standard during ordinary development and extended for
changes affecting recovery, buffering, sessions, or transport.

Fast Python dependency-check tests and a media-free receiver guard check:

```sh
uv run --locked tests/test_tooling.py
uv run --locked tools/simulation_smoke.py build --guards-only
```

Guards-only still requires localhost UDP permission. The media suites generate
fresh captures under `build/simulation-*`, decode with FFmpeg treating errors as
failures, and write `results.json`, logs, proxy statistics, CSV, and H.264 files.
The extended sustained check defaults to 40 loops / 2400 access units at 240/s;
`--sustained-loops N` changes the finite duration. It checks internal bounds and
settled Linux process RSS growth where `/proc` is available. This small fixture
is not a high-resolution throughput qualification or long-duration soak.

The buffer-pressure case sends five incomplete 1 MiB units, forcing the 4 MiB
reassembly budget to evict data, then sends a genuine IDR. It verifies bounded
state, missing-unit accounting, exact recovered bytes, and independent decoding.
Injection is paced to isolate application eviction from kernel socket overflow.
The former artificial slow-receiver case and its `--slow-ms` option were removed.

## Troubleshooting

If FFmpeg/FFprobe are absent, install the system `ffmpeg` package above. For
executables outside PATH, use explicit paths:

```sh
uv run --locked tools/simulation_smoke.py build \
  --ffmpeg /path/to/ffmpeg --ffprobe /path/to/ffprobe
```

Preflight checks the built applications, media executables, and libx264 before
opening sockets or creating captures. Socket-restricted environments can run
CTest and the Python dependency tests, then run integration in a normal terminal.
Missing `simulation_tx`/`simulation_rx` means the CMake build must run first.

## Source layout and board builds

- `src/common`: opaque POSIX UDP transport, monotonic clock, bounded sequence history.
- `src/telemetry`: measurement representation/generator and the nested telemetry codec.
- `src/simulation`: file/UART sources, video envelope, reassembly, and CLI parsing.
- `src/simulation_tx.cpp`, `src/simulation_rx.cpp`: application orchestration.
- `tests`: independent codec fixtures, parser/recovery/bounds tests, tooling preflight checks.
- `tools`: integration runner and bounded UDP impairment proxy.

The original telemetry-only applications and smoke runner are retired. Their
codec tests remain, sequence coverage uses the shared bounded tracker, and CSV
value/sequence checks now live in the simulation receiver guards.

Keep this shared core for future camera, UART, and radio adapters. The selected
target is Pico Max (256 MB), SC3336 camera, microSD image
`Luckfox_Pico_ProMax_MicroSD_250607`, and USB host connection to E103-RTL8812CU.
An SDK revision matching that image is not yet verified.

Use the official [Luckfox Pico SDK](https://github.com/LuckfoxTECH/luckfox-pico)
in a separate checkout, with the RV1106 Pro/Max, SD_CARD, Buildroot configuration.
Luckfox documents Ubuntu 22.04 x86-64 as the
[SDK build environment](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/SDK/)
and provides a [Docker build environment](https://wiki.luckfox.com/Luckfox-WebBee/Docker-Image-Build/).
The SDK Git revision, firmware image date, and Docker image version are separate;
record the working SDK commit and establish kernel/media-library compatibility.
Application builds need the supplied ARM toolchain and matching libraries;
radio kernel modules additionally need the matching kernel configuration/build.

A host build or CMake configuration change does not verify board compilation
or execution. See [project context](PROJECT_CONTEXT.md) for confirmed hardware
choices, the documented USB host-mode setup, and outstanding validation.
