// Subscribes to the IMU topic, prints samples and optionally logs them to CSV.
// Can read live traffic, record it to a capture file, or replay a capture offline.

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "args.hpp"
#include "ldds/ldds.hpp"
#include "messages.hpp"
#include "util.hpp"

int main(int argc, char** argv) {
    std::vector<app::Option> options = {
        {"name", app::Kind::Text, "participant name announced to peers", "logger"},
        {"topic", app::Kind::Text, "topic to subscribe to", app::kDefaultTopic},
        {"count", app::Kind::Int, "stop after N samples (0 = unlimited)", "0"},
        {"duration", app::Kind::Real, "stop after N seconds (0 = unlimited)", "0"},
        {"depth", app::Kind::Int, "reader history depth", "16"},
        {"deadline", app::Kind::Int, "report a miss after N ms without samples (0 = off)", "0"},
        {"best-effort", app::Kind::Flag, "use best-effort instead of reliable QoS", ""},
        {"csv", app::Kind::Text, "append samples to this CSV file", ""},
        {"record", app::Kind::Text, "record raw network traffic to this capture file", ""},
        {"replay", app::Kind::Text, "read traffic from a capture file instead of the network", ""},
        {"quiet", app::Kind::Flag, "do not print every sample", ""},
    };
    for (auto& o : app::network_options()) options.push_back(std::move(o));
    app::Args args("logger_subscriber", "Print and log samples from a DDS-style topic.", options);
    if (!args.parse(argc, argv)) return args.exit_code();

    try {
        app::install_signal_handlers();
        std::unique_ptr<ldds::Transport> transport;
        const ldds::ReplayTransport* replay = nullptr;
        if (!args.text("replay").empty()) {
            auto feed = std::make_unique<ldds::ReplayTransport>(args.text("replay"));
            replay = feed.get();
            transport = std::move(feed);
        } else {
            transport = ldds::make_udp_transport(app::udp_config(args));
            if (!args.text("record").empty()) {
                transport = std::make_unique<ldds::RecordingTransport>(std::move(transport), args.text("record"));
            }
        }

        ldds::Config cfg;
        cfg.name = args.text("name");
        ldds::Participant participant(std::move(transport), cfg);
        participant.set_event_handler([](const ldds::Event& e) {
            switch (e.kind) {
                case ldds::EventKind::ParticipantDiscovered:
                    std::cout << "+ participant \"" << e.remote_name << "\" (" << ldds::short_id(e.remote_id) << ")\n";
                    break;
                case ldds::EventKind::ParticipantLost:
                    std::cout << "- participant \"" << e.remote_name << "\" (" << ldds::short_id(e.remote_id) << ")\n";
                    break;
                case ldds::EventKind::DeadlineMissed:
                    std::cout << "! deadline missed on " << e.topic << " from \"" << e.remote_name << "\"\n";
                    break;
                case ldds::EventKind::SamplesLost:
                    std::cout << "! lost " << e.count << " sample(s) on " << e.topic << " from \"" << e.remote_name
                              << "\"\n";
                    break;
            }
        });

        std::ofstream csv;
        if (!args.text("csv").empty()) {
            csv.open(args.text("csv"), std::ios::app);
            if (!csv) throw std::runtime_error("cannot open " + args.text("csv"));
            csv << "counter,time_s,ax,ay,az,gx,gy,gz\n";
        }

        const bool quiet = args.flag("quiet");
        std::int64_t received = 0;
        ldds::Qos qos = args.flag("best-effort") ? ldds::Qos::best_effort() : ldds::Qos::reliable();
        qos.history_depth = static_cast<std::uint16_t>(args.integer("depth"));
        qos.deadline = std::chrono::milliseconds{args.integer("deadline")};
        auto sub = participant.create_subscriber<app::ImuSample>(
            args.text("topic"), qos, [&](const app::ImuSample& s, const ldds::SampleInfo& info) {
                ++received;
                if (csv.is_open()) {
                    csv << s.counter << ',' << s.time_s << ',' << s.ax << ',' << s.ay << ',' << s.az << ',' << s.gx
                        << ',' << s.gy << ',' << s.gz << '\n';
                }
                if (quiet) return;
                std::cout << std::fixed << "seq=" << std::setw(4) << info.seq << " t=" << std::setprecision(3)
                          << s.time_s << " accel=(" << std::setprecision(3) << s.ax << ", " << s.ay << ", " << s.az
                          << ") gyro=(" << std::setprecision(3) << s.gx << ", " << s.gy << ", " << s.gz << ") from "
                          << ldds::short_id(info.participant_id) << '\n';
            });
        std::cout << "participant " << ldds::short_id(participant.id()) << " \"" << cfg.name << "\" listening on "
                  << sub.topic() << " (" << ldds::to_string(qos.reliability) << ")" << std::endl;

        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        const std::int64_t limit = args.integer("count");
        const double max_seconds = args.real("duration");
        while (!app::stop_requested()) {
            if (limit > 0 && received >= limit) break;
            if (max_seconds > 0 && std::chrono::duration<double>(Clock::now() - start).count() >= max_seconds) break;
            if (replay != nullptr && replay->exhausted()) {
                participant.spin_once(std::chrono::milliseconds{0});
                break;
            }
            participant.spin_once(std::chrono::milliseconds{10});
        }

        const ldds::ReaderStats st = sub.stats();
        std::cout << "received=" << st.received << " lost=" << st.lost << " duplicates=" << st.duplicates
                  << " deadline_missed=" << st.deadline_missed << " nacks_sent=" << st.nacks_sent << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "logger_subscriber: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
