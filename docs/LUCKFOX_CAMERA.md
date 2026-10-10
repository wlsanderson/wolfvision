# SC3336 and H.264 adapter review

Reviewed October 9, 2026 against SDK commit
`824b817f889c2cbff1d48fcdb18ab494a68f69d1`, with the Pro/Max SD_CARD Buildroot
configuration. The boards are still on order. This document records source and
artifact inspection, a successful vendor-example cross-build, a proposed adapter
design, and future board checks. Camera capture and encoding have not run here.

## Findings and reference examples

[Luckfox's RKMPI guide](https://wiki.luckfox.com/Luckfox-Pico-Pro-Max/MPI/)
lists RV1106, Buildroot, and SC3336 examples. The installed SDK provides a more
direct starting point for Wolfvision than its OpenCV/AI demos:

```text
SC3336 -> ISP/RKAIQ -> VI channel -> VENC H.264 -> encoded stream
                                                   |
                                      bounded access-unit adapter
                                                   |
                              shared packetization -> transport
```

Use the hardware VI-to-VENC binding initially. Introduce VPSS if scaling or
format conversion needs it after board testing. The planned transmitter needs
no application-side raw RGB processing for this path.

All SDK paths below are relative to `/home/wlsanderson/sdk/luckfox-pico`.

| Reference | What it establishes |
| --- | --- |
| [`media/samples/simple_test/simple_vi_bind_venc_rtsp.c`](https://github.com/LuckfoxTECH/luckfox-pico/blob/824b817f889c2cbff1d48fcdb18ab494a68f69d1/media/samples/simple_test/simple_vi_bind_venc_rtsp.c) | Small single-channel example with RKAIQ initialization, VI-to-VENC binding, H.264 output, RTSP, and stream release |
| `media/samples/simple_test/simple_vi_bind_venc.c` | Small direct capture/encode/file example; it does not initialize RKAIQ itself |
| [`media/samples/example/demo/sample_demo_vi_venc.c`](https://github.com/LuckfoxTECH/luckfox-pico/blob/824b817f889c2cbff1d48fcdb18ab494a68f69d1/media/samples/example/demo/sample_demo_vi_venc.c) | More comprehensive reference for encoder frame rates, normal-P GOP, output files, and teardown; also enables extra channels/features |
| `media/samples/example/common/sample_comm_venc.c` | Encoder creation, explicit rate-control fields, stream acquisition/release, and buffer settings |
| `media/samples/example/common/isp3.x/sample_comm_isp.c` | Sensor discovery, IQ directory selection, RKAIQ prepare/start/stop, and ISP frame-rate controls |
| [`media/rockit/rockit/mpi/sdk/include/rk_comm_venc.h`](https://github.com/LuckfoxTECH/luckfox-pico/blob/824b817f889c2cbff1d48fcdb18ab494a68f69d1/media/rockit/rockit/mpi/sdk/include/rk_comm_venc.h) | Pack/frame metadata and encoder attribute definitions |
| `media/samples/simple_test/Makefile` | Include paths, RV1106/UAPI2 defines, and link dependencies for the small example |

The SDK's `media/samples/README` recommends `example/` as the maintained
reference and describes `simple_test/` as quick functional examples. Use the
small example for first camera bring-up and consult the maintained helpers for
configuration; copying either entire application would bring unrelated behavior
into Wolfvision.

The selected source device tree includes an enabled SC3336 node, camera module
`CMK-OT2119-PC1`, and lens `30IRC-F16`. The generated rootfs staging tree contains:

- `/oem/usr/ko/sc3336.ko`
- `/oem/usr/share/iqfiles/sc3336_CMK-OT2119-PC1_30IRC-F16.json`
- `/oem/usr/bin/simple_vi_bind_venc_rtsp`
- `/oem/usr/bin/sample_demo_vi_venc`
- `/oem/usr/lib/librockit.so` and `/oem/usr/lib/librkaiq.so`

These files establish build preparation, not successful driver loading, sensor
identification, tuning compatibility with the delivered camera, or video output.

## Laptop verification performed

The untouched `simple_vi_bind_venc_rtsp.c` was compiled and linked with SDK GCC
8.3.0 and the generated `output/out/media_out` headers/libraries. The result is
`build-luckfox-camera-review/simple_vi_bind_venc_rtsp` in Wolfvision's ignored
build directory. `file` and `readelf` identify ARMv7, EABI5 hard-float, and the
`/lib/ld-uClibc.so.0` interpreter. Its direct dynamic dependencies are
`librockit.so`, `librockchip_mpp.so.1`, `librga.so`, `libstdc++.so.6`,
`librkaiq.so`, and `libc.so.0`. RTSP links from the SDK static archive.

Reproduce on the laptop from the Wolfvision repository:

```sh
mkdir -p build-luckfox-camera-review
WOLFVISION_SDK_ROOT=/home/wlsanderson/sdk/luckfox-pico
WOLFVISION_MEDIA_ROOT="$WOLFVISION_SDK_ROOT/output/out/media_out"
"$WOLFVISION_SDK_ROOT/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc" \
  "$WOLFVISION_SDK_ROOT/media/samples/simple_test/simple_vi_bind_venc_rtsp.c" \
  -o build-luckfox-camera-review/simple_vi_bind_venc_rtsp \
  -DRKAIQ -DUAPI2 -DRV1106_RV1103 -Os \
  -march=armv7-a -mfpu=neon -mfloat-abi=hard \
  -I"$WOLFVISION_MEDIA_ROOT/include" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/uAPI2" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/common" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/xcore" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/algos" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/iq_parser" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/iq_parser_v2" \
  -I"$WOLFVISION_MEDIA_ROOT/include/rkaiq/smartIr" \
  -L"$WOLFVISION_MEDIA_ROOT/lib" \
  -Wl,-rpath-link,"$WOLFVISION_MEDIA_ROOT/lib" \
  -lpthread -lm -lrockit -lrockchip_mpp -lrga -lstdc++ -lrkaiq -lrtsp
file build-luckfox-camera-review/simple_vi_bind_venc_rtsp
```

No vendor or Wolfvision C++/Python source was modified. No ARM example was
executed on the laptop. The existing SDK-packaged binary is already available
for the first board test; the separate compilation confirms that the headers
and libraries needed by this example are usable.

## Adapter contract and API mapping

The following is a proposed implementation plan, not implemented functionality
or a confirmed video performance target.

| Stage | SDK APIs / behavior | Wolfvision responsibility |
| --- | --- | --- |
| ISP | RKAIQ sensor enumeration, `rk_aiq_uapi2_sysctl_init`, `prepare`, `start`; existing `SAMPLE_COMM_ISP_*` helpers show the sequence | One owner of the camera/ISP; verify sensor and IQ selection, check every result |
| System/VI | `RK_MPI_SYS_Init`, VI device attributes/enable/pipe binding, `RK_MPI_VI_SetChnAttr`, `RK_MPI_VI_EnableChn` | Configurable camera/pipe/channel, YUV420SP/DMABUF initially, explicit bounded buffers |
| Encoder | `RK_MPI_VENC_CreateChn`, `RK_MPI_VENC_StartRecvFrame`, `RK_MPI_SYS_Bind` | H.264, explicit bitrate/FPS/GOP/profile, initially normal-P and no B frames; confirm output on hardware |
| Output | `RK_MPI_VENC_GetStream`, `RK_MPI_MB_Handle2VirAddr`, `RK_MPI_VENC_ReleaseStream` | Copy complete encoded units into owned bounded storage; release each acquired stream on every exit path |
| Recovery | `RK_MPI_VENC_RequestIDR` is declared | Request a recovery point after local drops/restarts; periodic IDRs remain necessary without an uplink |
| Shutdown | Unbind, stop receiving, destroy encoder, disable VI, stop/deinitialize ISP, system exit | Cancel workers with finite waits and unwind only resources successfully acquired |

Start with a source result that distinguishes access unit, timeout, end, and
error. The unit owns Annex B bytes and carries encoder PTS separately from the
sender's monotonic timestamp. Vendor headers/types belong inside a Luckfox
adapter; the packetizer and portable tests should see only the portable unit.

Keep sources, packet serialization, and transport separate. Extract reusable
video inspection/normalization and packet facilities from `src/simulation` into
shared modules when implementing the adapter; keep file replay and synthetic
UART as simulation adapters. Add the Luckfox target as an opt-in SDK-dependent
build. Host builds must continue without vendor headers/libraries. This adds a
hardware adapter around the shared core, rather than a second transmitter stack.

### Frame boundaries and ownership

The header defines `VENC_STREAM_S.u32PackCount` and pack fields `u32Len`,
`u32Offset`, `u64PTS`, `bFrameEnd`, and `bStreamEnd`. The simple samples allocate
one pack and forward its bytes. That is insufficient evidence that every future
encoder configuration returns one complete access unit in one pack.

The adapter must bound pack allocation and accumulated bytes, handle all returned
packs, and use verified frame-end metadata. Inspect actual pack counts, offsets,
lengths, sequence numbers, and PTS during the first board capture. The examples
do not exercise nonzero offsets; confirm the length/offset interpretation before
implementing that case. Check the valid span against `RK_MPI_MB_GetSize` and
reject inconsistent metadata. Use `RK_MPI_VENC_QueryStatus`/frame mode only after
confirming their allocation and output behavior for this RV1106 implementation.

Copy encoded data before releasing the stream. Never keep a vendor memory-block
pointer in a network queue after `ReleaseStream`. Use a finite `GetStream`
timeout so shutdown and UART service cannot be delayed indefinitely. The simple
RTSP sample uses 200000 ms; the common helper retries forever. Neither behavior
should be carried into the production adapter.

### H.264 normalization and recovery

Wolfvision's current `inspect_video`/reassembler require one leading AUD per
access unit, and SPS/PPS alongside every IDR. The reviewed example contains no
explicit AUD or repeated-header configuration. There is no captured output yet
to establish Annex B framing or repeated parameter sets.

The normalizer should:

1. Verify Annex B output and complete access-unit boundaries before emitting.
2. Insert a valid AUD when missing, with syntax consistent with the unit's
   slice types; avoid duplicating an existing AUD.
3. Retain bounded SPS/PPS state and include the parameter sets referenced by
   each IDR. Do not assume the latest PPS belongs to every slice. Reset state
   after a restart or incompatible configuration change.
4. Start delivery only with an IDR and its matching parameter sets. Drop a
   whole oversized/malformed/incomplete unit, then gate dependent video until
   recovery. Keep the existing 1 MiB unit limit, including inserted headers.
5. Verify the resulting stream with an independent decoder. Requesting an IDR
   alone does not establish that SPS/PPS were repeated.

Use explicit normal-P GOP configuration and verify no B frames in a captured
stream. The small example selects High profile and GOP 60, but does not explicitly
set encoder frame-rate fields or GOP mode. High profile alone does not determine
frame reordering. The maintained VENC helper shows source/destination FPS
numerators/denominators; bitrate fields are documented in kbps.

### Scheduling, protocol, and bounded state

The current transmitter paces file replay with `--fps` and services synthetic
UART in that loop. Live encoding supplies its own cadence. Service camera output
with finite waits or a worker, and service real UART independently so a camera
timeout cannot stop telemetry.

A proposed starting queue is at most two complete units and 2 MiB total encoded
payload, in addition to a single bounded unit under assembly. Vendor VI/VENC/CMA
buffers, temporary fragmentation copies, metadata, and OS buffers are additional
memory. Validate actual usage on the 256 MB board; these values are application
limits proposed for implementation, not a measured process-memory budget.

When overloaded, drop complete units, count them, clear dependent queued video,
and wait/request an IDR instead of growing a stale-video queue. Preserve missing
unit accounting when assigning IDs to locally dropped frames. Test this policy
with the existing receiver's recovery behavior.

**Protocol work is required before sending live inputs.** WVSM v1 hardcodes flag
1 as simulated inputs, and WVTM v1 marks its measurements simulated. Decoders
reject other flags. Define a version/profile that distinguishes live video and
real versus synthetic telemetry, and update both endpoints and tests together.
Retain explicit serialization, bounded state, and whole-access-unit output.
The real UART fields/framing remain unresolved; do not silently reuse `SIM,...`
as the actual device protocol.

Keep encoder PTS available for ordering/diagnostics. Its epoch has not been
verified against Wolfvision's `steady_clock`. Do not turn board PTS or a
cross-device clock subtraction into a camera-to-display latency measurement.

## First board test, when hardware arrives

First select and boot the firmware to test. Prefer a single documented
image/toolchain/library combination for initial bring-up; compatibility of the
older downloaded `250607` image with this SDK remains unresolved. Confirm SSH or
serial access before later USB host-mode work.

Run these inspection commands **on the board**, after attaching the camera with
power off and booting:

```sh
uname -a
free -m
lsmod
dmesg | tail -n 100
ls -l /dev/video* /dev/media*
ls -l /oem/usr/share/iqfiles/*sc3336*
ps
```

Check sensor identification, camera/ISP/encoder driver readiness, IQ selection,
and any camera errors. A module absent from `lsmod` can be built into the kernel;
use boot messages and device nodes as well.

The generated image starts `rkipc` through `S21appinit`/`RkLunch.sh`. Stop that
camera owner gracefully with `killall -TERM rkipc` if it is running, then use
`ps` to confirm it exited before launching another camera program. The SDK's
`RkLunch-stop.sh` also kills `udhcpc`, so using it would affect DHCP networking.
The inspected `rkipc` handles SIGTERM/SIGINT. Keep this a temporary bring-up step;
do not change the startup configuration yet.

After verifying paths on the actual board, a suggested finite smoke command is:

```sh
/oem/usr/bin/simple_vi_bind_venc_rtsp \
  -I 0 -w 1920 -h 1080 -e h264 -b 2048 -c 900 \
  -a/oem/usr/share/iqfiles
```

The attached `-a/path` form is deliberate: this sample uses an optional getopt
argument. In its implementation `-I` selects the VI channel, while the ISP camera
ID stays zero. `-c 900` stops after more than 900 successful stream acquisitions,
not an exact time or guaranteed frame count. The resolution/bitrate are suggested
bring-up settings, not agreed bench acceptance targets; validate the camera mode
and actual output FPS. Stop with Ctrl-C if needed, allowing for the sample's long
stream wait on failure. RTSP is only the diagnostic transport for this test.

On the laptop, after installing/checking system FFmpeg and FFprobe, replace
`BOARD_IP` below with the board's actual IP. Start recording while the board
example is running:

```sh
ffmpeg -rtsp_transport tcp -i rtsp://BOARD_IP/live/0 \
  -t 10 -an -c:v copy camera-smoke.mkv
ffprobe -v error -select_streams v:0 -count_frames \
  -show_entries stream=codec_name,width,height,profile,has_b_frames,avg_frame_rate,nb_read_frames \
  -of json camera-smoke.mkv
ffmpeg -v error -xerror -i camera-smoke.mkv -f null -
```

This recording can establish independent decode, image dimensions, and observed
frame count. RTSP may repackage headers; validating the adapter's AUD/SPS/PPS
and access-unit boundaries additionally requires raw encoder bytes and pack
metadata collected before RTSP. Visual quality, camera identification, encoder
PTS, memory, termination/restart behavior, and no-B-frame policy also need checks.
RF range and physical latency require later measurements through the actual link.

## Implementation and validation order

1. Implement the portable access-unit normalizer and source contract, with tests
   for split/multiple packs, offsets, missing AUD, SPS/PPS references, truncation,
   oversize, queue pressure, restart, and IDR recovery. Use uv-managed tooling.
2. Define the live-input protocol/profile and preserve simulation/rejection tests.
3. Add an opt-in Luckfox source with checked setup/teardown and finite waits;
   cross-build against this exact SDK. Keep UART/transport adapters separate.
4. When the board arrives, run the vendor smoke test, then capture raw encoder
   output and validate the adapter against it before integrated camera/UART/radio
   bench tests. Agree on the bench settings and pass/fail thresholds separately.

Only the review and vendor-example cross-build in this document are complete.
The adapter, protocol changes, board checks, and hardware integration remain
pending.
