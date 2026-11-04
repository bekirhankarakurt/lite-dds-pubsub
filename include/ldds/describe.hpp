#pragma once

#include <cstdint>
#include <string>

#include "ldds/wire.hpp"

namespace ldds {

/// Short hexadecimal form of a participant id (low 32 bits).
[[nodiscard]] std::string short_id(std::uint64_t participant_id);

/// Human-readable, possibly multi-line description of a packet (used by the spy tool).
[[nodiscard]] std::string describe(const Packet& packet);

}  // namespace ldds
