#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

namespace {

/// Drops DATA datagrams for which `drop(seq, is_retransmission_candidate)` is true.
template <class Pred>
void drop_data(TestNet& net, Pred pred) {
    net.bus()->set_drop_filter([pred](const Bytes& datagram) mutable {
        const auto packet = decode(datagram);
        if (!packet) return false;
        const auto* d = std::get_if<Data>(&packet->body);
        return d != nullptr && pred(d->seq);
    });
}

struct Rig {
    explicit Rig(Qos writer_qos, Qos reader_qos) : a(net.make("alpha", 1)), b(net.make("beta", 2)) {
        pub = a->create_publisher<Pose>("pose", writer_qos);
        sub = b->create_subscriber<Pose>("pose", reader_qos, [this](const Pose& p, const SampleInfo&) {
            ids.push_back(p.id);
        });
        b->set_event_handler([this](const Event& e) {
            if (e.kind == EventKind::SamplesLost) lost_events += e.count;
        });
        net.run(milliseconds{100}, {a.get(), b.get()});
    }

    void publish(std::int32_t first, std::int32_t last) {
        for (std::int32_t i = first; i <= last; ++i) {
            pub.write(pose(i));
            net.run(milliseconds{10}, {a.get(), b.get()});
        }
    }

    TestNet net;
    std::unique_ptr<Participant> a;
    std::unique_ptr<Participant> b;
    Publisher<Pose> pub;
    Subscriber<Pose> sub;
    std::vector<std::int32_t> ids;
    std::uint64_t lost_events{0};
};

}  // namespace

TEST(reliable_reader_recovers_a_dropped_sample_in_order) {
    Rig rig(Qos::reliable(), Qos::reliable());
    std::uint32_t seen = 0;
    drop_data(rig.net, [&seen](std::uint64_t seq) { return seq == 3 && seen++ == 0; });
    rig.publish(1, 5);
    rig.net.run(milliseconds{200}, {rig.a.get(), rig.b.get()});
    const std::vector<std::int32_t> expected{1, 2, 3, 4, 5};
    CHECK(rig.ids == expected);
    CHECK(rig.sub.stats().nacks_sent >= 1U);
    CHECK_EQ(rig.pub.stats().retransmitted, 1U);
    CHECK_EQ(rig.sub.stats().lost, 0U);
}

TEST(best_effort_reader_loses_the_same_sample) {
    Rig rig(Qos::best_effort(), Qos::best_effort());
    drop_data(rig.net, [](std::uint64_t seq) { return seq == 3; });
    rig.publish(1, 5);
    rig.net.run(milliseconds{200}, {rig.a.get(), rig.b.get()});
    const std::vector<std::int32_t> expected{1, 2, 4, 5};
    CHECK(rig.ids == expected);
    CHECK_EQ(rig.sub.stats().lost, 1U);
    CHECK_EQ(rig.lost_events, 1U);
    CHECK_EQ(rig.pub.stats().retransmitted, 0U);
}

TEST(heartbeat_recovers_a_lost_tail_sample) {
    Rig rig(Qos::reliable(), Qos::reliable());
    std::uint32_t seen = 0;
    drop_data(rig.net, [&seen](std::uint64_t seq) { return seq == 5 && seen++ == 0; });
    rig.publish(1, 5);
    CHECK_EQ(rig.ids.size(), 4U);
    rig.net.run(milliseconds{300}, {rig.a.get(), rig.b.get()});
    CHECK_EQ(rig.ids.size(), 5U);
    CHECK_EQ(rig.ids.back(), 5);
}

TEST(burst_loss_is_recovered_without_reordering) {
    Rig rig(Qos::reliable(32), Qos::reliable(32));
    std::uint32_t drops = 0;
    drop_data(rig.net, [&drops](std::uint64_t seq) { return seq % 3 == 0 && drops++ < 4; });
    for (std::int32_t i = 1; i <= 12; ++i) rig.pub.write(pose(i));
    rig.net.run(milliseconds{500}, {rig.a.get(), rig.b.get()});
    std::vector<std::int32_t> expected;
    for (std::int32_t i = 1; i <= 12; ++i) expected.push_back(i);
    CHECK(rig.ids == expected);
}

TEST(unrecoverable_gap_is_reported_and_skipped) {
    Rig rig(Qos::reliable(2), Qos::reliable());
    drop_data(rig.net, [](std::uint64_t seq) { return seq == 2 || seq == 3; });
    for (std::int32_t i = 1; i <= 5; ++i) rig.pub.write(pose(i));
    rig.net.run(milliseconds{500}, {rig.a.get(), rig.b.get()});
    const std::vector<std::int32_t> expected{1, 4, 5};
    CHECK(rig.ids == expected);
    CHECK_EQ(rig.sub.stats().lost, 2U);
    CHECK_EQ(rig.lost_events, 2U);
}

TEST(duplicate_samples_are_ignored) {
    TestNet net;
    auto a = net.make("alpha", 1);
    std::vector<std::int32_t> ids;
    auto sub = a->create_subscriber<Pose>("pose", Qos::reliable(),
                                          [&ids](const Pose& p, const SampleInfo&) { ids.push_back(p.id); });
    auto injector = net.bus()->connect();
    for (const std::uint64_t seq : {1ULL, 1ULL, 2ULL, 2ULL}) {
        Data d{1, seq, 0, true, "pose", "test/Pose", serialize(pose(static_cast<std::int32_t>(seq)))};
        CHECK(injector->send(encode(Packet{99, d})));
    }
    net.run(milliseconds{20}, {a.get()});
    CHECK_EQ(ids.size(), 2U);
    CHECK_EQ(sub.stats().duplicates, 2U);
}

TEST(late_joiner_starts_at_the_first_sample_it_sees) {
    Rig rig(Qos::reliable(), Qos::reliable());
    rig.publish(1, 3);
    auto c = rig.net.make("gamma", 3);
    std::vector<std::int32_t> late;
    auto late_sub = c->create_subscriber<Pose>("pose", Qos::reliable(),
                                               [&late](const Pose& p, const SampleInfo&) { late.push_back(p.id); });
    rig.net.run(milliseconds{300}, {rig.a.get(), rig.b.get(), c.get()});
    CHECK(late.empty());  // volatile durability: no history replay
    rig.publish(4, 5);
    rig.net.run(milliseconds{100}, {rig.a.get(), rig.b.get(), c.get()});
    const std::vector<std::int32_t> expected{4, 5};
    CHECK(late == expected);
}
