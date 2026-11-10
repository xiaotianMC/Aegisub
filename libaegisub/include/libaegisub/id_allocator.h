// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <atomic>
#include <limits>
#include <stdexcept>
namespace agi {
// Bounded instances test exhaustion without resetting the live allocator.
class IdAllocator {
    std::atomic<int> last{0};
    int const maximum;
public:
    explicit IdAllocator(int maximum = std::numeric_limits<int>::max()) : maximum(maximum) {
        if (maximum < 1) throw std::invalid_argument("ID maximum must be positive");
    }
    int Next() {
        int old = last.load(std::memory_order_relaxed);
        do {
            if (old >= maximum) throw std::overflow_error("Subtitle line ID exhausted");
        } while (!last.compare_exchange_weak(old, old + 1, std::memory_order_relaxed));
        return old + 1;
    }
};
}
