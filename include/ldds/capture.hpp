#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "ldds/bytes.hpp"
#include "ldds/transport.hpp"

namespace ldds {

struct CapturedPacket {
    std::int64_t timestamp_ns{0};  ///< wall-clock nanoseconds since the Unix epoch
    Bytes data;
};

/// Capture file: 8-byte magic "LDDSCAP1", then records of
/// { i64 timestamp_ns, u32 length, bytes } in little-endian order.
class CaptureWriter {
public:
    explicit CaptureWriter(const std::string& path);  ///< throws std::runtime_error
    void write(std::int64_t timestamp_ns, const Bytes& data);

private:
    std::ofstream out_;
};

class CaptureReader {
public:
    explicit CaptureReader(const std::string& path);  ///< throws std::runtime_error / DecodeError
    [[nodiscard]] std::optional<CapturedPacket> next();  ///< throws DecodeError on a truncated record

private:
    std::ifstream in_;
};

/// Current wall-clock time in nanoseconds since the Unix epoch.
[[nodiscard]] std::int64_t wall_clock_ns();

/// Decorator that appends every received datagram to a capture file.
class RecordingTransport final : public Transport {
public:
    RecordingTransport(std::unique_ptr<Transport> inner, const std::string& path);

    [[nodiscard]] bool send(const Bytes& datagram) override { return inner_->send(datagram); }
    [[nodiscard]] std::optional<Bytes> receive(std::chrono::milliseconds timeout) override;

private:
    std::unique_ptr<Transport> inner_;
    CaptureWriter writer_;
};

/// Feeds a capture file to a participant as if it were live traffic. Sending is a no-op.
class ReplayTransport final : public Transport {
public:
    explicit ReplayTransport(const std::string& path);

    [[nodiscard]] bool send(const Bytes&) override { return true; }
    [[nodiscard]] std::optional<Bytes> receive(std::chrono::milliseconds timeout) override;

    /// True once every recorded packet has been handed out.
    [[nodiscard]] bool exhausted() const noexcept { return !next_.has_value(); }

private:
    CaptureReader reader_;
    std::optional<CapturedPacket> next_;
};

}  // namespace ldds
