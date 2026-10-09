#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

TEST(peers_discover_each_other_and_match_endpoints) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    std::vector<Event> events;
    a->set_event_handler([&events](const Event& e) { events.push_back(e); });
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose");
    net.run(milliseconds{100}, {a.get(), b.get()});

    const auto peers = a->remote_participants();
    REQUIRE_EQ(peers.size(), 1U);
    CHECK_EQ(peers[0].name, "beta");
    CHECK_EQ(peers[0].id, 2U);
    CHECK_EQ(peers[0].endpoints.size(), 1U);
    CHECK_EQ(pub.matched_subscribers(), 1U);
    CHECK_EQ(sub.matched_publishers(), 1U);
    REQUIRE_EQ(events.size(), 1U);
    CHECK(events[0].kind == EventKind::ParticipantDiscovered);
    CHECK_EQ(events[0].remote_name, "beta");
}

TEST(topic_or_type_mismatch_does_not_match) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    auto pub = a->create_publisher<Pose>("pose");
    auto wrong_topic = b->create_subscriber<Pose>("other");
    auto wrong_type = b->create_subscriber<Other>("pose");
    net.run(milliseconds{100}, {a.get(), b.get()});
    CHECK_EQ(pub.matched_subscribers(), 0U);
    CHECK_EQ(wrong_topic.matched_publishers(), 0U);
    CHECK_EQ(wrong_type.matched_publishers(), 0U);
}

TEST(reliable_reader_does_not_match_best_effort_writer) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    auto pub = a->create_publisher<Pose>("pose", Qos::best_effort());
    auto sub = b->create_subscriber<Pose>("pose", Qos::reliable());
    net.run(milliseconds{100}, {a.get(), b.get()});
    CHECK_EQ(pub.matched_subscribers(), 0U);
    CHECK_EQ(sub.matched_publishers(), 0U);
}

TEST(silent_peer_is_lost_after_lease_expiry) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    std::vector<Event> events;
    a->set_event_handler([&events](const Event& e) { events.push_back(e); });
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose");
    net.run(milliseconds{100}, {a.get(), b.get()});
    REQUIRE_EQ(pub.matched_subscribers(), 1U);

    net.run(milliseconds{3000}, {a.get()});
    CHECK_EQ(a->remote_participants().size(), 1U);  // still within the 3.5 s lease
    net.run(milliseconds{1000}, {a.get()});
    CHECK_EQ(a->remote_participants().size(), 0U);
    CHECK_EQ(pub.matched_subscribers(), 0U);
    REQUIRE_EQ(events.size(), 2U);
    CHECK(events[1].kind == EventKind::ParticipantLost);
    CHECK_EQ(events[1].remote_name, "beta");
}

TEST(heartbeat_traffic_keeps_the_lease_alive) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    net.run(milliseconds{10000}, {a.get(), b.get()});
    CHECK_EQ(a->remote_participants().size(), 1U);
    CHECK_EQ(b->remote_participants().size(), 1U);
}

TEST(graceful_shutdown_is_reported_immediately) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    int lost = 0;
    a->set_event_handler([&lost](const Event& e) { lost += e.kind == EventKind::ParticipantLost ? 1 : 0; });
    net.run(milliseconds{100}, {a.get(), b.get()});
    b.reset();
    net.run(milliseconds{5}, {a.get()});
    CHECK_EQ(lost, 1);
    CHECK_EQ(a->remote_participants().size(), 0U);
}

TEST(own_multicast_echo_is_ignored) {
    TestNet net;
    auto a = net.make("alpha", 1);
    net.run(milliseconds{50}, {a.get()});
    CHECK_EQ(a->remote_participants().size(), 0U);
}

TEST(malformed_datagrams_are_counted_not_fatal) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto injector = net.bus()->connect();
    CHECK(injector->send(Bytes{1, 2, 3}));
    net.run(milliseconds{10}, {a.get()});
    CHECK_EQ(a->stats().malformed, 1U);
}
