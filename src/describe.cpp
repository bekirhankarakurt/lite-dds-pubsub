#include "ldds/describe.hpp"

#include <cstdio>
#include <sstream>

namespace ldds {
namespace {

std::string describe_endpoint(const EndpointInfo& e) {
    std::ostringstream os;
    os << (e.kind == EndpointKind::Writer ? "W#" : "R#") << e.id << ' ' << e.topic << " [" << e.type_name
       << "] " << to_string(e.qos.reliability) << " depth=" << e.qos.history_depth;
    if (e.qos.deadline.count() > 0) os << " deadline=" << e.qos.deadline.count() << "ms";
    return os.str();
}

}  // namespace

std::string short_id(std::uint64_t participant_id) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(participant_id & 0xFFFFFFFFU));
    return buf;
}

std::string describe(const Packet& packet) {
    std::ostringstream os;
    const std::string id = short_id(packet.participant_id);
    os << packet_name(packet.body) << ' ' << id;
    if (const auto* a = std::get_if<Announce>(&packet.body)) {
        os << " \"" << a->name << "\" lease=" << a->lease_ms << "ms endpoints=" << a->endpoints.size();
        for (const EndpointInfo& e : a->endpoints) os << "\n    " << describe_endpoint(e);
    } else if (const auto* d = std::get_if<Data>(&packet.body)) {
        os << ' ' << d->topic << " w#" << d->writer_id << " seq=" << d->seq << " bytes=" << d->payload.size()
           << (d->reliable ? " reliable" : " best-effort");
    } else if (const auto* h = std::get_if<Heartbeat>(&packet.body)) {
        os << ' ' << h->topic << " w#" << h->writer_id << " seq=[" << h->first_seq << ".." << h->last_seq << ']';
    } else if (const auto* n = std::get_if<Nack>(&packet.body)) {
        os << " -> " << short_id(n->target) << " w#" << n->writer_id << " missing=[";
        for (std::size_t i = 0; i < n->seqs.size(); ++i) os << (i != 0 ? "," : "") << n->seqs[i];
        os << ']';
    }
    return os.str();
}

}  // namespace ldds
