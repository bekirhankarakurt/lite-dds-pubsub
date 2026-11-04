#include "ldds/capture.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace ldds {
namespace {

constexpr char kMagic[8] = {'L', 'D', 'D', 'S', 'C', 'A', 'P', '1'};
constexpr std::size_t kRecordHeader = 12;
constexpr std::uint32_t kMaxCaptureRecord = 1U << 20;

}  // namespace

std::int64_t wall_clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

CaptureWriter::CaptureWriter(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) throw std::runtime_error("cannot open capture file for writing: " + path);
    out_.write(kMagic, sizeof kMagic);
    out_.flush();
}

void CaptureWriter::write(std::int64_t timestamp_ns, const Bytes& data) {
    ByteWriter w;
    w.i64(timestamp_ns);
    w.u32(static_cast<std::uint32_t>(data.size()));
    out_.write(reinterpret_cast<const char*>(w.data().data()), static_cast<std::streamsize>(w.data().size()));
    out_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    out_.flush();
}

CaptureReader::CaptureReader(const std::string& path) : in_(path, std::ios::binary) {
    if (!in_) throw std::runtime_error("cannot open capture file: " + path);
    char magic[sizeof kMagic] = {};
    in_.read(magic, sizeof magic);
    if (in_.gcount() != static_cast<std::streamsize>(sizeof magic) ||
        std::string(magic, sizeof magic) != std::string(kMagic, sizeof kMagic)) {
        throw DecodeError("not an LDDS capture file: " + path);
    }
}

std::optional<CapturedPacket> CaptureReader::next() {
    std::uint8_t header[kRecordHeader];
    in_.read(reinterpret_cast<char*>(header), static_cast<std::streamsize>(kRecordHeader));
    if (in_.gcount() == 0) return std::nullopt;
    if (in_.gcount() != static_cast<std::streamsize>(kRecordHeader)) throw DecodeError("truncated capture record");
    ByteReader r(header, kRecordHeader);
    CapturedPacket packet;
    packet.timestamp_ns = r.i64();
    const std::uint32_t length = r.u32();
    if (length > kMaxCaptureRecord) throw DecodeError("capture record too large");
    packet.data.resize(length);
    in_.read(reinterpret_cast<char*>(packet.data.data()), static_cast<std::streamsize>(length));
    if (in_.gcount() != static_cast<std::streamsize>(length)) throw DecodeError("truncated capture record");
    return packet;
}

RecordingTransport::RecordingTransport(std::unique_ptr<Transport> inner, const std::string& path)
    : inner_(std::move(inner)), writer_(path) {}

std::optional<Bytes> RecordingTransport::receive(std::chrono::milliseconds timeout) {
    std::optional<Bytes> datagram = inner_->receive(timeout);
    if (datagram) writer_.write(wall_clock_ns(), *datagram);
    return datagram;
}

ReplayTransport::ReplayTransport(const std::string& path) : reader_(path), next_(reader_.next()) {}

std::optional<Bytes> ReplayTransport::receive(std::chrono::milliseconds timeout) {
    if (!next_) {
        std::this_thread::sleep_for(timeout);
        return std::nullopt;
    }
    Bytes data = std::move(next_->data);
    next_ = reader_.next();
    return data;
}

}  // namespace ldds
