#include <thread>

#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

TEST(samples_flow_between_participants) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    std::vector<Pose> got;
    std::vector<SampleInfo> infos;
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose", Qos::best_effort(),
                                          [&](const Pose& p, const SampleInfo& i) {
                                              got.push_back(p);
                                              infos.push_back(i);
                                          });
    net.run(milliseconds{100}, {a.get(), b.get()});
    for (std::int32_t i = 1; i <= 3; ++i) {
        CHECK_EQ(pub.write(pose(i)), static_cast<std::uint64_t>(i));
        net.run(milliseconds{10}, {a.get(), b.get()});
    }
    REQUIRE_EQ(got.size(), 3U);
    CHECK_EQ(got[2].label, "p3");
    CHECK_EQ(got[1].x, 1.0);
    CHECK_EQ(infos[0].participant_id, 1U);
    CHECK_EQ(infos[2].seq, 3U);
    CHECK_EQ(sub.stats().received, 3U);
    CHECK_EQ(pub.stats().published, 3U);
}

TEST(no_network_traffic_without_remote_readers) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto pub = a->create_publisher<Pose>("pose");
    net.run(milliseconds{50}, {a.get()});
    const auto before = net.bus()->datagrams_sent();
    pub.write(pose(1));
    CHECK_EQ(net.bus()->datagrams_sent(), before);
}

TEST(loopback_delivers_synchronously_inside_one_participant) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = a->create_subscriber<Pose>("pose");
    CHECK_EQ(pub.matched_subscribers(), 1U);
    net.run(milliseconds{50}, {a.get()});
    const auto before = net.bus()->datagrams_sent();
    pub.write(pose(5));
    CHECK_EQ(net.bus()->datagrams_sent(), before);
    const auto sample = sub.take();
    REQUIRE(sample.has_value());
    CHECK_EQ(sample->value.id, 5);
    CHECK(!sub.take().has_value());
}

TEST(queue_keeps_only_the_newest_history_depth_samples) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = a->create_subscriber<Pose>("pose", Qos::best_effort(3));
    for (std::int32_t i = 1; i <= 5; ++i) pub.write(pose(i));
    const auto samples = sub.take_all();
    REQUIRE_EQ(samples.size(), 3U);
    CHECK_EQ(samples[0].value.id, 3);
    CHECK_EQ(samples[2].value.id, 5);
    CHECK(sub.take_all().empty());
}

TEST(best_effort_counts_gaps_and_ignores_stale_samples) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto sub = a->create_subscriber<Pose>("pose", Qos::best_effort(8));
    auto injector = net.bus()->connect();
    const auto send = [&](std::uint64_t seq) {
        Data d{1, seq, 0, false, "pose", "test/Pose", serialize(pose(static_cast<std::int32_t>(seq)))};
        CHECK(injector->send(encode(Packet{99, d})));
    };
    send(1);
    send(4);  // 2 and 3 never arrive
    send(2);  // stale
    net.run(milliseconds{10}, {a.get()});
    CHECK_EQ(sub.stats().received, 2U);
    CHECK_EQ(sub.stats().lost, 2U);
    CHECK_EQ(sub.stats().duplicates, 1U);
}

TEST(undecodable_payloads_are_counted) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto sub = a->create_subscriber<Pose>("pose");
    const std::uint32_t raw = a->add_writer("pose", "test/Pose", Qos::best_effort());
    a->write(raw, Bytes{1});
    CHECK_EQ(sub.decode_errors(), 1U);
    CHECK(!sub.take().has_value());
}

TEST(endpoints_unregister_on_destruction) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    auto pub = a->create_publisher<Pose>("pose");
    {
        auto sub = b->create_subscriber<Pose>("pose");
        net.run(milliseconds{50}, {a.get(), b.get()});
        CHECK_EQ(pub.matched_subscribers(), 1U);
    }
    net.run(milliseconds{50}, {a.get(), b.get()});
    CHECK_EQ(pub.matched_subscribers(), 0U);
}

TEST(handlers_may_publish_without_deadlock) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto echo = a->create_publisher<Other>("echo");
    auto sink = a->create_subscriber<Other>("echo");
    auto in = a->create_subscriber<Pose>("pose", Qos::best_effort(),
                                         [&echo](const Pose& p, const SampleInfo&) { echo.write(Other{p.id}); });
    auto pub = a->create_publisher<Pose>("pose");
    pub.write(pose(9));
    const auto s = sink.take();
    REQUIRE(s.has_value());
    CHECK_EQ(s->value.id, 9);
}

TEST(invalid_arguments_are_rejected) {
    TestNet net;
    auto a = net.make("alpha", 1);
    CHECK_THROWS(a->create_publisher<Pose>(""), std::invalid_argument);
    CHECK_THROWS(a->create_publisher<Pose>(std::string(300, 't')), std::invalid_argument);
    Qos zero_depth;
    zero_depth.history_depth = 0;
    CHECK_THROWS(a->create_subscriber<Pose>("pose", zero_depth), std::invalid_argument);
    CHECK_THROWS(a->write(999, Bytes{}), std::invalid_argument);
    const std::uint32_t w = a->add_writer("big", "T", Qos::best_effort());
    CHECK_THROWS(a->write(w, Bytes(kMaxDatagram, 0)), std::length_error);
}

TEST(moved_endpoints_stay_registered_once) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = a->create_subscriber<Pose>("pose");
    Publisher<Pose> moved = std::move(pub);
    pub = a->create_publisher<Pose>("pose");
    moved.write(pose(1));
    pub.write(pose(2));
    CHECK_EQ(sub.take_all().size(), 2U);
}

TEST(background_thread_delivers_samples) {
    auto bus = MemoryBus::create();
    Config ca;
    ca.name = "alpha";
    Config cb;
    cb.name = "beta";
    Participant a(bus->connect(), ca);
    Participant b(bus->connect(), cb);
    auto pub = a.create_publisher<Pose>("pose");
    auto sub = b.create_subscriber<Pose>("pose");
    a.start();
    b.start();
    for (int i = 0; i < 200 && pub.matched_subscribers() == 0; ++i) std::this_thread::sleep_for(milliseconds{10});
    REQUIRE_EQ(pub.matched_subscribers(), 1U);
    pub.write(pose(1));
    std::optional<Sample<Pose>> got;
    for (int i = 0; i < 200 && !got; ++i) {
        got = sub.take();
        if (!got) std::this_thread::sleep_for(milliseconds{10});
    }
    a.stop();
    b.stop();
    REQUIRE(got.has_value());
    CHECK_EQ(got->value.id, 1);
}
