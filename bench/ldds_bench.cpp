// Latency and throughput benchmark. Paths:
//   loopback  one participant, publisher and subscriber in the same process (local shortcut)
//   mem       two participants over the in-process MemoryBus
//   udp       two participants over real UDP multicast on this host

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include "args.hpp"
#include "ldds/ldds.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Blob = std::vector<std::uint8_t>;

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct Rig {
    std::unique_ptr<ldds::Participant> tx;
    std::unique_ptr<ldds::Participant> rx;  // null for the loopback path (rx == tx)
    ldds::Participant& sub_side() const { return rx ? *rx : *tx; }
};

double percentile(const std::vector<double>& sorted, double p) {
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<app::Option> options = {
        {"path", app::Kind::Text, "loopback | mem | udp", "udp"},
        {"size", app::Kind::Int, "payload bytes per sample", "64"},
        {"latency-samples", app::Kind::Int, "ping-style samples for the latency run", "2000"},
        {"throughput-samples", app::Kind::Int, "samples for the throughput run", "20000"},
        {"best-effort", app::Kind::Flag, "use best-effort instead of reliable QoS", ""},
    };
    for (auto& o : app::network_options()) options.push_back(std::move(o));
    app::Args args("ldds_bench", "Measure one-way latency and throughput of lite-dds-pubsub.", options);
    if (!args.parse(argc, argv)) return args.exit_code();

    try {
        const std::string path = args.text("path");
        const auto size = static_cast<std::size_t>(args.integer("size"));
        if (size < 8 || size > 60000) throw std::invalid_argument("--size must be 8-60000");

        Rig rig;
        auto bus = ldds::MemoryBus::create();
        const auto make = [&](const char* name) {
            ldds::Config cfg;
            cfg.name = name;
            std::unique_ptr<ldds::Transport> t = path == "mem" ? bus->connect() : ldds::make_udp_transport(app::udp_config(args));
            return std::make_unique<ldds::Participant>(std::move(t), cfg);
        };
        if (path != "loopback" && path != "mem" && path != "udp") throw std::invalid_argument("unknown --path '" + path + "'");
        rig.tx = make("bench-tx");
        if (path != "loopback") rig.rx = make("bench-rx");

        const ldds::Qos qos = args.flag("best-effort") ? ldds::Qos::best_effort(256) : ldds::Qos::reliable(256);
        std::atomic<std::uint64_t> received{0};
        std::atomic<std::int64_t> last_latency_ns{0};
        auto sub = rig.sub_side().create_subscriber<Blob>(
            "bench/blob", qos, [&](const Blob&, const ldds::SampleInfo& info) {
                last_latency_ns = now_ns() - info.timestamp_ns;
                ++received;
            });
        auto pub = rig.tx->create_publisher<Blob>("bench/blob", qos);
        rig.tx->start();
        if (rig.rx) rig.rx->start();
        const auto deadline = Clock::now() + std::chrono::seconds{5};
        while (pub.matched_subscribers() == 0 && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds{5});
        if (pub.matched_subscribers() == 0) throw std::runtime_error("subscriber was not discovered within 5 s");
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        const Blob payload(size, 0xA5);
        std::vector<double> latencies_us;
        const std::int64_t latency_samples = args.integer("latency-samples");
        std::uint64_t timeouts = 0;
        for (std::int64_t i = 0; i < latency_samples; ++i) {
            const std::uint64_t before = received;
            (void)pub.write(payload);
            const auto give_up = Clock::now() + std::chrono::milliseconds{200};
            while (received == before && Clock::now() < give_up) std::this_thread::yield();
            if (received == before) {
                ++timeouts;
            } else {
                latencies_us.push_back(static_cast<double>(last_latency_ns.load()) / 1000.0);
            }
        }
        std::sort(latencies_us.begin(), latencies_us.end());

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "path=" << path << " size=" << size << " B qos=" << ldds::to_string(qos.reliability) << '\n';
        if (!latencies_us.empty()) {
            std::cout << "latency     n=" << latencies_us.size() << "  min=" << latencies_us.front()
                      << "  p50=" << percentile(latencies_us, 0.50) << "  p99=" << percentile(latencies_us, 0.99)
                      << "  max=" << latencies_us.back() << " us  (timeouts=" << timeouts << ")\n";
        }

        const std::int64_t burst = args.integer("throughput-samples");
        const std::uint64_t base = received;
        const auto t0 = Clock::now();
        for (std::int64_t i = 0; i < burst; ++i) (void)pub.write(payload);
        auto last_progress = Clock::now();
        std::uint64_t seen = received;
        while (received - base < static_cast<std::uint64_t>(burst) &&
               Clock::now() - last_progress < std::chrono::milliseconds{500}) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            if (received != seen) {
                seen = received;
                last_progress = Clock::now();
            }
        }
        const double secs = std::chrono::duration<double>(last_progress - t0).count();
        const std::uint64_t got = received - base;
        const double msgs_per_s = secs > 0 ? static_cast<double>(got) / secs : 0.0;
        std::cout << "throughput  sent=" << burst << "  received=" << got << " ("
                  << 100.0 * static_cast<double>(got) / static_cast<double>(burst) << "%)  " << std::setprecision(0)
                  << msgs_per_s << " msg/s  " << std::setprecision(1)
                  << msgs_per_s * static_cast<double>(size) / 1e6 << " MB/s\n";

        rig.tx->stop();
        if (rig.rx) rig.rx->stop();
    } catch (const std::exception& e) {
        std::cerr << "ldds_bench: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
