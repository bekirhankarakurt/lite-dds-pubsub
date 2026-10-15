#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "ldds/bytes.hpp"

namespace ldds {

/// Serialization trait. Specialize for your message type:
///
///   template <> struct ldds::Serializer<Pose> {
///       static std::string type_name() { return "Pose"; }
///       static void write(ByteWriter& w, const Pose& v);
///       static Pose read(ByteReader& r);   // throws DecodeError on bad input
///   };
///
/// `type_name()` takes part in topic matching: endpoints only match when
/// both topic and type name are equal.
template <class T, class Enable = void>
struct Serializer;

/// Arithmetic types (fixed width, little-endian on the wire).
template <class T>
struct Serializer<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    static_assert(!std::is_same_v<T, long double>, "long double has no portable wire format");
    static_assert(sizeof(T) <= 8, "unsupported arithmetic width");

    static std::string type_name() {
        if constexpr (std::is_same_v<T, bool>) {
            return "bool";
        } else {
            const char* prefix = "u";
            if constexpr (std::is_floating_point_v<T>) {
                prefix = "f";
            } else if constexpr (std::is_signed_v<T>) {
                prefix = "i";
            }
            return prefix + std::to_string(sizeof(T) * 8);
        }
    }

    static void write(ByteWriter& w, T v) {
        if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                w.f32(static_cast<float>(v));
            } else {
                w.f64(static_cast<double>(v));
            }
        } else if constexpr (sizeof(T) == 1) {
            w.u8(static_cast<std::uint8_t>(v));
        } else if constexpr (sizeof(T) == 2) {
            w.u16(static_cast<std::uint16_t>(v));
        } else if constexpr (sizeof(T) == 4) {
            w.u32(static_cast<std::uint32_t>(v));
        } else {
            w.u64(static_cast<std::uint64_t>(v));
        }
    }

    static T read(ByteReader& r) {
        if constexpr (std::is_floating_point_v<T>) {
            if constexpr (sizeof(T) == 4) {
                return static_cast<T>(r.f32());
            } else {
                return static_cast<T>(r.f64());
            }
        } else if constexpr (sizeof(T) == 1) {
            return static_cast<T>(r.u8());
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(r.u16());
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(r.u32());
        } else {
            return static_cast<T>(r.u64());
        }
    }
};

template <>
struct Serializer<std::string> {
    static std::string type_name() { return "string"; }
    static void write(ByteWriter& w, const std::string& v) { w.str(v); }
    static std::string read(ByteReader& r) { return r.str(); }
};

/// Sequences: u32 element count followed by the elements.
template <class T>
struct Serializer<std::vector<T>> {
    static std::string type_name() { return "seq<" + Serializer<T>::type_name() + ">"; }

    static void write(ByteWriter& w, const std::vector<T>& v) {
        w.u32(static_cast<std::uint32_t>(v.size()));
        for (const auto& e : v) Serializer<T>::write(w, e);
    }

    static std::vector<T> read(ByteReader& r) {
        const std::uint32_t n = r.u32();
        if (n > r.remaining()) throw DecodeError("sequence length exceeds payload");
        std::vector<T> v;
        v.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(Serializer<T>::read(r));
        return v;
    }
};

template <class T>
[[nodiscard]] Bytes serialize(const T& value) {
    ByteWriter w;
    Serializer<T>::write(w, value);
    return w.take();
}

template <class T>
[[nodiscard]] T deserialize(const Bytes& bytes) {
    ByteReader r(bytes);
    return Serializer<T>::read(r);
}

}  // namespace ldds
