#include <cstdio>
#include <filesystem>

#include "fixture.hpp"

using namespace ldds;
using std::chrono::milliseconds;

namespace {

struct TempFile {
    explicit TempFile(const std::string& name)
        : path((std::filesystem::temp_directory_path() / ("ldds_test_" + name)).string()) {}
    ~TempFile() { std::remove(path.c_str()); }
    std::string path;
};

}  // namespace

TEST(capture_round_trip) {
    TempFile file("roundtrip.cap");
    {
        CaptureWriter w(file.path);
        w.write(111, Bytes{1, 2, 3});
        w.write(222, Bytes{});
    }
    CaptureReader r(file.path);
    const auto first = r.next();
    REQUIRE(first.has_value());
    CHECK_EQ(first->timestamp_ns, 111);
    CHECK(first->data == (Bytes{1, 2, 3}));
    const auto second = r.next();
    REQUIRE(second.has_value());
    CHECK(second->data.empty());
    CHECK(!r.next().has_value());
}

TEST(invalid_capture_files_are_rejected) {
    TempFile file("invalid.cap");
    {
        std::FILE* f = std::fopen(file.path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fputs("not a capture", f);
        std::fclose(f);
    }
    CHECK_THROWS(CaptureReader(file.path), DecodeError);
    CHECK_THROWS(CaptureReader(file.path + ".missing"), std::runtime_error);
}

TEST(truncated_record_is_detected) {
    TempFile file("truncated.cap");
    {
        CaptureWriter w(file.path);
        w.write(1, Bytes(32, 7));
    }
    std::filesystem::resize_file(file.path, std::filesystem::file_size(file.path) - 5);
    CaptureReader r(file.path);
    CHECK_THROWS(r.next(), DecodeError);
}

TEST(recorded_session_replays_into_a_subscriber) {
    TempFile file("session.cap");
    {
        TestNet net;
        auto a = net.make("alpha", 1);
        Config cfg;
        cfg.name = "beta";
        cfg.id = 2;
        cfg.clock = std::make_shared<ManualClock>();
        Participant b(std::make_unique<RecordingTransport>(net.bus()->connect(), file.path), cfg);
        auto pub = a->create_publisher<Pose>("pose");
        auto sub = b.create_subscriber<Pose>("pose");
        net.run(milliseconds{100}, {a.get(), &b});
        for (std::int32_t i = 1; i <= 3; ++i) {
            pub.write(pose(i));
            net.run(milliseconds{10}, {a.get(), &b});
        }
        CHECK_EQ(sub.take_all().size(), 3U);
    }

    auto replay = std::make_unique<ReplayTransport>(file.path);
    ReplayTransport* feed = replay.get();
    Participant offline(std::move(replay));
    auto sub = offline.create_subscriber<Pose>("pose");
    while (!feed->exhausted()) offline.spin_once(milliseconds{0});
    const auto samples = sub.take_all();
    REQUIRE_EQ(samples.size(), 3U);
    CHECK_EQ(samples[0].value.id, 1);
    CHECK_EQ(samples[2].info.seq, 3U);
}
