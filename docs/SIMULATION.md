# Simulation guide

This host milestone uses prerecorded H.264 and synthetic UART data over IPv4
UDP. It prepares shared software for hardware v0.1; it does not emulate camera
APIs, hardware encoding, UART wiring, radio drivers, or RF propagation.
Follow [development setup](DEVELOPMENT.md#setup) and the
[README build commands](../README.md#build-and-test) first.

## Create a video input

The input must be raw **Annex B H.264**, with an access unit delimiter (AUD,
NAL type 9) beginning each access unit. The first unit and every IDR must include
SPS and PPS. Use no B frames; each access unit must fit within 1 MiB. MP4
containers and streams without AUDs must be converted first.

Generate a real encoded, three-second test input:

```sh
ffmpeg -f lavfi -i testsrc2=size=160x120:rate=20 -t 3 -an \
  -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -bf 0 \
  -x264-params aud=1:repeat-headers=1:keyint=10:min-keyint=10:scenecut=0 \
  -f h264 build/input.h264
```

For a real video file, replace `-f lavfi -i testsrc2=...` with
`-i your-video.mp4`, retaining the encoder settings. The test pattern contains
genuine encoded video; Wolfvision transports and reconstructs its bytes.

## Run TX and RX

Start RX in one terminal and TX in another, from the repository root:

```sh
./build/simulation_rx --address 127.0.0.1 --port 5100 --output-prefix build/capture
```

```sh
./build/simulation_tx --input build/input.h264 --address 127.0.0.1 --port 5100 \
  --fps 20 --telemetry-rate 10 --loops 1 --session 100
```

The finite replay creates `build/capture-100.h264`, `build/capture-100.csv`, and
`build/capture-100.json`. Choose a fresh prefix/session for another run;
directories must exist and existing output files are refused.

Play or independently validate the recording:

```sh
ffplay -f h264 -framerate 20 build/capture-100.h264
ffmpeg -v error -xerror -i build/capture-100.h264 -f null -
```

Elementary streams do not store packet timestamps or TX pacing. Specify a
playback rate as appropriate. Lost frames shorten the recording; placeholders
and wall-clock gaps are not preserved. An integrated live viewer is not provided.

## Options and completion

Both applications accept `--help`, numeric IPv4 `--address` (default
`127.0.0.1`), and `--port` (default `5100`). RX binds the address; TX sends to it.

| TX option | Default | Meaning |
| --- | --- | --- |
| `--input PATH` | Required | AUD-delimited raw H.264 |
| `--fps N` | 20 | Access unit pacing, 1–240 per second |
| `--telemetry-rate N` | 10 | Simulated UART measurements, 1–1000 Hz |
| `--loops N` | 1 | Positive finite number of file passes |
| `--session N` | Random nonzero 64-bit ID | Fresh ID for each TX start |

| RX option | Default | Meaning |
| --- | --- | --- |
| `--output-prefix PATH` | `capture` | Output prefix with `-SESSION` suffix |
| `--sessions N` | 1 | Number of completed/interrupted sessions before exit |
| `--timeout-ms N` | 3000 | Valid-new-packet inactivity or wait for a new session |
| `--reorder-ms N` | 150 | Incomplete-unit / gap waiting deadline |

TX sends an end marker after replay. RX waits for delayed fragments and closes
the files. Exit 0 means normal completion, including intentionally lossy
captures; exit 1 means CLI/I/O failure; RX exit 2 means interruption/timeout or
missing requested sessions. Details are in [receiver behavior](PROTOCOL.md).

To test TX restart manually, run RX with `--sessions 2`, interrupt TX during
replay, then start a replacement TX with a fresh session ID. RX closes the
interrupted session as `restart` and creates new files/state. This assumes one
sender; it is not multi-sender arbitration.

## Telemetry and outputs

`H264FileSource` streams one access unit at a time. `SimulatedUartSource`
generates deterministic measurements and feeds a bounded streaming parser with
synthetic ASCII lines such as:

```text
SIM,12000,25000\n
```

The final `\n` represents one actual newline byte. Fields are decimal millivolts
and milli-degrees Celsius. The parser handles partial chunks, multiple frames,
invalid numbers, and overlong lines (maximum 80 bytes excluding newline), then
resynchronizes at a newline. No serial port is opened; this framing is a fixture,
not the chosen future device protocol. Supply is `12000 + sample % 101` mV;
temperature is `25000 + (sample % 201) * 10` milli-degrees Celsius.

The CSV records session, measurement sequence, monotonic microseconds, supply
millivolts, temperature milli-degrees Celsius, and envelope arrival status.
Duplicates do not create extra rows. JSON contains close reason, received and
rejected packets, separate sequence events, missing/skipped/invalid/written video
units, telemetry rows, throughput, and peak buffering.

`gap_observations` counts forward sequence skips, not permanent packet loss;
late arrivals can fill gaps. `max_local_packet_delay_us` measures same-computer
packet delay, not camera-to-display latency. Rejections before a session begins
are printed but do not belong to its summary. The
[protocol guide](PROTOCOL.md) defines formats, validation, bounds, and recovery.

## Inject link faults

Start RX, the proxy, and TX in three terminals, in this order:

```sh
./build/simulation_rx --port 5100 --output-prefix build/impaired
```

```sh
uv run --locked tools/udp_impair.py --listen-port 5101 --destination-port 5100 \
  --seed 123 --loss 0.01 --duplicate 0.2 --delay-ms 10 --jitter-ms 40 \
  --stats build/proxy.json
```

```sh
./build/simulation_tx --input build/input.h264 --port 5101 --fps 20 --session 101
```

Jitter changes delivery order. `--burst-every N --burst-length M` drops the first
M datagrams in each N-datagram block. `--outage-start-ms N --outage-duration-ms M`
defines a timed outage relative to the first datagram. `--drop-video-unit N`
drops all fragments of one unit. `--queue-limit N` caps the queue (default 2048);
overflow is counted and dropped. The proxy drains delayed packets, then exits
after `--idle-ms` inactivity (default 2000).

Seeded decisions repeat for the same arrival order; scheduling can change timed
outage boundaries. For automated checks, use the standard or extended suite
listed in [development testing](DEVELOPMENT.md#testing).

## Current limitations

Pacing is best effort and does not infer FPS from input. Synchronous disk writes
and OS socket buffers can lose data under overload; there is no adaptive bitrate
or backpressure. Loss recovery skips dependent pictures until a complete IDR;
there is no retransmission or FEC. Session switching trusts valid new IDs, so
use one sender on localhost. Cross-device timestamps need a separate strategy.

Live camera input, on-board H.264 encoding, real UART, Luckfox build/execution,
and the E103-RTL8812CU link remain unverified. These host tests do not establish
radio throughput/range, power, temperature, antenna behavior, flight reliability,
or hardware v0.1 completion. See [project context](PROJECT_CONTEXT.md).
