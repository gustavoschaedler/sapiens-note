#pragma once
// Time-ordered identifier generation (UUIDv7-style: 48-bit ms timestamp prefix
// + random suffix), rendered as a lowercase hex string. See docs/DATABASE.md.

#include <random>
#include <string>

#include "sapiens/Hal.h"

namespace sapiens {

class IdGenerator {
public:
    explicit IdGenerator(IClock& clock, std::uint64_t seed = 0);

    // Returns a 32-char lowercase hex id, monotonic within a millisecond via a
    // per-ms counter so ids stay ordered even when generated rapidly.
    std::string next();

private:
    IClock& clock_;
    std::mt19937_64 rng_;
    Millis last_ms_{0};
    std::uint32_t counter_{0};
};

} // namespace sapiens
