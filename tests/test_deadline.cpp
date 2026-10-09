#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

TEST(deadline_miss_is_reported_once_per_silence) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    int missed = 0;
    b->set_event_handler([&missed](const Event& e) {
        if (e.kind == EventKind::DeadlineMissed) {
            ++missed;
            CHECK_EQ(e.topic, "pose");
            CHECK_EQ(e.remote_name, "alpha");
        }
    });
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose", Qos::best_effort().with_deadline(milliseconds{100}));
    net.run(milliseconds{100}, {a.get(), b.get()});

    pub.write(pose(1));
    net.run(milliseconds{80}, {a.get(), b.get()});
    CHECK_EQ(missed, 0);
    net.run(milliseconds{60}, {a.get(), b.get()});
    CHECK_EQ(missed, 1);
    net.run(milliseconds{500}, {a.get(), b.get()});
    CHECK_EQ(missed, 1);  // not repeated while silence continues

    pub.write(pose(2));
    net.run(milliseconds{200}, {a.get(), b.get()});
    CHECK_EQ(missed, 2);
    CHECK_EQ(sub.stats().deadline_missed, 2U);
}

TEST(steady_traffic_never_misses_the_deadline) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose", Qos::best_effort().with_deadline(milliseconds{100}));
    net.run(milliseconds{100}, {a.get(), b.get()});
    for (std::int32_t i = 0; i < 40; ++i) {
        pub.write(pose(i));
        net.run(milliseconds{50}, {a.get(), b.get()});
    }
    CHECK_EQ(sub.stats().deadline_missed, 0U);
    CHECK_EQ(sub.stats().received, 40U);
}

TEST(lost_participant_discards_reception_state) {
    TestNet net;
    auto a = net.make("alpha", 1);
    auto b = net.make("beta", 2);
    auto pub = a->create_publisher<Pose>("pose");
    auto sub = b->create_subscriber<Pose>("pose", Qos::best_effort().with_deadline(milliseconds{100}));
    net.run(milliseconds{100}, {a.get(), b.get()});
    pub.write(pose(1));
    net.run(milliseconds{20}, {a.get(), b.get()});
    pub = Publisher<Pose>();  // endpoints must not outlive their participant
    a.reset();  // BYE removes the peer, so its silence is no longer a deadline violation
    net.run(milliseconds{500}, {b.get()});
    CHECK_EQ(sub.stats().deadline_missed, 0U);
}
