#!/usr/bin/env -S uv run --locked
"""Seeded, bounded UDP impairment proxy for the Wolfvision simulation."""

import argparse
import heapq
import json
import math
import random
import socket
import time


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--listen-address", default="127.0.0.1")
    p.add_argument("--listen-port", type=int, default=5101)
    p.add_argument("--destination-address", default="127.0.0.1")
    p.add_argument("--destination-port", type=int, default=5100)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--loss", type=float, default=0)
    p.add_argument("--duplicate", type=float, default=0)
    p.add_argument("--delay-ms", type=float, default=0)
    p.add_argument("--jitter-ms", type=float, default=0)
    p.add_argument("--burst-every", type=int, default=0)
    p.add_argument("--burst-length", type=int, default=0)
    p.add_argument("--outage-start-ms", type=float, default=0)
    p.add_argument("--outage-duration-ms", type=float, default=0)
    p.add_argument("--drop-video-unit", type=int, default=-1)
    p.add_argument("--idle-ms", type=float, default=2000)
    p.add_argument("--queue-limit", type=int, default=2048)
    p.add_argument("--stats", default="proxy.json")
    return p


def run(o):
    if not (0 <= o.loss <= 1 and 0 <= o.duplicate <= 1):
        raise ValueError("probabilities must be 0..1")
    for value in (o.delay_ms, o.jitter_ms, o.outage_start_ms, o.outage_duration_ms, o.idle_ms):
        if not math.isfinite(value) or value < 0 or value > 600000:
            raise ValueError("times must be finite and 0..600000 ms")
    if not (0 < o.idle_ms <= 600000 and 0 < o.queue_limit <= 65536):
        raise ValueError("idle-ms and queue-limit out of range")
    if not (0 <= o.burst_length <= o.burst_every):
        raise ValueError("burst length must be 0..burst-every")
    rng = random.Random(o.seed)
    stats = dict(received=0, forwarded=0, dropped=0, duplicated=0, queue_dropped=0, peak_queue=0)
    queue = []
    ticket = 0
    started = last = None
    destination = (o.destination_address, o.destination_port)
    with (
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as incoming,
        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as outgoing,
    ):
        incoming.bind((o.listen_address, o.listen_port))
        incoming.settimeout(0.01)
        print("READY proxy", flush=True)
        ready_at = time.monotonic()
        while True:
            now = time.monotonic()
            while queue and queue[0][0] <= now:
                _, _, data = heapq.heappop(queue)
                outgoing.sendto(data, destination)
                stats["forwarded"] += 1
            if now - (last if last is not None else ready_at) > o.idle_ms / 1000 and not queue:
                break
            try:
                data, _ = incoming.recvfrom(65536)
            except TimeoutError:
                continue
            now = time.monotonic()
            if started is None:
                started = now
            last = now
            stats["received"] += 1
            elapsed = (now - started) * 1000
            burst = o.burst_every and (stats["received"] - 1) % o.burst_every < o.burst_length
            outage = (
                o.outage_duration_ms
                and o.outage_start_ms <= elapsed < o.outage_start_ms + o.outage_duration_ms
            )
            video_drop = (
                len(data) >= 48
                and data[:6] == b"WVSM\x01\x01"
                and int.from_bytes(data[32:36], "big") == o.drop_video_unit
            )
            if burst or outage or video_drop or rng.random() < o.loss:
                stats["dropped"] += 1
                continue
            copies = 2 if rng.random() < o.duplicate else 1
            stats["duplicated"] += copies - 1
            for _ in range(copies):
                if len(queue) == o.queue_limit:
                    stats["queue_dropped"] += 1
                    continue
                delay = (o.delay_ms + rng.random() * o.jitter_ms) / 1000
                heapq.heappush(queue, (now + delay, ticket, data))
                ticket += 1
                stats["peak_queue"] = max(stats["peak_queue"], len(queue))
    with open(o.stats, "w") as stream:
        json.dump(stats, stream, indent=2)
    print(json.dumps(stats), flush=True)


if __name__ == "__main__":
    run(parser().parse_args())
