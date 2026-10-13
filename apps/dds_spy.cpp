// Passive network analyzer: decodes and prints every discovery/data/control packet on the multicast group.
// Can record a session to a capture file and decode capture files offline.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>

#include "args.hpp"
#include "ldds/ldds.hpp"
#include "util.hpp"

namespace {

struct Tally {
    std::map<std::string, std::uint64_t> by_type;
    std::uint64_t malformed{0};
    std::uint64_t total{0};
};

/// Prints one packet if it passes the filter; returns false for malformed input.
void show(const ldds::Bytes& datagram, std::int64_t timestamp_ns, const std::string& filter, Tally& tally) {
    ++tally.total;
    const auto packet = ldds::decode(datagram);
    if (!packet) {
        ++tally.malformed;
        if (filter == "all") std::cout << app::format_time(timestamp_ns) << " MALFORMED " << datagram.size() << " bytes\n";
        return;
    }
    const std::string name = ldds::packet_name(packet->body);
    ++tally.by_type[name];
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (filter != "all" && filter != lower) return;
    std::cout << app::format_time(timestamp_ns) << ' ' << ldds::describe(*packet) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<app::Option> options = {
        {"filter", app::Kind::Text, "show only: all, announce, data, heartbeat, nack, bye", "all"},
        {"count", app::Kind::Int, "stop after N packets (0 = unlimited)", "0"},
        {"duration", app::Kind::Real, "stop after N seconds (0 = unlimited)", "0"},
        {"record", app::Kind::Text, "write raw packets to this capture file", ""},
        {"replay", app::Kind::Text, "decode a capture file instead of listening", ""},
    };
    for (auto& o : app::network_options()) options.push_back(std::move(o));
    app::Args args("dds_spy", "Decode and print lite-dds-pubsub network traffic.", options);
    if (!args.parse(argc, argv)) return args.exit_code();

    try {
        const std::string filter = args.text("filter");
        static const char* const kFilters[] = {"all", "announce", "data", "heartbeat", "nack", "bye"};
        if (std::find(std::begin(kFilters), std::end(kFilters), filter) == std::end(kFilters)) {
            throw std::invalid_argument("unknown --filter '" + filter + "'");
        }
        const std::int64_t limit = args.integer("count");
        Tally tally;

        if (!args.text("replay").empty()) {
            ldds::CaptureReader reader(args.text("replay"));
            while (auto packet = reader.next()) {
                show(packet->data, packet->timestamp_ns, filter, tally);
                if (limit > 0 && static_cast<std::int64_t>(tally.total) >= limit) break;
            }
        } else {
            app::install_signal_handlers();
            std::unique_ptr<ldds::Transport> transport = ldds::make_udp_transport(app::udp_config(args));
            if (!args.text("record").empty()) {
                transport = std::make_unique<ldds::RecordingTransport>(std::move(transport), args.text("record"));
            }
            using Clock = std::chrono::steady_clock;
            const auto start = Clock::now();
            const double max_seconds = args.real("duration");
            std::cerr << "listening on " << args.text("group") << ':' << args.integer("port") << " via "
                      << args.text("iface") << " (Ctrl-C to stop)\n";
            while (!app::stop_requested()) {
                if (limit > 0 && static_cast<std::int64_t>(tally.total) >= limit) break;
                if (max_seconds > 0 && std::chrono::duration<double>(Clock::now() - start).count() >= max_seconds) break;
                if (auto datagram = transport->receive(std::chrono::milliseconds{100})) {
                    show(*datagram, ldds::wall_clock_ns(), filter, tally);
                }
            }
        }

        std::cout << "-- " << tally.total << " packets";
        for (const auto& entry : tally.by_type) std::cout << ", " << entry.first << '=' << entry.second;
        if (tally.malformed != 0) std::cout << ", MALFORMED=" << tally.malformed;
        std::cout << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "dds_spy: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
