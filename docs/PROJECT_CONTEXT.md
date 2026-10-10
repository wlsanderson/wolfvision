# Wolfvision project context

Updated: October 9, 2026

## Confirmed purpose and v0.1 goal

`wolfvision` is the complete digital video and telemetry project, with an
airborne transmitter and ground receiver. Rocket video/telemetry is the working
application context from earlier “Planning V01” discussions. Luckfox is a selected
development platform, not the project's identity.

The user defines **hardware v0.1** as an integrated bench prototype:

```text
Live camera -> Luckfox Pico Max (256 MB): H.264 encoding ---+
External data source -> Luckfox UART ----------------+-> video/data packetization
 -> airborne E103-RTL8812CU -> wireless link
 -> ground E103-RTL8812CU -> USB -> laptop receiver
```

Direct camera input, H.264, UART data input, and both E103-RTL8812CU modules
are confirmed intended choices. The user has now specified:

- Board: Luckfox Pico **Max**, the 256 MB RAM variant of the Pro/Max family.
- Downloaded image: `Luckfox_Pico_ProMax_MicroSD_250607` (name as reported);
  microSD is the selected boot medium. Flashing/boot and image contents are unverified.
- Camera: Luckfox SC3336 3 MP camera.
- Airborne radio connection: E103-RTL8812CU over USB; Luckfox must use USB host mode.

The user confirmed the Luckfox boards are still on order and unavailable.
Current SDK setup and compilation run entirely on the laptop; flashing, boot,
and physical integration must wait for the hardware.

The board/radio/camera integration and throughput remain unverified. The exact
SDK revision corresponding to the downloaded image has not been established.

Software must be tested on Ubuntu/WSL and run on Luckfox Pico Max. A compatible
board build and successful board execution require separate evidence from the
host build. Prerecorded video and synthetic UART are the authorized intermediate
simulation milestone, not substitutes for hardware v0.1 camera/UART input.

Proposed functional acceptance is decoded camera video and UART data checked
against the source at the laptop through the entire actual hardware path.
The user asked for help refining this: playback versus recording, duration,
resolution/frame rate/bitrate, latency, loss/recovery thresholds, and thermal/power
criteria still need agreement. FEC, GUI, uplink, range goals, and flight
qualification are not confirmed v0.1 acceptance requirements.

## Development decisions

- C++17 and CMake for applications/shared core; Python for automation and analysis.
- Use uv for repository Python tooling: `uv sync --locked`, `uv run --locked`,
  root `pyproject.toml`, `.python-version` (3.12), and `uv.lock`. Current scripts
  use the standard library. FFmpeg/FFprobe are separate system tools.
- The user requires formatting/lint checks whenever code is written or changed.
  uv locks clang-format/clang-tidy for C++ and Ruff for Python. Shared commands
  are `tools/check_code.py --fix` and `tools/check_code.py` through uv;
  [AGENTS.md](../AGENTS.md#required-formatting-and-linting) records the required workflow.
- Keep sources, serialization, transport, and hardware adapters separate. Share
  the tested core with future hardware adapters instead of creating parallel
  simulation/hardware codebases.
- The user moved from Ubuntu 24.04 to Ubuntu 22.04 for SDK setup. On October 9,
  2026, this workspace reported Ubuntu 22.04.5 LTS, x86-64, and a WSL 2 kernel.
  The repository is at `/home/wlsanderson/wolfvision`; its remote URL has not
  been independently verified.
- Use the current Ubuntu 22.04 WSL environment for SDK setup. Current
  [vendor documentation](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/SDK/)
  specifies Ubuntu 22.04 x86-64, now verified for this workspace. The SDK checkout
  is now verified at `/home/wlsanderson/sdk/luckfox-pico`, outside the Wolfvision
  repository. Board configuration selection and supplied compiler startup are
  verified. The user reported a successful full SDK build, and generated images
  were independently inspected; board execution remains unverified. The
  resumable setup steps are in [LUCKFOX_SDK.md](LUCKFOX_SDK.md).
- Recommended SDK family: [LuckfoxTECH/luckfox-pico](https://github.com/LuckfoxTECH/luckfox-pico),
  configured for `RV1106_Luckfox_Pico_Pro_Max`, `SD_CARD`, and `Buildroot`.
  The current shared board configuration is
  `BoardConfig-SD_CARD-Buildroot-RV1106_Luckfox_Pico_Pro_Max-IPC.mk` and selects
  `arm-rockchip830-linux-uclibcgnueabihf`. This is a documented starting point,
  not a verified match to the `250607` image or a confirmed project SDK pin.
  Establish the image/SDK/kernel/media-library compatibility and record the
  working SDK commit before claiming a board build or building radio modules.
- The user approved repository cleanup: retire the original telemetry-only apps,
  retain their reusable codec/tests, move generic facilities to `src/common`,
  shorten the README, and split detailed guides. CTest covers quick C++ tests;
  explicit uv commands select standard or extended integration.

## Implemented host simulation

The shared core and `simulation_tx` / `simulation_rx` provide:

- Streaming prerecorded Annex B H.264, with AUD boundaries and SPS/PPS at IDRs;
  initial profile has no B frames. C++ does not encode or decode video.
- Clearly labelled synthetic UART lines parsed across partial chunks, then
  serialized with the retained WVTM telemetry codec. No real serial port is opened.
- A WVSM video/telemetry/end envelope over one numeric IPv4 UDP endpoint, explicit
  big-endian bytes, session/sequence/monotonic timestamps, and fragmentation into
  at most 1148-byte datagrams. These bench formats do not settle hardware transport.
- Configurable addresses/ports/rates, finite replay loops, and receiver session limits.
- Per-session H.264, CSV, and JSON; whole-unit output, duplicate suppression,
  distinct gap/late/stale reporting, and missing/invalid-unit recovery at an IDR.
- Bounded state: 1 MiB per video unit, 64-unit reassembly window, 4 MiB retained
  fragment payload, 4096 sequence numbers, 32 retired session IDs. Temporary
  buffers/metadata and OS buffers are additional costs; 4 MiB is not an RSS cap.
- A seeded, bounded UDP proxy for loss, bursts, jitter, duplicates, outages,
  deterministic video-unit drops, and queue-overflow counters.

Usage is in [SIMULATION.md](SIMULATION.md), wire rules in
[PROTOCOL.md](PROTOCOL.md), and setup/testing in [DEVELOPMENT.md](DEVELOPMENT.md).

## Verification evidence

SDK setup inspection on October 9, 2026: checkout commit
`824b817f889c2cbff1d48fcdb18ab494a68f69d1`, with a clean tracked working tree.
The user reported successful `build_select_board`; `.BoardConfig.mk` was
independently inspected and resolves to
`BoardConfig-SD_CARD-Buildroot-RV1106_Luckfox_Pico_Pro_Max-IPC.mk`.
The supplied `arm-rockchip830-linux-uclibcgnueabihf-gcc` and `g++` both ran and
reported GCC 8.3.0 (crosstool-NG 1.24.0). The inspected commit is now the
verified compilation baseline; firmware boot and compatibility with the
downloaded `250607` image remain unverified.

The first SDK build attempt reached Buildroot rootfs preparation but stopped
at its dependency check because PATH contained whitespace. This was reported
by the user. A build-scoped PATH
containing only Linux system directories and the SDK compiler directory was
checked to contain no whitespace and to resolve host make/GCC/G++ and both
ARM compilers. The user subsequently reported that all builds succeeded. An
independent inspection found nonempty `boot.img`, `rootfs.img`, `uboot.img`,
`idblock.img`, `env.img`, `userdata.img`, `update.img`, and `sd_update.img` under
`/home/wlsanderson/sdk/luckfox-pico/output/image/`. `file` identifies `rootfs.img`
as ext4. The terminal success output was reported by the user; no saved build
log was found in the inspected locations. The SDK also archived images and
`build_info.txt` under
`IMAGE/IPC_SD_CARD_BUILDROOT_RV1106_LUCKFOX_PICO_PRO_MAX_20261009.1910_RELEASE_TEST/`;
the build metadata confirms RV1106, SD card, ext4/Buildroot, and the selected
uClibc toolchain. This records successful SDK compilation
with artifact evidence, not boot or physical hardware validation; see
[SDK troubleshooting](LUCKFOX_SDK.md#5-build-the-sdk-image).

Wolfvision cross-build on October 9, 2026: CMake configured a separate
`build-luckfox` Release build for Linux/ARM with the SDK's GCC 8.3.0 C++ compiler
and `BUILD_TESTING=OFF`. All three shared libraries and `simulation_tx` /
`simulation_rx` compiled and linked successfully without source changes.
`file` and `readelf` confirm ARM 32-bit EABI5 hard-float executables, using
`/lib/ld-uClibc.so.0`; both require `libstdc++.so.6`, `libgcc_s.so.1`, and
`libc.so.0`. These library names and the loader are present in the SDK-generated
rootfs staging directory. Runtime compatibility and board execution remain
unverified; ARM tests were not executed on the x86-64 laptop. These are the
existing prerecorded-video/synthetic-UART applications, not live camera adapters.

Camera preparation on October 9, 2026: reviewed the installed SDK's small
VI-to-VENC/RTSP example, maintained VENC/ISP helpers, encoder headers, sensor
device-tree configuration, and generated rootfs. The SC3336 module and
`sc3336_CMK-OT2119-PC1_30IRC-F16.json` IQ file are packaged. Independently
cross-compiled and linked the unchanged `simple_vi_bind_venc_rtsp.c` against
SDK-generated media headers/libraries; the ARM binary is at
`build-luckfox-camera-review/simple_vi_bind_venc_rtsp`. No example was executed.
[LUCKFOX_CAMERA.md](LUCKFOX_CAMERA.md) records the API/source mapping, proposed
bounded adapter design, and future board test procedure. AUD/SPS/PPS behavior,
pack/offset/frame semantics, PTS clock compatibility, and camera operation remain
unverified. Live inputs require a protocol/profile update because existing WVSM
and WVTM flags explicitly identify simulation. No adapter or wire changes have
been implemented.

The original telemetry-only milestone was built/tested on October 8, 2026,
including golden packet bytes, malformed inputs, sequence cases, CSV values,
and finite localhost reception. Its apps, CLI parser, unbounded sequence tracker,
and separate smoke runner were retired during the approved cleanup. Codec tests
remain; bounded sequence and simulation guard tests preserve useful coverage.

The simulation was verified on October 9, 2026 with genuine FFmpeg/libx264 video
and independent decoding. Clean and reordered/duplicated recordings were
byte-exact and decoded all 60 frames. Dropping unit 5 produced one missing unit,
four dependent units skipped, and 55 decoded frames. Random loss, burst loss,
outage, transmitter restart, receiver restart, and a finite sustained run passed.
The former artificial slow-receiver check did not establish overload and was
replaced by a deliberate fragment-budget pressure/recovery test.

Cleanup verification: the host build passed, CTest passed all three
executables (shared sequence, telemetry codec, simulation), five Python preflight
tests passed, and the standard integration suite passed receiver guards plus
clean, reordered/duplicated, and deterministic-loss runs. CTest took 0.46 seconds;
the standard suite took approximately 12 seconds and extended approximately 49.
CSV checks confirm unique samples and increasing timestamps in sample order.
Receiver guards cover
signed CSV measurements, distinct sequence events, malformed nested packets,
timestamp mismatch, and timeout despite ongoing invalid/duplicate traffic.
The extended suite also passed all ten video scenarios plus receiver guards.
The buffer-pressure case retained at most 4,194,300 payload bytes (below 4 MiB),
counted five missing units, and recovered one exact, independently decoded IDR.

The sustained fixture uses 40 loops / 2400 access units at 240 units/s, roughly
10 seconds of small 160×120 video. The cleanup run decoded all 2400 frames, with a
6195-byte peak retained payload and bounded sequence history; settled Linux RSS
growth also passed. This is not a long-duration soak or high-resolution throughput
qualification. Captures/reports go into fresh `build/simulation-*` directories.
No physical camera, actual UART, board execution, or radio integration has
been verified. SDK and Wolfvision ARM compilation are recorded above.
System FFmpeg/FFprobe were available in the earlier workspace;
Python dependencies remain managed separately by uv. These test results describe
the earlier environment. After the Ubuntu 22.04 move, development tools were
initially missing. Subsequent SDK setup restored host `make`, GCC/G++, and
CMake, confirmed by executable lookup. The new workspace has not rerun the
host tests; check uv and media-tool availability before running them.

Formatting/lint setup was subsequently verified: the repository gate passed
clang-format, clang-tidy, and Ruff checks after formatting the existing baseline
and resolving findings. Deliberate formatting errors, an undefined Python name,
a C++ null dereference, and a missing compilation database correctly failed.
The rebuilt three C++ tests, five Python tests, and standard integration suite
also passed. Formatting/lint requirements are recorded in `AGENTS.md`.

## Open decisions before hardware integration

| Area | Information still needed |
| --- | --- |
| Luckfox | SDK and ARM application compilation verified; verify board boot/runtime, downloaded-image compatibility, and firmware choice for first boot |
| Camera/encoder | Validate selected SC3336 camera and vendor capture/H.264 APIs; choose resolution, frame rate, bitrate, keyframe policy |
| UART | Source device, electrical levels, wiring, baud rate, framing, fields/units, acquisition rates |
| Radio | Configure selected USB host connection; verify driver, USB power/cabling, wireless mode, transport mapping, channel/frequency, bandwidth, power, antennas |
| Receiver | Laptop OS, live playback/recording behavior, device integration |
| Protocol/time | Hardware packet requirements, synchronization, cross-device latency measurement, FEC requirement |
| Acceptance | Bench duration, decoded video/data criteria, latency/loss/recovery thresholds, power/thermal limits |

While the boards are on order, SDK image compilation and the Wolfvision ARM
cross-build and the vendor-camera source review/example cross-build are complete.
The proposed adapter and board bring-up steps are in
[LUCKFOX_CAMERA.md](LUCKFOX_CAMERA.md). Portable normalization and live-input
protocol work can proceed on the laptop; restore the host test environment as
needed. When hardware arrives, select the
firmware for first boot, verify boot/runtime compatibility, run the ARM
applications, then test the vendor SC3336/H.264 example before writing board
adapters. The
[RKMPI examples](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/MPI/) explicitly
list RV1106, Buildroot, and SC3336; these combinations remain untested here.
The [USB guide](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/USB/) documents
`luckfox-config` -> Advanced Options -> USB -> host, followed by reboot.
USB gadget networking is unavailable in host mode; establish Ethernet SSH or
serial access before switching. Host mode alone does not verify the radio driver
or wireless operating mode.
Define real UART framing before adding a serial adapter. Once APIs are selected,
add live camera/encoder and transport adapters around the shared core and agree
on hardware bench pass/fail criteria. Extend fixtures to representative video
sizes and longer stress tests as integration needs become concrete.

## Earlier proposals and future scope

Earlier “Planning V01” suggestions remain options, not commitments:

- Functional camera/encoder/link processor/RF chain separation; a supervisor MCU
  for sequencing, watchdogs, interfaces, temperature/voltage, and power control.
- Ground omni/patch/directional antennas with switching/diversity alternatives;
  HDMI/USB/network outputs, recording, external SMA, and a low-rate control uplink.
- FEC, interleaving, adaptive bitrate, and richer link statistics.
- Later portable range and flight prototypes, followed by integrated V1 hardware.

Suggested numerical targets were 720p/1080p and under 300 ms for v0.1;
1080p30, roughly 150–200 ms, and several kilometres for V1, with a proposed
extended link distance over 15,000 ft using a directional receive antenna.
None are confirmed or demonstrated; the distance does not establish flight altitude.
Frequency/power/link budget remain open. Fiberglass airframe, small airborne
antenna, rotation/tumbling, and parachute reception are historical working
assumptions to confirm with the vehicle design.

RF margin, polarization/nulls, diversity, power/thermal behavior, vibration,
acceleration, and actual camera-to-display latency need physical measurements.
Host impairment tests cannot establish RF range, antenna performance, or flight
reliability.

## Context provenance

This document summarizes this IDE conversation, “Set up Luckfox development,”
and two recovered “Planning V01” exchanges. “Project Waiting Tasks” was listed
but not readable and is not incorporated. It is not a complete import of all
historical chats. New explicit user decisions and inspected code/test evidence
supersede older recommendations. Preserve the distinction between implemented,
verified, planned, and unresolved work when updating this context.
