#pragma once

#include "BitArray.h"
#include "OutputBitStream.h"
#include "Utilities.h"
#include "FastUint31Division.h"

#include <exception>

using namespace EntropyCodingUtilities;

struct StateAndSymbol {
    uint32_t state;
    uint8_t symbol;
};

// Range Asymmetric Numeral Systems (rANS) encoder and decoder for a binary alphabet (0 and 1),
// with optional support for table-based processing (tANS).
class BinaryRangeANSCoder {
   private:
	uint32_t totalRangeBitWidth;
	uint32_t totalFrequency;

	uint32_t frequencyOf[2];
	uint32_t cumulativeFrequencyOf[2];
	uint32_t encoderFlushThresholdOf[2];
	FastUint31Division fastDivisionForFrequencyOf[2];

	std::vector<uint32_t> encoderStateTransitionTable;
	std::vector<StateAndSymbol> decoderStateTransitionTable;

   public:
	BinaryRangeANSCoder(double probabilityOf1, uint8_t totalRangeBitWidth) {
		if (probabilityOf1 < 0.0 || probabilityOf1 > 1.0) {
			throw std::exception("Probability of 1 must be between 0.0 and 1.0.");
		}

		if (totalRangeBitWidth < 2 || totalRangeBitWidth > 23) {
			throw std::exception("Total range bit width must be between 2 and 23 (inclusive).");
		}

		// Probability of symbol 0
		double probabilityOf0 = 1.0 - probabilityOf1;

		// Total size of the integer range, in bits.
		// Determines how "quantized" the symbol probabilities would be.
		// Recommended widths are between 6 and 20 bits.
		//
		// Maximum supported value is 23, since it implies the state would use up to 31 bits of
		// unsigned integer range (maximum supported by fast division).
		//
		// Larger range means more expensive table construction, and larger table memory size.
		// Table size is 256 times larger than the range, or 8 bits more.
		//
		// If you intend to use table-base encoding / decoding methods,
		// try to use a smaller range size, like 6 - 12 bits.
		this->totalRangeBitWidth = totalRangeBitWidth;

		// Total frequency of all symbols
		this->totalFrequency = 1u << totalRangeBitWidth;

		// Compute frequency of symbol 0
		auto frequencyOf0 = uint32_t(round(probabilityOf0 * totalFrequency));

		// Ensure frequencies are at least 1
		frequencyOf0 = clip(frequencyOf0, 1u, totalFrequency - 1);

		// Lookup table for frequencies of symbols
		frequencyOf[0] = frequencyOf0;
		frequencyOf[1] = totalFrequency - frequencyOf0;

		// Lookup table for cumulative frequencies of symbols
		cumulativeFrequencyOf[0] = 0;
		cumulativeFrequencyOf[1] = frequencyOf0;

		// Lookup table for encoder flush threshold of symbols
		encoderFlushThresholdOf[0] = frequencyOf[0] * 256;
		encoderFlushThresholdOf[1] = frequencyOf[1] * 256;

		// Lookup table for fast division object for the symbol frequencies
		fastDivisionForFrequencyOf[0] = FastUint31Division(frequencyOf[0]);
		fastDivisionForFrequencyOf[1] = FastUint31Division(frequencyOf[1]);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Encoding and decoding methods (non table-based).
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Encode message bits
	uint32_t Encode(BitArray& inputBitArray, std::vector<uint8_t>& outputBytes) {
		uint32_t state = totalFrequency;

		// Iterate message bits in reverse order
		for (int64_t readPosition = inputBitArray.BitLength() - 1; readPosition >= 0; readPosition--) {
			// Take message bit
			auto symbol = inputBitArray.ReadBitAt(readPosition);

			// While the threshold is reached, flush the lowest byte of the state.
			//
			// The threshold is the symbol's frequency, times 256.
			//
			// The goal here is to ensure that the decoder is able to correctly recreate
			// the same sequence of states, in reverse order. To achieve that, it needs to reliably
			// determine when the encoder flushed a byte, so it can read an encoded byte at the
			// right time.
			//
			// The key is to ensure that after flushing, the state would be smaller than the total
			// frequency. The decoder (reading in reverse) will then be able to recognize that in
			// its threshold, and trigger its "unflush" operation.
			auto flushThreshold = encoderFlushThresholdOf[symbol];

			while (state >= flushThreshold) {
				outputBytes.push_back(state & 255);
				state >>= 8;
			}

			// Compute the state transition and transition to the new state
			state = ComputeEncoderStateTransitionFor(state, symbol);
		}

		// Reverse flushed bytes so the decoder can read them in forward order,
		// to correctly recreate the states seen during encoding, in reverse order.
		std::reverse(outputBytes.begin(), outputBytes.end());

		// Return the final state.
		//
		// The final state is guaranteed to be in the range [0, totalFrequency * 256).
		// So, for a range of 8 bits, it will fit 16 bits.
		// For range of 16 bits, it will fit 24 bits.
		// For 24 bits, it will fit 32 bits (maximum supported).
		//
		// For now, I don't serialize the state to bytes, because there are many
		// ways to do so. For example, using plain fixed-length byte encodings,
		// variable-length encodings, etc.
		//
		// Every range size would have a different serialization method that would be optimal
		// for it, so it makes it difficult to find a one-fits-all solution.
		return state;
	}

	// Decode bits given encoded bytes and state.
	// outputBitArray should be pre-sized to the expected decoded message length.
	void Decode(uint8_t* encodedBytes,
				int64_t encodedByteLength,
				uint32_t state,
				BitArray& outputBitArray) {

		auto outputBitLength = outputBitArray.BitLength();

		int64_t readPosition = 0;

		for (int64_t writePosition = 0; writePosition < outputBitLength; writePosition++) {
			// While state is smaller than the threshold, read a byte (aka "unflush") into the state.
			//
			// The threshold is the total frequency of all symbols.
			while (state < totalFrequency && readPosition < encodedByteLength) {
				state = (state << 8) | uint32_t(encodedBytes[readPosition++]);
			}

			// Compute the state transition
			auto stateTransitionResult = ComputeDecoderStateTransitionFor(state);

			// Transition to the new state
			state = stateTransitionResult.state;

			// Output the decoded symbol
			outputBitArray.WriteBitAt(writePosition, stateTransitionResult.symbol);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Table-based encoding and decoding methods.
	//
	// Separated to ensure methods get optimized correctly by the compiler.
	//
	// Although the code duplication is not very desirable, attempts to merge
	// table-based and non table-based methods seemed to have significantly
	// degraded performance.
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Encode bits using table. Requires encoder state transition table to be built first.
	uint32_t EncodeUsingTable(BitArray& inputBitArray, std::vector<uint8_t>& outputBytes) {
		if (!HasEncoderStateTransitionTable()) {
			throw std::exception("Encoder state transition table has not been built.");
		}

		uint32_t state = totalFrequency;

		for (int64_t readPosition = inputBitArray.BitLength() - 1; readPosition >= 0; readPosition--) {
			auto symbol = inputBitArray.ReadBitAt(readPosition);

			auto flushThreshold = encoderFlushThresholdOf[symbol];

			while (state >= flushThreshold) {
				outputBytes.push_back(state & 255);
				state >>= 8;
			}

			state = LookupEncoderStateTransitionFor(state, symbol);
		}

		std::reverse(outputBytes.begin(), outputBytes.end());

		return state;
	}

	// Decode using table. Requires decoder state transition table to be built first.
	void DecodeUsingTable(uint8_t* encodedBytes,
						  int64_t encodedByteLength,
						  uint32_t state,
						  BitArray& outputBitArray) {

		if (!HasDecoderStateTransitionTable()) {
			throw std::exception("Decoder state transition table has not been built.");
		}

		int64_t outputBitLength = outputBitArray.BitLength();

		int64_t readPosition = 0;

		for (int64_t writePosition = 0; writePosition < outputBitLength; writePosition++) {
			while (state < totalFrequency && readPosition < encodedByteLength) {
				state = (state << 8) | uint32_t(encodedBytes[readPosition++]);
			}

			auto stateTransitionResult = LookupDecoderStateTransitionFor(state);

			state = stateTransitionResult.state;

			outputBitArray.WriteBitAt(writePosition, stateTransitionResult.symbol);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// State transition computation methods
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Given a starting state and symbol, compute the next encoder state
	inline uint32_t ComputeEncoderStateTransitionFor(uint32_t state, uint8_t symbol) {
		// Compute quotient and remainder based on the state and frequency of the symbol
		//
		// Slow version:
		//uint32_t quotient = state / frequencyOf[symbol];
		//uint32_t remainder = state % frequencyOf[symbol];
		//
		// Fast version,
		// Uses fast division based on a single 64-bit multiplication and a single right shift:
		auto divisionResult = fastDivisionForFrequencyOf[symbol].DivideAndGetRemainder(state);
		uint32_t quotient = divisionResult.quotient;
		uint32_t remainder = divisionResult.remainder;

		// Compute the new state
		uint32_t newState = (totalFrequency * quotient) + cumulativeFrequencyOf[symbol] + remainder;

		return newState;
	}

	// Given a starting state, compute the next decoder state and the emitted symbol
	inline StateAndSymbol ComputeDecoderStateTransitionFor(uint32_t state) {
		// Compute quotient and remainder based on the state and total frequency.
		//
		// Slow version:
		//uint32_t quotient = state / totalFrequency;
		//uint32_t remainder = state % totalFrequency;
		//
		// Fast version, optimized with bitwise operations since totalFrequency
		// is guaranteed to be a power of two:
		uint32_t quotient = state >> totalRangeBitWidth;
		uint32_t remainder = state & (totalFrequency - 1);

		// Find the decoded symbol based on the remainder
		uint8_t decodedSymbol = remainder >= cumulativeFrequencyOf[1];

		// Compute the new state
		uint32_t newState = (frequencyOf[decodedSymbol] * quotient) - cumulativeFrequencyOf[decodedSymbol] + remainder;

		return { newState, decodedSymbol };
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Table construction and lookup methods
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Looks up encoder transition in the table.
	// Doesn't check if the table is empty or if arguments are out of range.
	inline uint32_t LookupEncoderStateTransitionFor(uint32_t state, uint8_t symbol) {
		return encoderStateTransitionTable[(uint64_t(state) * 2) + symbol];
	}

	// Looks up decoder transition in the table.
	// Doesn't check if the table is empty or if arguments are out of range.
	inline StateAndSymbol LookupDecoderStateTransitionFor(uint32_t state) {
		return decoderStateTransitionTable[state];
	}

	// Builds the encoder's state transition table
	// (optional, needs to be explicitly called to enable table-based encoding)
	void BuildEncoderStateTransitionTable() {
		if (HasEncoderStateTransitionTable()) {
			return;
		}

		// The size of the encoder table is the total frequency times 256.
		// A state value cannot be greater than or equal to this value.
		auto stateCount = uint64_t(totalFrequency) * 256;

		// Reserve memory
		encoderStateTransitionTable.reserve(stateCount * 2);

		// Append two consecutive table entries for each state, one for symbol 0 and other for symbol 1
		for (uint32_t stateValue = 0; stateValue < stateCount; stateValue++) {
			auto followingStateFor0 = ComputeEncoderStateTransitionFor(stateValue, 0);
			auto followingStateFor1 = ComputeEncoderStateTransitionFor(stateValue, 1);

			encoderStateTransitionTable.push_back(followingStateFor0);
			encoderStateTransitionTable.push_back(followingStateFor1);
		}
	}

	// Build the decoder's state transition table
	// (optional, needs to be explicitly called to enable table-based decoding)
	void BuildDecoderStateTransitionTable() {
		if (HasDecoderStateTransitionTable()) {
			return;
		}

		// The size of the decoder table is the total frequency times 256.
		// A state value cannot be equal to or greater than this value.
		auto stateCount = uint64_t(totalFrequency) * 256;

		// Reserve memory
		decoderStateTransitionTable.reserve(stateCount);

		// Append a single table entry for each state
		for (uint32_t stateValue = 0; stateValue < stateCount; stateValue++) {
			auto followingStateAndSymbol = ComputeDecoderStateTransitionFor(stateValue);

			decoderStateTransitionTable.push_back(followingStateAndSymbol);
		}
	}

	// Has an encoder state transition table been built?
	bool HasEncoderStateTransitionTable() { return encoderStateTransitionTable.size() > 0; }

	// Has a decoder state transition table been built?
	bool HasDecoderStateTransitionTable() { return decoderStateTransitionTable.size() > 0; }

	// Computes the total memory size, in bytes, required by an encoder state transition table
	uint64_t GetEncoderStateTransitionTableMemorySize() { return uint64_t(totalFrequency) * 256 * sizeof(uint32_t) * 2; }

	// Computes the total memory size, in bytes, required by a decoder state transition table
	uint64_t GetDecoderStateTransitionTableMemorySize() { return uint64_t(totalFrequency) * 256 * sizeof(StateAndSymbol); }
};
