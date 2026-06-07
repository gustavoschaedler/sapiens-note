#include "sapiens/Id.h"

#include <array>
#include <cstdio>

namespace sapiens {

IdGenerator::IdGenerator(IClock& clock, std::uint64_t seed)
    : clock_(clock) {
    if (seed == 0) {
        std::random_device rd;
        seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }
    rng_.seed(seed);
}

std::string IdGenerator::next() {
    const Millis ms = clock_.now_ms();
    if (ms == last_ms_) {
        ++counter_;
    } else {
        last_ms_ = ms;
        counter_ = 0;
    }

    // 48-bit timestamp | 16-bit counter | 64-bit random -> 32 hex chars.
    const std::uint64_t ts = static_cast<std::uint64_t>(ms) & 0xFFFFFFFFFFFFull;
    const std::uint64_t hi = (ts << 16) | (counter_ & 0xFFFFu);
    const std::uint64_t lo = rng_();

    std::array<char, 33> buf{};
    std::snprintf(buf.data(), buf.size(), "%016llx%016llx",
                  static_cast<unsigned long long>(hi),
                  static_cast<unsigned long long>(lo));
    return std::string(buf.data());
}

} // namespace sapiens
