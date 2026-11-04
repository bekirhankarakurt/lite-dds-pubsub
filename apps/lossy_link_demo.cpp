// Runs a publisher and two subscribers over a simulated lossy link (no sockets, virtual time),
// contrasting best-effort with reliable QoS. Deterministic for a given --seed.

#include <iostream>
#include <random>

#include "args.hpp"
#include "ldds/ldds.hpp"

int main(int argc, char** argv) {
    app::Args args("lossy_link_demo", "Compare best-effort and reliable delivery over a lossy simulated link.",
                   {
                       {"loss", app::Kind::Int, "datagram loss in percent", "20"},
                       {"samples", app::Kind::Int, "samples to publish", "200"},
                       {"seed", app::Kind::Int, "random seed", "1"},
                       {"depth", app::Kind::Int, "writer history depth", "32"},
                   });
    if (!args.parse(argc, argv)) return args.exit_code();

    try {
        using std::chrono::milliseconds;
        const auto loss = static_cast<unsigned>(args.integer("loss"));
        const std::int64_t samples = args.integer("samples");
        if (loss > 100) throw std::invalid_argument("--loss must be 0-100");

        auto bus = ldds::MemoryBus::create();
        std::mt19937 rng(static_cast<std::uint32_t>(args.integer("seed")));
        bus->set_drop_filter([&rng, loss](const ldds::Bytes&) { return rng() % 100 < loss; });
        auto clock = std::make_shared<ldds::ManualClock>();
        const auto make = [&](const char* name) {
            ldds::Config cfg;
            cfg.name = name;
            cfg.clock = clock;
            return std::make_unique<ldds::Participant>(bus->connect(), cfg);
        };
        auto source = make("source");
        auto sink = make("sink");

        const auto depth = static_cast<std::uint16_t>(args.integer("depth"));
        auto be_pub = source->create_publisher<std::int64_t>("demo/best_effort", ldds::Qos::best_effort(depth));
        auto rel_pub = source->create_publisher<std::int64_t>("demo/reliable", ldds::Qos::reliable(depth));
        auto be_sub = sink->create_subscriber<std::int64_t>("demo/best_effort", ldds::Qos::best_effort(depth),
                                                            [](const std::int64_t&, const ldds::SampleInfo&) {});
        std::int64_t rel_last = 0;
        std::int64_t rel_out_of_order = 0;
        auto rel_sub = sink->create_subscriber<std::int64_t>(
            "demo/reliable", ldds::Qos::reliable(depth), [&](const std::int64_t& v, const ldds::SampleInfo&) {
                rel_out_of_order += v <= rel_last ? 1 : 0;
                rel_last = v;
            });

        const auto step = [&](milliseconds total) {
            for (milliseconds t{0}; t < total; t += milliseconds{5}) {
                clock->advance(milliseconds{5});
                for (int pass = 0; pass < 3; ++pass) {
                    source->spin_once(milliseconds{0});
                    sink->spin_once(milliseconds{0});
                }
            }
        };
        step(milliseconds{3000});  // discovery has to survive the loss too
        for (std::int64_t i = 1; i <= samples; ++i) {
            (void)be_pub.write(i);
            (void)rel_pub.write(i);
            step(milliseconds{10});
        }
        step(milliseconds{1000});

        std::cout << "link loss " << loss << "%, " << samples << " samples per topic, "
                  << bus->datagrams_dropped() << "/" << bus->datagrams_sent() << " datagrams dropped\n";
        const auto report = [&](const char* label, const ldds::ReaderStats& st) {
            std::cout << label << " received " << st.received << "/" << samples << "  lost " << st.lost
                      << "  nacks " << st.nacks_sent << '\n';
        };
        report("best-effort:", be_sub.stats());
        report("reliable:   ", rel_sub.stats());
        std::cout << "reliable retransmissions: " << rel_pub.stats().retransmitted
                  << ", out-of-order deliveries: " << rel_out_of_order << '\n';
    } catch (const std::exception& e) {
        std::cerr << "lossy_link_demo: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
