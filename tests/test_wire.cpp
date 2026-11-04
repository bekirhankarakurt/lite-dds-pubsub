#include "fixture.hpp"

using namespace ldds;

namespace {

Packet announce_packet() {
    Announce a;
    a.name = "node";
    a.lease_ms = 3500;
    a.endpoints.push_back(EndpointInfo{EndpointKind::Writer, 1, "imu", "T", Qos::reliable(8)});
    a.endpoints.push_back(
        EndpointInfo{EndpointKind::Reader, 2, "cmd", "U", Qos::best_effort(4).with_deadline(std::chrono::milliseconds{250})});
    return Packet{0x1122334455667788ULL, a};
}

}  // namespace

TEST(announce_round_trip) {
    const auto decoded = decode(encode(announce_packet()));
    REQUIRE(decoded.has_value());
    CHECK_EQ(decoded->participant_id, 0x1122334455667788ULL);
    const auto* a = std::get_if<Announce>(&decoded->body);
    REQUIRE(a != nullptr);
    CHECK_EQ(a->name, "node");
    CHECK_EQ(a->lease_ms, 3500U);
    REQUIRE_EQ(a->endpoints.size(), 2U);
    CHECK(a->endpoints[0].kind == EndpointKind::Writer);
    CHECK(a->endpoints[0].qos.reliability == Reliability::Reliable);
    CHECK_EQ(a->endpoints[0].qos.history_depth, 8);
    CHECK(a->endpoints[1].kind == EndpointKind::Reader);
    CHECK_EQ(a->endpoints[1].qos.deadline.count(), 250);
    CHECK_EQ(a->endpoints[1].topic, "cmd");
}

TEST(data_round_trip_keeps_reliability_flag) {
    Data d{9, 42, -5, true, "topic", "type", Bytes{1, 2, 3}};
    const auto decoded = decode(encode(Packet{7, d}));
    REQUIRE(decoded.has_value());
    const auto* out = std::get_if<Data>(&decoded->body);
    REQUIRE(out != nullptr);
    CHECK_EQ(out->writer_id, 9U);
    CHECK_EQ(out->seq, 42U);
    CHECK_EQ(out->timestamp_ns, -5);
    CHECK(out->reliable);
    CHECK(out->payload == d.payload);
    d.reliable = false;
    const auto best_effort = decode(encode(Packet{7, d}));
    REQUIRE(best_effort.has_value());
    CHECK(!std::get<Data>(best_effort->body).reliable);
}

TEST(heartbeat_nack_and_bye_round_trip) {
    const auto hb = decode(encode(Packet{1, Heartbeat{3, 10, 20, "t", "y"}}));
    REQUIRE(hb.has_value());
    CHECK_EQ(std::get<Heartbeat>(hb->body).last_seq, 20U);

    const auto nack = decode(encode(Packet{2, Nack{1, 3, {4, 5, 9}}}));
    REQUIRE(nack.has_value());
    CHECK_EQ(std::get<Nack>(nack->body).seqs.size(), 3U);
    CHECK_EQ(std::get<Nack>(nack->body).target, 1U);

    const auto bye = decode(encode(Packet{3, Bye{}}));
    REQUIRE(bye.has_value());
    CHECK(std::holds_alternative<Bye>(bye->body));
}

TEST(garbage_and_bad_headers_are_rejected) {
    CHECK(!decode(Bytes{}).has_value());
    CHECK(!decode(Bytes(64, 0xFF)).has_value());
    Bytes wrong_version = encode(Packet{1, Bye{}});
    wrong_version[4] = 99;
    CHECK(!decode(wrong_version).has_value());
    Bytes wrong_type = encode(Packet{1, Bye{}});
    wrong_type[5] = 77;
    CHECK(!decode(wrong_type).has_value());
}

TEST(every_truncation_is_handled) {
    const Bytes full = encode(announce_packet());
    for (std::size_t n = 0; n < full.size(); ++n) {
        const Bytes prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
        CHECK(!decode(prefix).has_value());
    }
    CHECK(decode(full).has_value());
}

TEST(forged_counts_cannot_force_allocation) {
    Bytes forged = encode(Packet{1, Nack{1, 1, {}}});
    forged[forged.size() - 2] = 0xFF;  // claim 255 sequence numbers with none present
    CHECK(!decode(forged).has_value());
}

TEST(oversized_datagram_is_refused) {
    Data d;
    d.payload.assign(kMaxDatagram, 0);
    CHECK_THROWS(encode(Packet{1, d}), std::length_error);
}

TEST(describe_summarizes_packets) {
    const std::string text = describe(announce_packet());
    CHECK(text.find("ANNOUNCE 55667788") == 0);
    CHECK(text.find("W#1 imu [T] reliable depth=8") != std::string::npos);
    CHECK(text.find("deadline=250ms") != std::string::npos);
    CHECK(describe(Packet{0xAB, Nack{0xCD, 2, {5, 6}}}) == "NACK 000000ab -> 000000cd w#2 missing=[5,6]");
}
