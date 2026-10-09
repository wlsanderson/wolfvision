#!/usr/bin/env -S uv run --locked
"""Check localhost video/telemetry; use --suite extended for additional fault/stress tests."""

import argparse
import csv
import hashlib
import json
import pathlib
import re
import selectors
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from itertools import pairwise


def check(ok, message):
    if not ok:
        raise RuntimeError(message)


def ready(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        check(selector.select(10), "startup timeout")
        line = process.stdout.readline()
    check(line.startswith("READY"), "startup failed: " + line)


def port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def envelope(
    session,
    sequence,
    body=b"",
    *,
    kind=2,
    timestamp=None,
    unit=0,
    fragment=0,
    fragments=1,
    total=None,
):
    """Independent big-endian test fixture, not the application's serializer."""
    if timestamp is None:
        timestamp = time.monotonic_ns() // 1000
    if total is None:
        total = len(body)
    return (
        struct.pack(
            "!4sBBHQQQIHHIHH",
            b"WVSM",
            1,
            kind,
            1,
            session,
            sequence,
            timestamp,
            unit,
            fragment,
            fragments,
            total,
            len(body),
            0,
        )
        + body
    )


def check_limits(summary):
    check(
        summary["peak_buffer_bytes"] <= 4 * 1024 * 1024
        and summary["peak_buffer_units"] <= 64
        and summary["sequence_entries"] <= 4096,
        "memory limits exceeded",
    )


def receiver_guards(build, output):
    """Invalid/duplicate traffic must not keep a missing-end capture alive."""
    rx_port = port()
    prefix = output / "guards"
    rx = subprocess.Popen(
        [
            str(build / "simulation_rx"),
            "--port",
            str(rx_port),
            "--output-prefix",
            str(prefix),
            "--timeout-ms",
            "500",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready(rx)
        timestamp = time.monotonic_ns() // 1000
        payload = struct.pack("!4sBBHQQIi", b"WVTM", 1, 1, 32, 0, timestamp, 12000, -1000)
        valid = envelope(9000, 0, payload, timestamp=timestamp)
        bad_version = payload[:4] + b"\x02" + payload[5:]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(valid, ("127.0.0.1", rx_port))
            sock.sendto(envelope(9000, 1, bad_version, timestamp=timestamp), ("127.0.0.1", rx_port))
            sock.sendto(envelope(9000, 2, payload, timestamp=timestamp + 1), ("127.0.0.1", rx_port))
            sock.sendto(valid[:-1], ("127.0.0.1", rx_port))
            sock.sendto(valid + b"\x00", ("127.0.0.1", rx_port))
            # Preserve integration coverage from the retired telemetry demo:
            # distinct gap/late/duplicate handling and signed CSV measurements.
            for sequence in (3, 3, 1, 4):
                body = struct.pack(
                    "!4sBBHQQIi",
                    b"WVTM",
                    1,
                    1,
                    32,
                    sequence,
                    timestamp,
                    12000 + sequence,
                    -250 if sequence == 1 else 25000,
                )
                sock.sendto(
                    envelope(9000, sequence, body, timestamp=timestamp), ("127.0.0.1", rx_port)
                )
            start = time.monotonic()
            while rx.poll() is None and time.monotonic() - start < 1.5:
                sock.sendto(valid, ("127.0.0.1", rx_port))
                sock.sendto(b"invalid", ("127.0.0.1", rx_port))
                time.sleep(0.02)
        out, err = rx.communicate(timeout=3)
        check(rx.returncode == 2, "missing-end capture must time out despite invalid traffic")
        summary = json.loads(pathlib.Path(str(prefix) + "-9000.json").read_text())
        check(
            summary["reason"] == "timeout"
            and summary["telemetry_rows"] == 4
            and summary["duplicates"] > 1
            and summary["rejected_packets"] >= 5
            and summary["sequence_entries"] == 4
            and summary["gap_observations"] == 2
            and summary["out_of_order"] == 1,
            "rejection or sequence classification changed capture state",
        )
        with open(str(prefix) + "-9000.csv") as stream:
            rows = list(csv.DictReader(stream))
        check(
            [row["sequence_status"] for row in rows] == ["first", "gap", "out_of_order", "in_order"]
            and [int(row["sequence"]) for row in rows] == [0, 3, 1, 4]
            and [int(row["supply_mv"]) for row in rows] == [12000, 12003, 12001, 12004]
            and [int(row["temperature_mc"]) for row in rows] == [-1000, 25000, -250, 25000],
            "CSV values/statuses differ or duplicate rows were written",
        )
        check(
            "timestamp mismatch" in err and "unsupported protocol version" in err,
            "missing rejection diagnostic",
        )
        (output / "guards-rx.log").write_text(out + err)
        print(
            "guards: malformed packets, signed CSV values, sequence events, and missing-end timeout passed",
            flush=True,
        )
    finally:
        if rx.poll() is None:
            rx.kill()
            rx.communicate(timeout=5)


def preflight(parser, options):
    """Fail before opening sockets or creating captures when tools are missing."""
    build = pathlib.Path(options.build).resolve()
    for app in ("simulation_tx", "simulation_rx"):
        if not shutil.which(str(build / app)):
            parser.error(
                f"Cannot find executable {build / app}. Build first with "
                "cmake -S . -B build && cmake --build build -j"
            )
    if options.guards_only:
        return build
    for name in ("ffmpeg", "ffprobe"):
        requested = getattr(options, name)
        executable = shutil.which(requested)
        if not executable:
            parser.error(
                f"Cannot find {name} executable {requested!r}. In Ubuntu/WSL run "
                "sudo apt update && sudo apt install ffmpeg, or pass "
                "--ffmpeg /path/to/ffmpeg --ffprobe /path/to/ffprobe. "
                "FFmpeg/FFprobe are system tools; uv sync does not install them."
            )
        setattr(options, name, str(pathlib.Path(executable).resolve()))
    try:
        subprocess.run(
            [options.ffprobe, "-version"], capture_output=True, text=True, check=True, timeout=10
        )
        encoders = subprocess.run(
            [options.ffmpeg, "-hide_banner", "-encoders"],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError) as error:
        parser.error(f"Cannot run the configured FFmpeg/FFprobe tools: {error}")
    if not any(
        len(fields := line.split()) > 1 and fields[1] == "libx264"
        for line in encoders.stdout.splitlines()
    ):
        parser.error(
            "FFmpeg needs the libx264 encoder for this test. In Ubuntu/WSL "
            "install the ffmpeg package or pass --ffmpeg with a build that includes libx264."
        )
    return build


def overload(build, output, recovery_unit):
    """Force the 4 MiB fragment budget, then recover a real IDR at capture close."""
    rx_port = port()
    prefix = output / "overload"
    rx = subprocess.Popen(
        [
            str(build / "simulation_rx"),
            "--port",
            str(rx_port),
            "--output-prefix",
            str(prefix),
            "--reorder-ms",
            "5000",
            "--timeout-ms",
            "1000",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready(rx)
        sequence = 0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:

            def send(body, **fields):
                nonlocal sequence
                sock.sendto(
                    envelope(9001, sequence, body, kind=1, **fields), ("127.0.0.1", rx_port)
                )
                sequence += 1
                # Pace injection to test application eviction, rather than kernel UDP overflow.
                time.sleep(0.0005)

            size = 1024 * 1024
            fragments = (size + 1099) // 1100
            for unit in range(5):
                for fragment in range(
                    fragments - 1
                ):  # Omit each final fragment so nothing can drain.
                    send(b"x" * 1100, unit=unit, fragment=fragment, fragments=fragments, total=size)
            count = (len(recovery_unit) + 1099) // 1100
            for fragment in range(count):
                send(
                    recovery_unit[fragment * 1100 : (fragment + 1) * 1100],
                    unit=5,
                    fragment=fragment,
                    fragments=count,
                    total=len(recovery_unit),
                )
        out, err = rx.communicate(timeout=5)
        check(rx.returncode == 2, "overload capture without an end marker must time out")
        base = str(prefix) + "-9001"
        summary = json.loads(pathlib.Path(base + ".json").read_text())
        check_limits(summary)
        check(
            summary["received_packets"] == sequence and summary["rejected_packets"] == 0,
            "overload injection lost or rejected packets",
        )
        check(
            summary["peak_buffer_bytes"] > 4 * 1024 * 1024 - 1100
            and summary["video_units_missing"] == 5
            and summary["video_units_written"] == 1
            and pathlib.Path(base + ".h264").read_bytes() == recovery_unit,
            "fragment-budget eviction or IDR recovery did not occur",
        )
        (output / "overload-rx.log").write_text(out + err)
        return summary
    finally:
        if rx.poll() is None:
            rx.kill()
            rx.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", nargs="?", default="build")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument(
        "--suite",
        choices=("standard", "extended"),
        default="standard",
        help="standard: core checks; extended: also stress, outages, and restarts",
    )
    parser.add_argument("--sustained-loops", type=int, default=40)
    parser.add_argument(
        "--guards-only",
        action="store_true",
        help="run UDP rejection/timeout checks without media tools",
    )
    o = parser.parse_args()
    if o.sustained_loops < 2:
        parser.error("--sustained-loops must be at least 2")
    build = preflight(parser, o)
    output = pathlib.Path(tempfile.mkdtemp(prefix="simulation-", dir=build))
    receiver_guards(build, output)
    if o.guards_only:
        print("Artifacts:", output)
        return
    fixture = output / "input.h264"
    subprocess.run(
        [
            o.ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-f",
            "lavfi",
            "-i",
            "testsrc2=size=160x120:rate=20",
            "-frames:v",
            "60",
            "-an",
            "-c:v",
            "libx264",
            "-preset",
            "ultrafast",
            "-tune",
            "zerolatency",
            "-pix_fmt",
            "yuv420p",
            "-bf",
            "0",
            "-x264-params",
            "aud=1:repeat-headers=1:keyint=10:min-keyint=10:scenecut=0",
            "-f",
            "h264",
            str(fixture),
        ],
        check=True,
        timeout=30,
    )
    input_bytes = fixture.read_bytes()
    results = {}

    def decoded_frames(path):
        subprocess.run(
            [
                o.ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-xerror",
                "-f",
                "h264",
                "-i",
                str(path),
                "-f",
                "null",
                "-",
            ],
            check=True,
            timeout=30,
        )
        probe = subprocess.run(
            [
                o.ffprobe,
                "-v",
                "error",
                "-count_frames",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=nb_read_frames",
                "-of",
                "json",
                str(path),
            ],
            capture_output=True,
            text=True,
            check=True,
            timeout=30,
        )
        return int(json.loads(probe.stdout)["streams"][0]["nb_read_frames"])

    def scenario(name, impairment=None, fps=20, loops=1, kill_first=False, restart_receiver=False):
        rx_port = port()
        prefix = output / name
        rx = subprocess.Popen(
            [
                str(build / "simulation_rx"),
                "--port",
                str(rx_port),
                "--output-prefix",
                str(prefix),
                "--sessions",
                "2" if kill_first else "1",
                "--reorder-ms",
                "150",
                "--timeout-ms",
                "3000",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        processes = [rx]
        proxy = None
        rss = []
        stop = threading.Event()

        def sample_rss():
            while not stop.wait(0.05):
                try:
                    status = pathlib.Path(f"/proc/{rx.pid}/status").read_text()
                    rss.append(
                        int(
                            next(
                                line.split()[1]
                                for line in status.splitlines()
                                if line.startswith("VmRSS:")
                            )
                        )
                    )
                except (OSError, StopIteration):
                    return

        monitor = threading.Thread(target=sample_rss, daemon=True)
        monitor.start()
        try:
            ready(rx)
            tx_port = rx_port
            if impairment:
                tx_port = port()
                proxy = subprocess.Popen(
                    [
                        sys.executable,
                        str(pathlib.Path(__file__).with_name("udp_impair.py")),
                        "--listen-port",
                        str(tx_port),
                        "--destination-port",
                        str(rx_port),
                        "--idle-ms",
                        "500",
                        "--seed",
                        "123",
                        "--stats",
                        str(output / (name + "-proxy.json")),
                        *impairment,
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                processes.append(proxy)
                ready(proxy)
            session = len(results) + 100
            tx_args = [
                str(build / "simulation_tx"),
                "--input",
                str(fixture),
                "--port",
                str(tx_port),
                "--fps",
                str(fps),
                "--loops",
                str(loops),
                "--session",
                str(session),
            ]
            if kill_first:
                first = subprocess.Popen(
                    tx_args[:-3] + ["100", "--session", str(session)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                processes.append(first)
                time.sleep(0.35)
                first.kill()
                first.communicate(timeout=5)
                session += 1000
                tx_args[-1] = str(session)
            if restart_receiver:
                tx_process = subprocess.Popen(
                    tx_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                )
                processes.append(tx_process)
                time.sleep(0.35)
                rx.kill()
                rx.communicate(timeout=5)
                rx = subprocess.Popen(
                    [
                        str(build / "simulation_rx"),
                        "--port",
                        str(rx_port),
                        "--output-prefix",
                        str(prefix) + "-resumed",
                        "--reorder-ms",
                        "150",
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                processes.append(rx)
                ready(rx)
                prefix = pathlib.Path(str(prefix) + "-resumed")
                tx_out, tx_err = tx_process.communicate(timeout=15)
                tx = subprocess.CompletedProcess(tx_args, tx_process.returncode, tx_out, tx_err)
            else:
                tx = subprocess.run(
                    tx_args, capture_output=True, text=True, timeout=max(15, 60 * loops / fps + 10)
                )
            check(tx.returncode == 0, tx.stderr)
            out, err = rx.communicate(timeout=10)
            check(rx.returncode == (2 if kill_first else 0), "receiver failed: " + out + err)
            (output / (name + "-rx.log")).write_text(out + err)
            (output / (name + "-tx.log")).write_text(tx.stdout + tx.stderr)
            if proxy:
                proxy_out, proxy_err = proxy.communicate(timeout=10)
                check(proxy.returncode == 0, proxy_err)
                (output / (name + "-proxy.log")).write_text(proxy_out + proxy_err)
            base = str(prefix) + "-" + str(session)
            summary = json.loads(pathlib.Path(base + ".json").read_text())
            with open(base + ".csv") as stream:
                rows = list(csv.DictReader(stream))
            check(rows, "no telemetry recorded")
            for row in rows:
                sample = int(row["sequence"])
                check(
                    int(row["supply_mv"]) == 12000 + sample % 101
                    and int(row["temperature_mc"]) == 25000 + sample % 201 * 10,
                    "wrong UART-derived values",
                )
            ordered = sorted(rows, key=lambda row: int(row["sequence"]))
            samples = [int(row["sequence"]) for row in ordered]
            stamps = [int(row["monotonic_us"]) for row in ordered]
            check(len(samples) == len(set(samples)), "duplicate telemetry CSV samples")
            check(all(a < b for a, b in pairwise(stamps)), "nonmonotonic measurement timestamps")
            check_limits(summary)
            frames = decoded_frames(pathlib.Path(base + ".h264"))
            if name in ("clean", "reorder", "sustained", "restart"):
                check(
                    pathlib.Path(base + ".h264").read_bytes() == input_bytes * loops,
                    "video bytes differ",
                )
                check(frames == 60 * loops, "decoded frame count differs")
            else:
                check(
                    0 < frames < 60 * loops,
                    "fault test must lose frames and produce decodable video",
                )
            if name == "reorder":
                check(
                    summary["duplicates"] > 2 and summary["out_of_order"] > 0,
                    "reorder/duplicate events absent",
                )
            if name == "loss":
                check(
                    frames == 55
                    and summary["video_units_missing"] == 1
                    and summary["video_units_skipped_until_idr"] == 4,
                    "IDR recovery accounting differs",
                )
            if kill_first:
                earlier = json.loads(
                    pathlib.Path(str(prefix) + "-" + str(session - 1000) + ".json").read_text()
                )
                check(earlier["reason"] == "restart", "restart not identified")
            if name == "sustained" and rss:
                # Whole-process RSS may grow during initial allocation; compare settled thirds.
                third = max(1, len(rss) // 3)
                growth = max(rss[-third:]) - min(rss[third : 2 * third])
                check(growth <= 8192, f"settled RSS grew {growth} KiB")
            summary["decoded_frames"] = frames
            summary["peak_rss_kib"] = max(rss) if rss else None
            results[name] = summary
            print(
                f"{name}: frames={frames}, telemetry={len(rows)}, missing={summary['video_units_missing']}, "
                f"peak_buffer={summary['peak_buffer_bytes']} bytes",
                flush=True,
            )
        finally:
            stop.set()
            monitor.join(timeout=1)
            for process in reversed(processes):
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=5)

    scenario("clean")
    scenario("reorder", ["--delay-ms", "10", "--jitter-ms", "40", "--duplicate", "0.3"])
    scenario("loss", ["--drop-video-unit", "5"])
    if o.suite == "extended":
        scenario("random-loss", ["--loss", "0.01"])
        scenario("burst", ["--burst-every", "150", "--burst-length", "3"])
        scenario("outage", ["--outage-start-ms", "250", "--outage-duration-ms", "200"])
        scenario("restart", kill_first=True)
        scenario("receiver-restart", ["--delay-ms", "0"], restart_receiver=True)
        scenario("sustained", fps=240, loops=o.sustained_loops)
        # The first generated access unit is a genuine IDR with SPS/PPS.
        auds = list(re.finditer(b"\x00\x00(?:\x00)?\x01\x09", input_bytes))
        check(len(auds) == 60 and auds[0].start() == 0, "unexpected generated fixture boundaries")
        summary = overload(build, output, input_bytes[: auds[1].start()])
        frames = decoded_frames(output / "overload-9001.h264")
        check(frames == 1, "overload recovery IDR must decode")
        summary["decoded_frames"] = frames
        results["overload"] = summary
        print(
            f"overload: recovered IDR, peak_buffer={summary['peak_buffer_bytes']} bytes", flush=True
        )
    (output / "results.json").write_text(json.dumps(results, indent=2))
    print(f"PASS: {o.suite} suite ({len(results)} video scenarios plus receiver guards).")
    print("Artifacts:", output)
    print("Fixture SHA256:", hashlib.sha256(input_bytes).hexdigest())


if __name__ == "__main__":
    main()
