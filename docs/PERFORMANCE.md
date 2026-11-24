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
