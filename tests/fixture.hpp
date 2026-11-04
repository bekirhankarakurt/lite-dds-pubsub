#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "check.hpp"
#include "ldds/ldds.hpp"

struct Pose {
    std::int32_t id{0};
    double x{0};
    std::string label;
};

struct Other {
    std::int32_t id{0};
};

template <>
struct ldds::Serializer<Pose> {
    static std::string type_name() { return "test/Pose"; }
    static void write(ByteWriter& w, const Pose& v) {
        w.u32(static_cast<std::uint32_t>(v.id));
        w.f64(v.x);
        w.str(v.label);
    }
    static Pose read(ByteReader& r) {
        Pose p;
        p.id = static_cast<std::int32_t>(r.u32());
        p.x = r.f64();
        p.label = r.str();
        return p;
    }
};

template <>
struct ldds::Serializer<Other> {
    static std::string type_name() { return "test/Other"; }
    static void write(ByteWriter& w, const Other& v) { w.u32(static_cast<std::uint32_t>(v.id)); }
    static Other read(ByteReader& r) { return Other{static_cast<std::int32_t>(r.u32())}; }
};

/// Deterministic in-memory network with a manual clock.
class TestNet {
public:
    TestNet() : bus_(ldds::MemoryBus::create()), clock_(std::make_shared<ldds::ManualClock>()) {}

    std::unique_ptr<ldds::Participant> make(const std::string& name, std::uint64_t id) {
        ldds::Config cfg;
        cfg.name = name;
        cfg.id = id;
        cfg.clock = clock_;
        return std::make_unique<ldds::Participant>(bus_->connect(), cfg);
    }

    /// Advances virtual time in 5 ms steps, letting the participants exchange packets.
    void run(std::chrono::milliseconds total, const std::vector<ldds::Participant*>& nodes) {
        using std::chrono::milliseconds;
        for (milliseconds t{0}; t < total; t += milliseconds{5}) {
            clock_->advance(milliseconds{5});
            for (int pass = 0; pass < 3; ++pass) {
                for (ldds::Participant* p : nodes) p->spin_once(milliseconds{0});
            }
        }
    }

    [[nodiscard]] const std::shared_ptr<ldds::MemoryBus>& bus() const { return bus_; }

private:
    std::shared_ptr<ldds::MemoryBus> bus_;
    std::shared_ptr<ldds::ManualClock> clock_;
};

inline Pose pose(std::int32_t id) { return Pose{id, id * 0.5, "p" + std::to_string(id)}; }
