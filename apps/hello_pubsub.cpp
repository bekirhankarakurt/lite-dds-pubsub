// Smallest end-to-end example: two participants in one process exchange a typed sample.
// Swap the MemoryBus for ldds::make_udp_transport({}) to talk over real multicast.

#include <chrono>
#include <iostream>

#include "ldds/ldds.hpp"

struct Temperature {
    double celsius{0};
    std::string sensor;
};

template <>
struct ldds::Serializer<Temperature> {
    static std::string type_name() { return "example/Temperature"; }
    static void write(ByteWriter& w, const Temperature& t) {
        w.f64(t.celsius);
        w.str(t.sensor);
    }
    static Temperature read(ByteReader& r) {
        Temperature t;
        t.celsius = r.f64();
        t.sensor = r.str();
        return t;
    }
};

int main() {
    using std::chrono::milliseconds;
    auto bus = ldds::MemoryBus::create();
    ldds::Participant publisher_node(bus->connect());
    ldds::Participant subscriber_node(bus->connect());

    auto pub = publisher_node.create_publisher<Temperature>("lab/temperature", ldds::Qos::reliable());
    auto sub = subscriber_node.create_subscriber<Temperature>(
        "lab/temperature", ldds::Qos::reliable(), [](const Temperature& t, const ldds::SampleInfo& info) {
            std::cout << t.sensor << " reports " << t.celsius << " C (seq " << info.seq << ")\n";
        });

    while (pub.matched_subscribers() == 0) {  // discovery happens inside spin_once
        publisher_node.spin_once(milliseconds{10});
        subscriber_node.spin_once(milliseconds{10});
    }
    pub.write(Temperature{21.5, "bench-1"});
    pub.write(Temperature{21.7, "bench-1"});
    subscriber_node.spin_once(milliseconds{10});
    return 0;
}
