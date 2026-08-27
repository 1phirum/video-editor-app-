#include "core/ids.h"

#include <chrono>
#include <random>

namespace core {

std::string new_id(const std::string& prefix) {
    // Mirrors Python's uuid4().hex[:12] — 12 random lowercase hex chars.
    // Seed mixes random_device with a clock reading because MinGW's
    // random_device has historically been deterministic.
    static thread_local std::mt19937_64 rng{
        static_cast<std::uint64_t>(std::random_device{}()) ^
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count())};
    static constexpr char kHex[] = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);

    std::string id;
    id.reserve(prefix.size() + 1 + 12);
    id += prefix;
    id += '_';
    for (int i = 0; i < 12; ++i)
        id += kHex[dist(rng)];
    return id;
}

}  // namespace core
