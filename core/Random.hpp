#pragma once
#include <stdint.h>

namespace xl {

// Small xorshift32 generator, seeded by the caller so tests repeat.
struct Rng {
	uint32_t state;

	Rng() : state(0x9E3779B9u) {}

	void seed(uint32_t s) {
		// Zero would lock xorshift at zero.
		state = s ? s : 0x9E3779B9u;
	}

	uint32_t next() {
		uint32_t x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state = x;
		return x;
	}

	// Uniform 0..n-1 (n >= 1).
	int below(int n) {
		if (n <= 1)
			return 0;
		// Top bits through a 64-bit multiply: no modulo bias worth noting at these n.
		return (int) (((uint64_t) next() * (uint64_t) n) >> 32);
	}
};

} // namespace xl
