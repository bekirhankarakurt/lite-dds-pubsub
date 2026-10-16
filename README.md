# lite-dds-pubsub

Minimal DDS-style publish/subscribe middleware in C++17 over UDP multicast: discovery, typed topics, and QoS (reliable delivery, history, deadline, liveliness) with no dependencies.

[![CI](https://github.com/bekirhankarakurt/lite-dds-pubsub/actions/workflows/ci.yml/badge.svg)](https://github.com/bekirhankarakurt/lite-dds-pubsub/actions/workflows/ci.yml)
[![CodeQL](https://github.com/bekirhankarakurt/lite-dds-pubsub/actions/workflows/codeql.yml/badge.svg)](https://github.com/bekirhankarakurt/lite-dds-pubsub/actions/workflows/codeql.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://bekirhankarakurt.github.io/lite-dds-pubsub/coverage-badge.json)](https://bekirhankarakurt.github.io/lite-dds-pubsub/coverage/)
[![Release](https://img.shields.io/github/v/release/bekirhankarakurt/lite-dds-pubsub)](https://github.com/bekirhankarakurt/lite-dds-pubsub/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](#requirements)

![Demo: logger and sensor publisher, then reliable vs best-effort on a lossy link](docs/demo.gif)

[Documentation](https://bekirhankarakurt.github.io/lite-dds-pubsub/) (API reference and coverage report) | [Changelog](CHANGELOG.md) | [Releases](https://github.com/bekirhankarakurt/lite-dds-pubsub/releases)

## Why / what

- **Decoupled nodes.** Publishers and subscribers only share a topic name and a type; they find each other automatically (announce, heartbeat, lease), the way DDS-based avionics and C2 software does.
- **Two delivery modes.** Best-effort for high-rate telemetry; reliable with NACK-based retransmit and in-order delivery for commands and logs.
- **Failure is observable.** Lost samples, missed deadlines and vanished peers are reported as events and counters instead of disappearing silently.
- **Runs without hardware or a network.** An in-memory bus and a record/replay transport let everything be tested deterministically with a virtual clock.
- **Small and readable.** About 2k lines of library code, RAII throughout, no raw `new`/`delete`, builds warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

## Demo

Terminal 1 starts a logger; terminal 2 starts a simulated IMU (real output of both):

```text
$ ./build/bin/sensor_publisher --count 5 --rate 10
participant 94401642 "sensor" publishing sensors/imu (reliable, 10 Hz)
subscribers matched: 1
published 5 samples, retransmitted 0

$ ./build/bin/logger_subscriber --count 5
participant fa6aeddb "logger" listening on sensors/imu (reliable)
+ participant "sensor" (94401642)
seq=   1 t=0.100 accel=(0.169, 0.190, 9.854) gyro=(0.016, 0.048, 0.012) from 94401642
seq=   2 t=0.200 accel=(0.296, 0.162, 9.777) gyro=(0.031, 0.044, 0.019) from 94401642
seq=   3 t=0.300 accel=(0.349, 0.118, 9.777) gyro=(0.045, 0.036, 0.019) from 94401642
seq=   4 t=0.400 accel=(0.317, 0.062, 9.854) gyro=(0.059, 0.027, 0.012) from 94401642
seq=   5 t=0.500 accel=(0.206, 0.000, 9.807) gyro=(0.071, 0.015, 0.000) from 94401642
received=5 lost=0 duplicates=0 deadline_missed=0 nacks_sent=0
```

Reliable vs best-effort on a link that drops 40% of datagrams (simulated, deterministic):

```text
$ ./build/bin/lossy_link_demo --loss 40
link loss 40%, 200 samples per topic, 272/680 datagrams dropped
best-effort: received 126/200  lost 70  nacks 0
reliable:    received 200/200  lost 0  nacks 82
reliable retransmissions: 150, out-of-order deliveries: 0
```

## Quick start (60 seconds)

```bash
git clone https://github.com/bekirhankarakurt/lite-dds-pubsub.git && cd lite-dds-pubsub
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

```bash
./build/bin/logger_subscriber            # terminal 1
./build/bin/sensor_publisher --count 20  # terminal 2
./build/bin/dds_spy --duration 5         # terminal 3: watch the wire
```

Minimal API (`apps/hello_pubsub.cpp`, runs in one process on the in-memory bus):

```cpp
struct Temperature { double celsius{0}; std::string sensor; };

template <> struct ldds::Serializer<Temperature> {
    static std::string type_name() { return "example/Temperature"; }
    static void write(ByteWriter& w, const Temperature& t) { w.f64(t.celsius); w.str(t.sensor); }
    static Temperature read(ByteReader& r) { Temperature t; t.celsius = r.f64(); t.sensor = r.str(); return t; }
};

auto bus = ldds::MemoryBus::create();            // or ldds::make_udp_transport({})
ldds::Participant a(bus->connect()), b(bus->connect());
auto pub = a.create_publisher<Temperature>("lab/temperature", ldds::Qos::reliable());
auto sub = b.create_subscriber<Temperature>("lab/temperature", ldds::Qos::reliable(),
    [](const Temperature& t, const ldds::SampleInfo& i) { /* ... */ });
pub.write({21.5, "bench-1"});                    // after discovery; b.spin_once(...) delivers it
```

```text
$ ./build/bin/hello_pubsub
bench-1 reports 21.5 C (seq 1)
bench-1 reports 21.7 C (seq 2)
```

## Requirements

| Item | Requirement |
|---|---|
| OS | Linux (POSIX sockets; other POSIX systems should work but are not tested in CI) |
| Compiler | GCC 9+ or Clang 10+ with C++17 |
| Build | CMake 3.16+ |
| Dependencies | None (only the C++ standard library and pthreads) |
| Network | A multicast-capable interface; the default configuration stays on loopback (`127.0.0.1`) |

### Build options

| CMake option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | Standard CMake build type |
| `LDDS_BUILD_TESTS` | `ON` | Build the unit tests (CTest) |
| `LDDS_BUILD_APPS` | `ON` | Build the examples, `dds_spy` and the lossy-link demo |
| `LDDS_BUILD_BENCH` | `ON` | Build `ldds_bench` |
| `LDDS_WERROR` | `OFF` | Treat warnings as errors (CI turns this on) |
| `LDDS_SANITIZE` | `OFF` | Build with AddressSanitizer + UndefinedBehaviorSanitizer |

Use the library from another CMake project with `add_subdirectory(lite-dds-pubsub)` and `target_link_libraries(app PRIVATE ldds::ldds)`.

## Usage

All binaries land in `build/bin/`. Every tool accepts `--help`; the network options below are shared.

| Option | Default | Meaning |
|---|---|---|
| `--group` | `239.255.42.99` | Multicast group |
| `--port` | `7447` | UDP port |
| `--iface` | `127.0.0.1` | Local interface address used for multicast (set to your LAN address to leave the host) |

| Tool | Purpose | Notable options |
|---|---|---|
| `sensor_publisher` | Publishes a simulated IMU | `--rate`, `--count`, `--best-effort`, `--depth`, `--topic`, `--name`, `--wait` |
| `logger_subscriber` | Prints and logs samples | `--count`, `--duration`, `--best-effort`, `--deadline <ms>`, `--csv`, `--record`, `--replay`, `--quiet` |
| `dds_spy` | Decodes all traffic on the group | `--filter all\|announce\|data\|heartbeat\|nack\|bye`, `--count`, `--duration`, `--record`, `--replay` |
| `lossy_link_demo` | Reliable vs best-effort over simulated loss | `--loss`, `--samples`, `--seed`, `--depth` |
| `ldds_bench` | Latency and throughput | `--path loopback\|mem\|udp`, `--size`, `--latency-samples`, `--throughput-samples`, `--best-effort` |
| `hello_pubsub` | Minimal API example | none |

### Recipes

1. **Watch the wire.** `./build/bin/dds_spy --filter data` prints one line per sample (`DATA 94401642 sensors/imu w#1 seq=3 bytes=36 reliable`). Samples are only sent while a matching reader exists, so start a subscriber first.
2. **Record a session, analyze it later.**
   ```bash
   ./build/bin/logger_subscriber --record session.ldds --count 100
   ./build/bin/dds_spy --replay session.ldds --filter nack
   ./build/bin/logger_subscriber --replay session.ldds --quiet --csv imu.csv
   ```
   Replay feeds the capture through a normal participant, so offline analysis exercises the same code as live runs.
3. **High-rate telemetry with a deadline.** Best-effort at 200 Hz; the logger reports a miss if the writer goes quiet for more than 100 ms:
   ```bash
   ./build/bin/logger_subscriber --best-effort --deadline 100 --quiet &
   ./build/bin/sensor_publisher --best-effort --rate 200 --count 1000
   ```
4. **Several isolated domains on one host.** Different group/port pairs never see each other:
   ```bash
   ./build/bin/logger_subscriber --group 239.255.1.1 --port 7500 &
   ./build/bin/sensor_publisher  --group 239.255.1.1 --port 7500 --count 10
   ```
5. **Across machines.** Pass each host's own LAN address: `--iface 192.168.1.10` on one, `--iface 192.168.1.11` on the other, same group and port. The default TTL of 1 keeps traffic on the local subnet.
6. **Benchmark.** `./build/bin/ldds_bench --path udp --size 256 --best-effort`

Reference numbers (Release, GCC 13, one Linux container, 64-byte samples, reliable QoS; yours will differ):

| Path | p50 latency | p99 latency | Throughput |
|---|---|---|---|
| `loopback` (same participant) | 0.2 µs | 0.3 µs | 2.1 M msg/s |
| `mem` (in-memory bus) | 16 µs | 44 µs | 62 k msg/s |
| `udp` (multicast on loopback) | 18 µs | 62 µs | 42 k msg/s |

Latency is measured with the steady clock and is only meaningful when publisher and subscriber share a host.

## Architecture

```text
  user code
 +-----------------------------+      +-------------------------------+
 | Publisher<T>  Subscriber<T> |      | dds_spy / tools               |
 |  Serializer<T> (type_name,  |      +---------------+---------------+
 |  write, read)               |                      | decode()
 +--------------+--------------+                      |
                | bytes + SampleInfo                  |
 +--------------v--------------------------------------v--+
 | Participant                                             |
 |  discovery  announce / lease / BYE   -> remote registry |
 |  writers    history ring, heartbeat, NACK service       |
 |  readers    per-writer seq state, reorder buffer,       |
 |             NACK scheduler, deadline monitor            |
 |  local loopback shortcut (no network round trip)        |
 +--------------------------+------------------------------+
                            | encoded datagrams (wire.hpp)
 +--------------------------v------------------------------+
 | Transport                                               |
 |  UdpTransport (multicast) | MemoryBus | Recording/Replay|
 +---------------------------------------------------------+
```

Design trade-offs:

| Decision | Why | Cost |
|---|---|---|
| Multicast for everything (data, NACKs, repairs) | One socket, no per-peer state, trivial discovery | Repairs reach every subscriber; no encryption or auth |
| Samples sent only while a matching remote reader is known | No traffic on idle topics, loopback shortcut is truly network-free | A reader joining mid-stream misses samples until discovered (a few ms) |
| Volatile durability: late joiners start at the first sample they see | Simple and predictable | No history replay for new subscribers |
| Retransmit window = writer `history_depth` | Bounded memory | Older gaps are skipped and reported as `SamplesLost` |
| Topic and type name carried in every DATA packet | Stateless receivers, safe type matching | About 60 bytes of overhead per sample |
| Injectable `Clock` and `Transport` | Deterministic tests of leases, deadlines, retransmit | One extra virtual call per packet |
| No fragmentation layer (max 64 KB datagram) | Keeps the wire format small | Keep samples under the path MTU (~1400 B) to avoid IP fragmentation on a LAN |

## Wire format

All integers are little-endian. Strings are a `u16` length plus UTF-8 bytes.

| Header field | Size | Value |
|---|---|---|
| magic | 4 | `LDDS` |
| version | 1 | `1` |
| type | 1 | 1 ANNOUNCE, 2 BYE, 3 DATA, 4 HEARTBEAT, 5 NACK |
| flags | 1 | bit 0: DATA is from a reliable writer |
| reserved | 1 | `0` |
| participant id | 8 | random per participant |

| Packet | Body |
|---|---|
| ANNOUNCE | name, `u32` lease ms, `u16` count, then per endpoint: `u8` kind (0 writer, 1 reader), `u32` id, topic, type, `u8` reliability, `u16` depth, `u32` deadline ms |
| BYE | empty |
| DATA | `u32` writer id, `u64` seq, `i64` timestamp ns, topic, type, `u32` length, payload |
| HEARTBEAT | `u32` writer id, `u64` first seq, `u64` last seq, topic, type |
| NACK | `u64` target participant, `u32` writer id, `u16` count, `u64` missing seq ... |

Capture files (`--record`): magic `LDDSCAP1`, then records of `i64` wall-clock ns, `u32` length, datagram bytes.

QoS matching: topic and type name must be equal, and a reliable reader does not match a best-effort writer.

## Testing

```bash
ctest --test-dir build --output-on-failure                       # 8 test programs
cmake -S . -B build-asan -DLDDS_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build-asan -j && ctest --test-dir build-asan
```

The harness is a 90-line header (`tests/check.hpp`). Tests cover the byte codec, every truncation of a packet, discovery and lease expiry, retransmit after burst and tail loss, unrecoverable gaps, duplicates, deadlines, record/replay and real UDP multicast. Protocol timing runs on a virtual clock and the in-memory bus, so the suite is fast and deterministic. The UDP tests skip (CTest status "skipped") when multicast sockets cannot be created. CI runs GCC, Clang, an ASan/UBSan job, clang-tidy (`.clang-tidy`), cppcheck and CodeQL; line coverage is published with the [documentation](https://bekirhankarakurt.github.io/lite-dds-pubsub/coverage/).

## Troubleshooting / FAQ

**Nothing is received between two machines.** The default `--iface 127.0.0.1` keeps traffic on the host. Pass the LAN address on both machines and check that switches/firewalls allow the group and UDP port.

**`udp transport: IP_ADD_MEMBERSHIP: No such device`.** The `--iface` address does not exist on this machine, or that interface has no multicast support.

**The publisher prints `subscribers matched: 0`.** No subscriber with the same topic *and* type name was discovered within `--wait` seconds. Reliable readers also never match best-effort writers; start the publisher without `--best-effort`.

**Why did `dds_spy` show only ANNOUNCE packets?** Data is only sent while a matching subscriber exists. Start a subscriber first.

**Reliable samples arrive late.** Repairs wait for the NACK interval (20 ms) and heartbeat period (100 ms); both are fields of `ldds::Config`.

**Is it compatible with real DDS/RTPS?** No. It borrows the concepts (participants, topics, QoS) but uses its own compact wire format.

**Is it thread-safe?** Publishing is thread-safe. Either call `spin_once()` from one thread or `start()` the background thread, not both. Handlers run on the spinning thread without internal locks held, so they may publish.

## Project layout

```text
include/ldds/
  bytes.hpp          little-endian ByteWriter/ByteReader, DecodeError
  serialization.hpp  Serializer<T> trait + built-ins (numbers, string, vector)
  qos.hpp            Reliability, history depth, deadline
  clock.hpp          Clock, SteadyClock, ManualClock (virtual time)
  wire.hpp           packet structs, encode/decode
  transport.hpp      Transport interface, UDP config, MemoryBus
  capture.hpp        capture files, RecordingTransport, ReplayTransport
  participant.hpp    Participant, Config, events, stats
  pubsub.hpp         Publisher<T>, Subscriber<T>
  describe.hpp       human-readable packet dump
  ldds.hpp           umbrella header
src/                 wire, describe, participant, UDP/memory transports, capture
apps/                hello_pubsub, sensor_publisher, logger_subscriber, dds_spy, lossy_link_demo
apps/common/         argument parser, demo message type, helpers
bench/               ldds_bench
tests/               check.hpp harness + 8 test programs
docs/                demo.tape (vhs script) and demo.gif
Doxyfile             API documentation config (published to GitHub Pages)
.clang-tidy          static analysis profile
.cppcheck-suppressions
CHANGELOG.md  CONTRIBUTING.md  SECURITY.md
.github/workflows/   ci (GCC, Clang, ASan/UBSan, static analysis), codeql, pages (docs + coverage), release
.github/             issue forms, PR template, dependabot (actions, weekly)
```

## Roadmap

- [x] Participant discovery with announce, lease and graceful BYE
- [x] Typed topics via `Serializer<T>`
- [x] Best-effort and reliable QoS with NACK repair and history depth
- [x] Deadline and liveliness events
- [x] Local loopback shortcut
- [x] Record/replay and in-memory simulation transport
- [x] Sensor publisher, logger subscriber, spy tool, benchmark
- [x] CI: sanitizers, clang-tidy, cppcheck, CodeQL, coverage, API docs, tagged releases
- [ ] Compact numeric topic ids to shrink per-sample overhead
- [ ] Unicast repairs to the requesting reader
- [ ] Durable (late-joiner) history for reliable topics
- [ ] IPv6 and Windows/macOS CI

## License

MIT, see [LICENSE](LICENSE).

Bekirhan Karakurt - github.com/bekirhankarakurt
