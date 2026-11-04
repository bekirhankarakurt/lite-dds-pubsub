# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-04

### Added
- Participant discovery over UDP multicast with periodic announce, lease expiry and graceful BYE.
- Typed topics through the `Serializer<T>` trait, with built-ins for arithmetic types, strings and vectors.
- Best-effort and reliable QoS: NACK-based retransmit, in-order delivery, heartbeats, history depth.
- Reader deadline and peer liveliness events, plus per-endpoint statistics.
- Local loopback shortcut that bypasses the network for endpoints in the same participant.
- In-memory bus, manual clock, and record/replay transports for deterministic simulation.
- `sensor_publisher`, `logger_subscriber`, `dds_spy`, `lossy_link_demo`, `hello_pubsub` and `ldds_bench`.
- Unit tests under CTest, and CI with GCC, Clang, ASan/UBSan, clang-tidy, cppcheck, CodeQL and coverage.

[Unreleased]: https://github.com/bekirhankarakurt/lite-dds-pubsub/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bekirhankarakurt/lite-dds-pubsub/releases/tag/v1.0.0
