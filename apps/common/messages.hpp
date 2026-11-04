#pragma once

#include <cstdint>
#include <string>

#include "ldds/serialization.hpp"

namespace app {

/// Inertial measurement sample published by the demo sensor.
struct ImuSample {
    std::uint32_t counter{0};
    double time_s{0};
    float ax{0}, ay{0}, az{0};  ///< m/s^2
    float gx{0}, gy{0}, gz{0};  ///< rad/s
};

inline constexpr const char* kDefaultTopic = "sensors/imu";

}  // namespace app

template <>
struct ldds::Serializer<app::ImuSample> {
    static std::string type_name() { return "ldds_demo/ImuSample"; }

    static void write(ByteWriter& w, const app::ImuSample& s) {
        w.u32(s.counter);
        w.f64(s.time_s);
        for (const float v : {s.ax, s.ay, s.az, s.gx, s.gy, s.gz}) w.f32(v);
    }

    static app::ImuSample read(ByteReader& r) {
        app::ImuSample s;
        s.counter = r.u32();
        s.time_s = r.f64();
        s.ax = r.f32();
        s.ay = r.f32();
        s.az = r.f32();
        s.gx = r.f32();
        s.gy = r.f32();
        s.gz = r.f32();
        return s;
    }
};
