#include "fixture.hpp"

using namespace ldds;

TEST(primitives_round_trip) {
    ByteWriter w;
    w.u8(0xAB);
    w.u16(0xBEEF);
    w.u32(0xDEADBEEF);
    w.u64(0x0123456789ABCDEFULL);
    w.i64(-42);
    w.f32(1.5F);
    w.f64(-2.25);
    w.str("héllo");
    const Bytes bytes = w.take();
    ByteReader r(bytes);
    CHECK_EQ(r.u8(), 0xAB);
    CHECK_EQ(r.u16(), 0xBEEF);
    CHECK_EQ(r.u32(), 0xDEADBEEFU);
    CHECK_EQ(r.u64(), 0x0123456789ABCDEFULL);
    CHECK_EQ(r.i64(), -42);
    CHECK_EQ(r.f32(), 1.5F);
    CHECK_EQ(r.f64(), -2.25);
    CHECK_EQ(r.str(), "héllo");
    CHECK_EQ(r.remaining(), 0U);
}

TEST(wire_is_little_endian) {
    ByteWriter w;
    w.u32(0x01020304);
    const Bytes expected{0x04, 0x03, 0x02, 0x01};
    CHECK(w.data() == expected);
}

TEST(truncated_reads_throw) {
    const Bytes bytes{1, 2, 3};
    ByteReader r(bytes);
    CHECK_THROWS(r.u32(), DecodeError);
    ByteReader s(bytes);
    CHECK_THROWS(s.raw(4), DecodeError);
}

TEST(oversized_string_is_rejected) {
    ByteWriter w;
    CHECK_THROWS(w.str(std::string(70000, 'x')), std::length_error);
}

TEST(builtin_serializers_round_trip) {
    CHECK_EQ(deserialize<std::int16_t>(serialize<std::int16_t>(-1234)), -1234);
    CHECK_EQ(deserialize<std::uint64_t>(serialize<std::uint64_t>(1ULL << 63)), 1ULL << 63);
    CHECK_EQ(deserialize<double>(serialize(3.25)), 3.25);
    CHECK_EQ(deserialize<bool>(serialize(true)), true);
    CHECK_EQ(deserialize<std::string>(serialize(std::string("abc"))), "abc");
    const std::vector<std::string> words{"a", "bb", ""};
    CHECK(deserialize<std::vector<std::string>>(serialize(words)) == words);
}

TEST(type_names_compose) {
    CHECK_EQ(Serializer<std::uint32_t>::type_name(), "u32");
    CHECK_EQ(Serializer<double>::type_name(), "f64");
    CHECK_EQ(Serializer<bool>::type_name(), "bool");
    CHECK_EQ(Serializer<std::vector<std::int8_t>>::type_name(), "seq<i8>");
}

TEST(sequence_length_cannot_exceed_payload) {
    ByteWriter w;
    w.u32(0xFFFFFFFFU);
    CHECK_THROWS(deserialize<std::vector<std::uint8_t>>(w.data()), DecodeError);
}

TEST(user_defined_type_round_trips) {
    const Pose p = pose(7);
    const Pose q = deserialize<Pose>(serialize(p));
    CHECK_EQ(q.id, 7);
    CHECK_EQ(q.x, 3.5);
    CHECK_EQ(q.label, "p7");
}
