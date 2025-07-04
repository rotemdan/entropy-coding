#pragma once

#include <cstdint>
#include <exception>

#if __cplusplus >= 202002L
#include <bit>
#endif

struct QuotientAndRemainderUint32 {
	uint32_t quotient;
	uint32_t remainder;
};

// Uses precomputed "magic numbers" to efficiently compute division
// of unsigned 31-bit integers, using only a single 64-bit multiplication and a single right shift.
//
// The algorithm only works correctly for numerators and divisors between 0 and 2^31.
//
// Modifying it to produce correct results for numerators or divisors between 2^31 and 2^32,
// would significantly increase its complexity.
//
// Based on ideas from the book:
// "Hacker's Delight" (Chapter 10), by Henry S. Warren, Jr. (2002)
class FastUint31Division {
   private:
    uint32_t divisor;

	uint64_t multiplier;
	uint8_t shiftAmount;

   public:
    FastUint31Division() {
		divisor = 0;
		multiplier = 0;
		shiftAmount = 0;
	}

	FastUint31Division(uint32_t divisor) {
		this->divisor = divisor;

		// If divisor is 0, set magic values that produce a result of 0 for any numerator
		if (divisor == 0) {
			multiplier = 0;
			shiftAmount = 0;

			return;
		}

		if (divisor >= (1ULL << 31)) {
			throw std::exception("Divisor can't be greater or equal to 2^31");
		}

		// Get the exponent of closest power of two greater or equal to the divisor
		auto divisorBitWidth = GetExponentOfClosestPowerOfTwoGreaterOrEqualTo(divisor);

		// Compute a "magic" multiplier and shift amount
		shiftAmount = 32 + divisorBitWidth;

		multiplier =  ((1ULL << shiftAmount) + (divisor - 1)) / divisor;
	}

	inline uint32_t Divide(uint32_t numerator) {
		return (numerator * multiplier) >> shiftAmount;
	}

	inline QuotientAndRemainderUint32 DivideAndGetRemainder(uint32_t numerator) {
		uint32_t quotient = (numerator * multiplier) >> shiftAmount;
		uint32_t remainder = numerator - (quotient * divisor);

		return { quotient, remainder };
	}

	// Finds exponent of closest power of two greater or equal to the given value.
	static uint8_t GetExponentOfClosestPowerOfTwoGreaterOrEqualTo(uint64_t value) {
		if (value <= 1) {
			return 0;
		}

#if __cplusplus >= 202002L
		// If compiled with C++20 or higher supported, use std::bit_width
		return std::bit_width(value - 1);
#else
		// Otherwise fall back to slower version
		uint8_t exponent = 0;

		uint64_t val = value - 1;

		while (val > 0) {
			exponent++;

			val >>= 1;
		}

		return exponent;
#endif
	}
};
