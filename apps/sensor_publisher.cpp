// Publishes a simulated IMU stream. No hardware needed: the signal is synthetic and deterministic.

#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

#include "args.hpp"
#include "ldds/ldds.hpp"
#include "messages.hpp"
#include "util.hpp"

namespace {

app::ImuSample simulate(std::uint32_t counter, double rate_hz) {
    constexpr double kTwoPi = 6.283185307179586;
    const double t = counter / rate_hz;
    app::ImuSample s;
    s.counter = counter;
    s.time_s = t;
    s.ax = static_cast<float>(0.35 * std::sin(kTwoPi * 0.8 * t));
    s.ay = static_cast<float>(0.20 * std::cos(kTwoPi * 0.5 * t));
    s.az = static_cast<float>(9.80665 + 0.05 * std::sin(kTwoPi * 3.0 * t));
    s.gx = static_cast<float>(0.10 * std::sin(kTwoPi * 0.25 * t));
    s.gy = static_cast<float>(0.05 * std::cos(kTwoPi * 0.40 * t));
    s.gz = static_cast<float>(0.02 * std::sin(kTwoPi * 1.00 * t));
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<app::Option> options = {
        {"name", app::Kind::Text, "participant name announced to peers", "sensor"},
        {"topic", app::Kind::Text, "topic to publish on", app::kDefaultTopic},
        {"rate", app::Kind::Real, "samples per second", "50"},
        {"count", app::Kind::Int, "stop after N samples (0 = run until Ctrl-C)", "0"},
        {"depth", app::Kind::Int, "history depth kept for retransmission", "16"},
        {"best-effort", app::Kind::Flag, "use best-effort instead of reliable QoS", ""},
        {"wait", app::Kind::Real, "seconds to wait for a subscriber before publishing", "5"},
    };
    for (auto& o : app::network_options()) options.push_back(std::move(o));
    app::Args args("sensor_publisher", "Publish simulated IMU samples on a DDS-style topic.", options);
    if (!args.parse(argc, argv)) return args.exit_code();

    try {
        const double rate = args.real("rate");
        if (rate <= 0) throw std::invalid_argument("--rate must be positive");
        app::install_signal_handlers();

        ldds::Config cfg;
        cfg.name = args.text("name");
        ldds::Participant participant(ldds::make_udp_transport(app::udp_config(args)), cfg);
        const auto depth = static_cast<std::uint16_t>(args.integer("depth"));
        const ldds::Qos qos = args.flag("best-effort") ? ldds::Qos::best_effort(depth) : ldds::Qos::reliable(depth);
        auto pub = participant.create_publisher<app::ImuSample>(args.text("topic"), qos);

        using Clock = std::chrono::steady_clock;
        const auto wait_until = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                   std::chrono::duration<double>(args.real("wait")));
        std::cout << "participant " << ldds::short_id(participant.id()) << " \"" << cfg.name << "\" publishing "
                  << pub.topic() << " (" << ldds::to_string(qos.reliability) << ", " << rate << " Hz)" << std::endl;
        while (pub.matched_subscribers() == 0 && Clock::now() < wait_until && !app::stop_requested()) {
            participant.spin_once(std::chrono::milliseconds{10});
        }
        std::cout << "subscribers matched: " << pub.matched_subscribers() << std::endl;

        const std::int64_t limit = args.integer("count");
        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate));
        auto next = Clock::now();
        std::uint32_t counter = 0;
        while (!app::stop_requested() && (limit == 0 || counter < limit)) {
            pub.write(simulate(++counter, rate));
            next += period;
            while (Clock::now() < next && !app::stop_requested()) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now());
                participant.spin_once(std::min(left, std::chrono::milliseconds{10}));
            }
        }
        // Let late NACKs for the final samples be served.
        const auto linger = Clock::now() + std::chrono::milliseconds{300};
        while (Clock::now() < linger && !app::stop_requested()) participant.spin_once(std::chrono::milliseconds{10});
        std::cout << "published " << pub.stats().published << " samples, retransmitted " << pub.stats().retransmitted
                  << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "sensor_publisher: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
