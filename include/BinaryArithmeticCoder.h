#pragma once

#include "BitArray.h"
#include "OutputBitStream.h"
#include "Utilities.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// Binary arithmetic coder. Uses fixed-point integer arithmetic.
//////////////////////////////////////////////////////////////////////////////////////////////
namespace BinaryArithmeticCoder {

// Total range bit width. Cannot be higher than 32 bits.
inline constexpr uint8_t totalRangeBitWidth = 32;

// Range constants. Map the [0.0, 1.0) fractional range to integers.
inline constexpr uint64_t lowest = 0;
inline constexpr uint64_t highest = 1ULL << totalRangeBitWidth;
inline constexpr double highestAsDouble = double(highest);

inline constexpr uint64_t quarterRange = highest / 4;
inline constexpr uint64_t halfRange = highest / 2;
inline constexpr uint64_t threeQuartersRange = highest - quarterRange;

static uint64_t ComputeFixedPointMultiplierFor(double fractionBetween0And1) {
	uint64_t multiplier = uint64_t(fractionBetween0And1 * highestAsDouble);
	multiplier = EntropyCodingUtilities::clip(multiplier, 0ULL, highest - 1);

	return multiplier;
}

// Encode message bits
void Encode(BitArray& inputBitArray,
			OutputBitStream& outputBitStream,
			double probabilityOf1) {

	// Input bit array length
	auto inputBitLength = inputBitArray.BitLength();

	// Probability of 0 symbol
	double probabilityOf0 = 1.0 - probabilityOf1;

	// Probability of 0 symbol fixed-point multiplier
	uint64_t probabilityOf0FixedPointMultiplier = ComputeFixedPointMultiplierFor(probabilityOf0);

	// Current interval
	uint64_t low = lowest;
	uint64_t high = highest;

	// Pending bit count
	uint64_t pendingBitCount = 0;

	// Read position
	int64_t readPosition = 0;

	// Outputs a bit
	auto outputBit = [&](uint8_t bit) { outputBitStream.WriteBit(bit); };

	// Output all pending bits, with the given bit value
	auto outputPendingBitsAs = [&](uint8_t bit) {
		while (pendingBitCount > 0) {
			outputBit(bit);

			pendingBitCount -= 1;
		}
	};

	// Encode bit by bit
	for (int64_t readPosition = 0; readPosition < inputBitLength; readPosition++) {
		// Narrow current interval
		{
			// Read new bit from input
			uint8_t inputBit = inputBitArray.ReadBitAt(readPosition);

			// Calculate the boundary between symbols 0 and 1 within the current interval
			// This is the point where the sub-interval for 0 ends and 1 begins
			//
			// The boundary is computed via fast fixed-point arithmetic.
			//
			// To compute the sub-interval length, the current interval length is multiplied by
			// a precomputed integer multiplier equal to `highest * probabilityOf0`
			// and then divided by `highest` via a right shift by `totalRangeBitWidth`,
			// since `highest = 1 << totalRangeBitWidth`.
			//
			// The boundary is then computed as `low + lowerSubintervalLength`
			uint64_t intervalLength = high - low;
			uint64_t lowerSubintervalLength = (intervalLength * probabilityOf0FixedPointMultiplier) >> totalRangeBitWidth;
			uint64_t boundary = low + lowerSubintervalLength;

			if (inputBit == 0) {
				high = boundary;  // New interval is [low, boundary)
			} else {
				low = boundary;	 // New interval is [boundary, high)
			}
		}

		// Normalize the interval and output bits
		while (true) {
			if (high < halfRange) {	// Interval is in the lower half [0, 0.5)
				// Output 0
				outputBit(0);

				// Output pending bits as 1s
				outputPendingBitsAs(1);

				// Scale up interval
				low *= 2;
				high *= 2;
			} else if (low >= halfRange) {  // Interval is in the upper half [0.5, 1)
				// Output 1
				outputBit(1);

				// Output pending bits as 0s
				outputPendingBitsAs(0);

				// Shift and scale up the interval
				low = (low - halfRange) * 2;
				high = (high - halfRange) * 2;
			} else if (low >= quarterRange &&
					   high < threeQuartersRange) {	// Interval is in the middle half [0.25, 0.75)
				// Can't output a definitive bit yet, but the interval can be rescaled

				// Increment pending bit count
				pendingBitCount += 1;

				// Shift and scale up the interval, to prevent precision loss
				low = (low - quarterRange) * 2;
				high = (high - quarterRange) * 2;
			} else {
				// Can't output a bit or normalize yet
				break;
			}
		}
	}

	// Finalize
	{
		// Output the minimum number of bits required to uniquely identify the final interval

		// Account for the current interval's final bit resolution
		pendingBitCount += 1;

		if (low < quarterRange) {
			// If the current 'low' is in the first quarter [0, 0.25),
			// the first definitive bit is 0.
			outputBit(0);

			// All previously deferred bits must be 1s to compensate for middle-half shifts
			// that occurred in the lower part of the middle range.
			outputPendingBitsAs(1);
		} else {
			// If the current 'low' is in the upper three quarters [0.25, 1.0),
			// the first definitive bit is 1.
			outputBit(1);

			// All previously deferred bits must be 0s to compensate for middle-half shifts
			// that occurred in the upper part of the middle range.
			outputPendingBitsAs(0);
		}
	}
}

// Decode message bits given encoded bits.
// outputBitArray should be pre-sized to the expected decoded message length.
void Decode(BitArray& inputBitArray,
			BitArray& outputBitArray,
			double probabilityOf1) {

	// Input bit array length
	auto inputBitLength = inputBitArray.BitLength();

	// Output bit array length
	auto outputBitLength = outputBitArray.BitLength();

	// Probability of 0 symbol
	double probabilityOf0 = 1.0 - probabilityOf1;

	// Probability of 0 symbol fixed-point multiplier
	uint64_t probabilityOf0FixedPointMultiplier = ComputeFixedPointMultiplierFor(probabilityOf0);

	// Current interval
	uint64_t low = lowest;
	uint64_t high = highest;

	// Current value derived from the input bits
	uint64_t value = lowest;

	// Read and write positions
	int64_t readPosition = 0;
	int64_t writePosition = 0;

	// Outputs a bit
	auto outputBit = [&](uint8_t bit) { outputBitArray.WriteBitAt(writePosition++, bit); };

	// Initialize value
	{
		// Determine initial bit count
		auto initialBitCount = inputBitLength >= totalRangeBitWidth ? totalRangeBitWidth : inputBitLength;

		// Fill value with initial bits
		while (readPosition < initialBitCount) {
			value *= 2;
			value |= inputBitArray.ReadBitAt(readPosition++);
		}

		// Pad with zeros if encoded bit count is smaller than precision bit count
		value = value << (totalRangeBitWidth - initialBitCount);
	}

	// Decode the specified number of bits
	while (writePosition < outputBitLength) {
		// Narrow current interval
		{
			// Calculate the boundary between symbols 0 and 1 within the current interval
			// This is the point where the sub-interval for 0 ends and 1 begins
			uint64_t intervalLength = high - low;
			uint64_t lowerSubintervalLength = (intervalLength * probabilityOf0FixedPointMultiplier) >> totalRangeBitWidth;
			uint64_t boundary = low + lowerSubintervalLength;

			// Determine the symbol based on where 'value' falls
			if (value < boundary) {
				outputBit(0);  // Output a 0 bit

				high = boundary;  // New interval is [low, boundary)
			} else {
				outputBit(1);  // Output a 1 bit

				low = boundary;	 // New interval is [boundary, high)
			}
		}

		// Normalize (mirroring the encoder's logic)
		// This keeps 'low', 'high', and 'value' synchronized with the encoder's state.
		while (true) {
			if (high < halfRange) {	// Interval is in the lower half [0, 0.5)
				// Scale up interval
				low *= 2;
				high *= 2;

				// Scale up value
				value *= 2;
			} else if (low >= halfRange) {  // Interval is in the upper half [0.5, 1)
				// Shift and scale up interval
				low = (low - halfRange) * 2;
				high = (high - halfRange) * 2;

				// Shift and scale up value
				value = (value - halfRange) * 2;
			} else if (low >= quarterRange &&
					   high < threeQuartersRange) {	// Interval is in the middle half [0.25, 0.75)
				// Shift and scale up interval
				low = (low - quarterRange) * 2;
				high = (high - quarterRange) * 2;

				// Shift and scale up value
				value = (value - quarterRange) * 2;
			} else {
				// Can't normalize yet
				break;
			}

			// Read next bit into value's least significant bit
			//
			// Value's least significant bit must be 0, since value was multiplied by two
			// in all branches of the conditional, effectively being shifted left by one bit
			if (readPosition < inputBitLength) {
				value |= inputBitArray.ReadBitAt(readPosition++);
			}
		}
	}
}

}
