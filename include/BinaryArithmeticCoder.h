#pragma once

#include "BitArray.h"
#include "OutputBitStream.h"

// Binary arithmetic coder, based mostly on integer arithmetic.
namespace BinaryArithmeticCoder {

// Range bit width should ensure that when the interval range (high - low) is
// converted to float64, during computation of the boundary, the resulting floating point number
// doesn't cause a loss in accuracy.
//
// A value of 53 should be safe since the maximum safe integer for float64 is 2^53 - 1
inline constexpr int64_t totalRangeBitWidth = 53;

// Range constants. Map the [0.0, 1.0) fractional range to integers.
inline constexpr uint64_t lowest = 0;
inline constexpr uint64_t highest = (1ULL << totalRangeBitWidth);

inline constexpr uint64_t quarterRange = highest / 4;
inline constexpr uint64_t halfRange = highest / 2;
inline constexpr uint64_t threeQuartersRange = highest - quarterRange;

void Encode(BitArray* inputBitArray,
			OutputBitStream* outputBitStream,
			double probabilityOf1) {
	// Input bit array length
	auto inputBitLength = inputBitArray->BitLength();

	// Probability of 0 bit
	double probabilityOf0 = 1.0 - probabilityOf1;

	// Current interval
	uint64_t low = lowest;
	uint64_t high = highest;

	// Pending bit count
	uint64_t pendingBitCount = 0;

	// Read position
	int64_t readPosition = 0;

	// Outputs a bit
	auto outputBit = [&](uint8_t bit) { outputBitStream->WriteBit(bit); };

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
			uint8_t inputBit = inputBitArray->ReadBitAt(readPosition);

			// Calculate the boundary between symbols 0 and 1 within the current interval
			// This is the point where the sub-interval for 0 ends and 1 begins
			uint64_t boundary = low + uint64_t(double(high - low) * probabilityOf0);

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

void Decode(BitArray* inputBitArray,
			BitArray* outputBitArray,
			double probabilityOf1) {

	// Input bit array length
	auto inputBitLength = inputBitArray->BitLength();

	// Probability of 0 bit
	double probabilityOf0 = 1.0 - probabilityOf1;

	// Current interval
	uint64_t low = lowest;
	uint64_t high = highest;

	// Current value derived from the input bits
	uint64_t value = lowest;

	// Read and write positions
	int64_t readPosition = 0;
	int64_t writePosition = 0;

	auto outputBitLength = outputBitArray->BitLength();

	// Outputs a bit
	auto outputBit = [&](uint8_t bit) { outputBitArray->WriteBit(bit, writePosition++); };

	// Initialize value
	{
		// Determine initial bit count
		auto initialBitCount = inputBitLength >= totalRangeBitWidth ? totalRangeBitWidth : inputBitLength;

		// Fill value with initial bits
		while (readPosition < initialBitCount) {
			value *= 2;
			value |= inputBitArray->ReadBitAt(readPosition++);
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
			uint64_t boundary = low + uint64_t(double(high - low) * probabilityOf0);

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
				value |= inputBitArray->ReadBitAt(readPosition++);
			}
		}
	}
}

}
