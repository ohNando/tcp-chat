# Performance

Numbers come from `Backend/tools/bench.sh`, which starts a fresh server for
each scenario and drives it with `bin/bench` (see `tools/bench.c`). The load
generator and the server run on the same machine, so treat the results as
relative: they are for comparing changes, not for absolute capacity.

Every message is broadcast to every member, so the server delivers
`clients × messages` copies, each encrypted separately for its recipient.

**Machine:** Intel Core i7-1360P (16 threads), Linux 7.3, GCC 16.2, loopback.

## Scenarios

| Name | Load |
|---|---|
| light | 50 clients × 10 msg/s, 64 B, 10 s → 500 msg/s in, 25 000 deliveries/s |
| heavy | 200 clients × 5 msg/s, 64 B, 10 s → 1 000 msg/s in, 200 000 deliveries/s |
| slow readers | 50 clients × 20 msg/s, 2 KB, plus 20 clients that never read |

Latency is the round trip from a client sending a message to the same
client receiving the server's broadcast of it.

## Baseline

Commit `feat: Add a load generator and benchmark scenarios for the server.`
(server built with `-O0 -g`).

| Scenario | Server CPU | Latency p50 / p99 / max (ms) | Server RSS (peak) |
|---|---|---|---|
| light | 11 % | 34.3 / 34.8 / 41.4 | 2.9 MiB |
| heavy | 24 % | 17.3 / 17.5 / 41.7 | 4.1 MiB |
| slow readers | 17 % | 16.3 / 17.3 / 40.8 | 3.2 MiB (22.4 MiB) |

All messages were delivered and every echo arrived in all scenarios.

Observations:

- The latency is suspiciously constant (p50 ≈ p99) and the maximum is always
  about 40 ms: the signature of Nagle's algorithm on the server's sockets
  interacting with delayed ACKs on the client.
- The server is built without optimization.
- Slow readers push peak memory to ~22 MiB: each of the 20 keeps up to
  1 MiB of backlog before it is dropped.

## After the first round of optimizations

| Scenario | Server CPU (user / kernel) | Latency p50 / p99 / max (ms) | Server RSS (peak) |
|---|---|---|---|
| light | 18 % (4 / 13) | 0.46 / 0.96 / 1.41 | 2.9 MiB |
| heavy | 37 % (2 / 36) | 0.47 / 1.48 / 2.70 | 4.1 MiB |
| slow readers | 21 % (18 / 4) | 0.32 / 0.45 / 0.87 | 3.1 MiB (23.1 MiB) |

What changed and what each step did:

| Change | Effect |
|---|---|
| `TCP_NODELAY` on server sockets | **p50 latency 34 ms → 0.5 ms**, max 41 ms → under 3 ms. Confirms the Nagle + delayed-ACK suspicion. CPU went up slightly because every message now leaves as its own segment instead of being held back. |
| `TCP_NODELAY` in the clients | Same fix for the client → server direction (browsers already do this for WebSockets). |
| `-O2` instead of `-O0` | No measurable change: see the CPU split below. Kept as a sensible default. |
| Output buffer compaction only when needed | No change in these scenarios (slow readers hit the 1 MiB cap and are dropped within seconds). In a micro-benchmark that keeps a ~900 KB backlog and queues 20 000 small records, **304.6 ms → 1.0 ms**: the old code copied the whole backlog on every record. |
| Input buffers grow from 4 KiB on demand | RSS barely moves, because Linux never backs untouched `malloc` pages with memory anyway. The gain is in address space and in returning memory after large records. |

### Where the time goes

`bench` reads the server's user and kernel CPU time separately. In the
heavy scenario about **95 % of the server's CPU is spent in the kernel**:
every message is written to every recipient with its own `send()`, which is
200 000 system calls per second. Our own code plus all the encryption
accounts for only 2–4 %.

That is why encoding a broadcast once instead of per recipient was dropped
from the plan: it would optimize the 2 %. The next real step would be
fewer system calls (batching writes per loop turn, or `io_uring`).

In the slow-readers scenario the user time is higher (18 %) because the
messages are 2 KB: that is XChaCha20-Poly1305 encrypting about 100 MB/s.
