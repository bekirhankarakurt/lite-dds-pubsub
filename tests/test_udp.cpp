#include <thread>

#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

namespace {

UdpConfig test_config(std::uint16_t port) {
    UdpConfig cfg;
    cfg.port = port;
    return cfg;
}

}  // namespace

TEST(invalid_addresses_are_rejected) {
    UdpConfig cfg = test_config(17450);
    cfg.group = "not-an-address";
    CHECK_THROWS(make_udp_transport(cfg), std::runtime_error);
}

TEST(multicast_datagrams_reach_every_socket) {
    std::unique_ptr<Transport> tx;
    std::unique_ptr<Transport> rx;
    try {
        tx = make_udp_transport(test_config(17451));
        rx = make_udp_transport(test_config(17451));
    } catch (const std::runtime_error& e) {
        SKIP(e.what());
    }
    const Bytes payload{9, 8, 7};
    REQUIRE(tx->send(payload));
    const auto got = rx->receive(milliseconds{1000});
    REQUIRE(got.has_value());
    CHECK(*got == payload);
}

TEST(participants_exchange_samples_over_udp) {
    std::unique_ptr<Participant> a;
    std::unique_ptr<Participant> b;
    try {
        Config ca;
        ca.name = "alpha";
        Config cb;
        cb.name = "beta";
        a = std::make_unique<Participant>(make_udp_transport(test_config(17452)), ca);
        b = std::make_unique<Participant>(make_udp_transport(test_config(17452)), cb);
    } catch (const std::runtime_error& e) {
        SKIP(e.what());
    }
    auto pub = a->create_publisher<Pose>("pose", Qos::reliable());
    auto sub = b->create_subscriber<Pose>("pose", Qos::reliable());
    for (int i = 0; i < 100 && pub.matched_subscribers() == 0; ++i) {
        a->spin_once(milliseconds{10});
        b->spin_once(milliseconds{10});
    }
    REQUIRE_EQ(pub.matched_subscribers(), 1U);
    pub.write(pose(1));
    pub.write(pose(2));
    std::vector<Sample<Pose>> got;
    for (int i = 0; i < 100 && got.size() < 2; ++i) {
        a->spin_once(milliseconds{5});
        b->spin_once(milliseconds{5});
        for (auto& s : sub.take_all()) got.push_back(std::move(s));
    }
    REQUIRE_EQ(got.size(), 2U);
    CHECK_EQ(got[1].value.label, "p2");
}
