# Packet formats and receiver behavior

These are version 1 **simulation/bench formats**. Hardware transport and time
synchronization still need definition. Serialization uses explicit byte writes
and reads; raw C++ structs, native padding, and native byte order are never sent.
All multi-byte fields are big-endian (network byte order).

## WVSM video/telemetry envelope

One UDP datagram holds a 48-byte header and at most 1100 payload bytes, for a
maximum of 1148 bytes. Unless stated otherwise, fields are unsigned integers.

| Offset | Bytes | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 4 | Magic | ASCII `WVSM` |
| 4 | 1 | Version | Exactly `1` |
| 5 | 1 | Type | `1` video, `2` telemetry, `3` end |
| 6 | 2 | Flags | Exactly `1`: simulated-input profile; other bits zero |
| 8 | 8 | Session | Nonzero ID, fresh for each TX start |
| 16 | 8 | Sequence | Counts all packet types within the session |
| 24 | 8 | Timestamp | Monotonic microseconds at construction, not UTC |
| 32 | 4 | Unit ID | Video access unit index; telemetry zero; end total unit count |
| 36 | 2 | Fragment index | Zero-based within the video unit; control zero |
| 38 | 2 | Fragment count | Video `ceil(total_size / 1100)`; control `1` |
| 40 | 4 | Total unit size | Video bytes; telemetry `32`; end `0` |
| 44 | 2 | Payload length | Exact remaining datagram size in bytes |
| 46 | 2 | Reserved | Zero |
| 48 | Variable | Payload | Video fragment, nested WVTM packet, or empty |

Video unit IDs start at zero. Units are nonempty and at most 1 MiB; `UINT32_MAX`
is reserved for a possible final end count. Every fragment except the last is
exactly 1100 bytes; the last contains the remaining bytes. All slices and
headers of one encoded access unit are fragmented together. Sequence/unit
counters do not wrap; TX exits if exhausted.

Telemetry uses one 32-byte WVTM packet. Its measurement sequence counts samples
independently of the envelope sequence, and its timestamp must equal the envelope
timestamp. End packets contain no payload; their unit field supplies the total
video unit count so missing tail units can be counted. TX repeats the exact same
end datagram twice at 20 ms intervals, retaining its sequence number.

## WVTM nested measurements

The original telemetry codec is retained as the simulation telemetry payload.
The receiver accepts it inside WVSM, rather than as a standalone datagram.
Its exact size is 32 bytes.

| Offset | Bytes | Field | Meaning / units |
| --- | --- | --- | --- |
| 0 | 4 | Magic | ASCII `WVTM` |
| 4 | 1 | Version | Exactly `1` |
| 5 | 1 | Flags | Exactly `1`: simulated measurements; other bits zero |
| 6 | 2 | Packet length | Exactly `32` bytes |
| 8 | 8 | Sequence | Unsigned measurement sample number |
| 16 | 8 | Timestamp | Unsigned monotonic microseconds |
| 24 | 4 | Supply voltage | Unsigned millivolts |
| 28 | 4 | Temperature | Signed two's-complement milli-degrees Celsius |

For example, `12000` mV means 12 V, and `-1000` milli-degrees Celsius means
−1 °C. Integer fields avoid floating-point representation differences.

Both timestamps come from `std::chrono::steady_clock`. They are comparable in
this initial single-computer test; their epoch is not specified by the protocol.
Do not subtract timestamps from separate devices without a synchronization plan.
The receiver's local packet delay is not glass-to-glass video latency.

## Validation and sequence handling

RX rejects truncated/oversized packets, invalid magic/version/flags/reserved
bits, zero sessions, payload-length mismatches, unsupported types, invalid
fragment counts/indices/sizes, malformed nested telemetry, and timestamp mismatch.
Conflicting fragment duplicates or inconsistent unit metadata are rejected by
reassembly. End counts smaller than the observed video extent are rejected;
after an end marker, later units beyond its count and inconsistent end counts
are rejected. Output parameters are committed only after codec validation.

Sequence state is independent per capture. The first accepted sequence sets a
baseline; a higher number advances the watermark. These events are distinct:

| Arrival | Meaning |
| --- | --- |
| `first` | First accepted packet establishes the baseline |
| `in_order` | Exactly one greater than the highest observed number |
| `gap` | Higher number skips one or more sequences; skipped count is reported |
| `duplicate` | Already observed within the rolling window; no second CSV row or fragment delivery |
| `out_of_order` | Previously unseen number below the highest, within the window |
| `stale` | At least 4096 below the highest; no fragment delivery or CSV row |

The tracker retains at most 4096 numbers in `[highest - 4095, highest]` (clipped
at zero). Duplicate detection outside that window is intentionally unavailable.
Late and duplicate arrivals preserve the watermark. `gap_observations` counts
skipped numbers on forward jumps; later arrivals can fill gaps, so this value
is not permanent packet loss. CSV arrival status describes the envelope sequence.

Codec-rejected, duplicate, stale, and retired-session traffic cannot postpone the
valid-new-packet idle timeout. A validated new session closes the current one
as `restart` and starts fresh state/files. RX remembers 32 retired session IDs.
This policy assumes one sender; session IDs provide no authentication.

## Video recovery and memory limits

Input is constrained Annex B H.264: each access unit begins with an AUD, every
IDR includes SPS/PPS, and the initial encoder profile uses no B frames. C++
checks NAL structure and recovery points, not every H.264 bitstream detail.
Integration tests independently decode genuine encoded recordings with FFmpeg.

Only complete access units reach the recording. Missing/incomplete units expire
after `--reorder-ms` or when the window/payload budget is exceeded. Missing or
invalid units break the reference chain: dependent units are skipped until a
complete IDR with SPS/PPS arrives. Recovery resumes playback; it does not restore
lost video or telemetry.

Reassembly retains at most 64 unit IDs and 4 MiB of fragment payload, with at
most 1 MiB per unit. Metadata is bounded by those limits. Source, fragmentation,
and output assembly also use temporary unit storage. These limits exclude OS
socket/file buffers and are not a whole-process RSS cap. Disk output grows with
capture duration.

After an end marker RX waits `2 * reorder-ms` for late fragments, then flushes
and counts missing tail units. If all end copies are lost, valid-packet idle
timeout closes the capture instead. An interrupted/timed-out capture exits 2;
normal completion exits 0 even when packets were lost. CLI/I/O errors exit 1.

UDP has no delivery guarantee. This profile has no data retransmission, FEC,
application checksum, authentication, encryption, or adaptive bitrate.
