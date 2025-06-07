#pragma once

#include "Utilities.h"

#include <cstdint>
#include <exception>

// Uses fixed-point arithmetic to compute `x * fraction`
// where `x` is a uint32 and `fraction` is between 0.0 and 1.0
class FastUint32MultiplicationByFraction {
   private:
	uint64_t scaledMultiplier;

   public:
	const uint64_t scaleFactor = 1ULL << 32;

	FastUint32MultiplicationByFraction(double fractionBetween0And1) {
		if (fractionBetween0And1 < 0.0 || fractionBetween0And1 > 1.0) {
			throw std::exception("Fraction must be between 0.0 and 1.0 (inclusive)");
		}

		// Compute the multiplier
		scaledMultiplier = uint64_t(fractionBetween0And1 * scaleFactor);
	}

	uint32_t Multiply(uint32_t multiplicand) {
		// Efficiently computes (multiplicand * (fractionBetween0And1 * scaleFactor)) / scaleFactor
		return uint32_t((multiplicand * scaledMultiplier) >> 32);
	}
};
