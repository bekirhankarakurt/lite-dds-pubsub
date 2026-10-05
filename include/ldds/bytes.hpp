#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ldds {

using Bytes = std::vector<std::uint8_t>;

/// Thrown when a buffer is truncated or structurally invalid.
class DecodeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Little-endian append-only encoder.
class ByteWriter {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }

    void f32(float v) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }

    void f64(double v) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u64(bits);
    }

    /// Length-prefixed (u16) string.
    void str(std::string_view s) {
        if (s.size() > 0xFFFFU) throw std::length_error("string longer than 65535 bytes");
        u16(static_cast<std::uint16_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void raw(const Bytes& b) { buf_.insert(buf_.end(), b.begin(), b.end()); }

    [[nodiscard]] const Bytes& data() const noexcept { return buf_; }
    [[nodiscard]] Bytes take() noexcept { return std::move(buf_); }

private:
    void put(std::uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU));
    }

    Bytes buf_;
};

/// Bounds-checked little-endian decoder over a borrowed buffer.
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size) noexcept : p_(data), size_(size) {}
    explicit ByteReader(const Bytes& b) noexcept : ByteReader(b.data(), b.size()) {}

    std::uint8_t u8() { return static_cast<std::uint8_t>(get(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(get(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() { return get(8); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

    float f32() {
        const std::uint32_t bits = u32();
        float v = 0;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }

    double f64() {
        const std::uint64_t bits = u64();
        double v = 0;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }

    std::string str() {
        const std::size_t n = u16();
        need(n);
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }

    Bytes raw(std::size_t n) {
        need(n);
        Bytes b(p_ + pos_, p_ + pos_ + n);
        pos_ += n;
        return b;
    }

    void skip(std::size_t n) {
        need(n);
        pos_ += n;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - pos_; }

private:
    void need(std::size_t n) const {
        if (n > size_ - pos_) throw DecodeError("buffer truncated");
    }

    std::uint64_t get(std::size_t n) {
        need(n);
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(p_[pos_ + i]) << (8U * i);
        pos_ += n;
        return v;
    }

    const std::uint8_t* p_;
    std::size_t size_;
    std::size_t pos_{0};
};

}  // namespace ldds
