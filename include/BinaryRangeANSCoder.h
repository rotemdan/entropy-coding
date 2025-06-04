#pragma once

#include "BitArray.h"
#include "OutputBitStream.h"
#include "Utilities.h"

#include <cassert>

struct StateAndSymbol {
    uint64_t state;
    uint8_t symbol;
};

// Binary Range Asymmetric Numeral Systems (rANS) encoder and decoder
class BinaryRangeANSCoder {
   private:
	double probabilityOf0;
	double probabilityOf1;

	uint64_t totalFrequencySpaceBits;
	uint64_t totalFrequency;

	uint64_t frequencyOf[2];
	uint64_t cumulativeFrequencyOf[2];

	std::vector<uint64_t> encoderStateTransitionTable;
	std::vector<StateAndSymbol> decoderStateTransitionTable;

   public:
	BinaryRangeANSCoder(double probabilityOf1, int totalFrequencySpaceBits) {
		// Probabilities of 0 and 1 symbols
		this->probabilityOf0 = 1.0 - probabilityOf1;
		this->probabilityOf1 = probabilityOf1;

		// Total size of frequency space, in bits.
		// Determines how "quantized" the symbol probabilities would be.
		// Common values are 8, 12, 16, 20, 24 bits.
		//
		// Larger space means more expensive table construction.
		//
		// If you intend to use table-base encoding / decoding methods,
		// try to use a smaller space, like 8 - 12 bits.
		this->totalFrequencySpaceBits = totalFrequencySpaceBits;

		// Total frequency of all symbols
		this->totalFrequency = 1ULL << totalFrequencySpaceBits;

		// Compute frequency of symbol 0
		auto frequencyOf0 = uint64_t(round(probabilityOf0 * totalFrequency));

		// Ensure frequencies are at least 1
		frequencyOf0 = EntropyCodingUtilities::clip(frequencyOf0, 1ULL, totalFrequency - 1);

		// Lookup table for frequencies of symbols
		frequencyOf[0] = frequencyOf0;
		frequencyOf[1] = totalFrequency - frequencyOf0;

		// Lookup table for cumulative frequencies of symbols
		cumulativeFrequencyOf[0] = 0;
		cumulativeFrequencyOf[1] = frequencyOf0;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Encoding and decoding methods (non table-based).
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Encode message bits
	uint64_t Encode(BitArray* inputBitArray, std::vector<uint8_t>* outputBytes) {
		uint64_t state = totalFrequency;

		// Iterate message bits in reverse order
		for (int64_t readPosition = inputBitArray->BitLength() - 1; readPosition >= 0; readPosition--) {
			// Take message bit
			auto symbol = inputBitArray->ReadBitAt(readPosition);

			// While the threshold is reached, flush the lowest byte of the state.
			//
			// The threshold is based on the symbol's frequency, times 256.
			//
			// The goal here is to ensure that the decoder is able to correctly recreate
			// the same sequence of states, in reverse order. To achieve that, it needs to reliably
			// determine when the encoder flushed a byte, so it can read an encoded byte at the
			// right time.
			//
			// The key is to ensure that after flushing, the state would be smaller than the total
			// frequency. The decoder (reading in reverse) will then be able to recognize that in
			// its threshold, and trigger its "unflush" operation.
			while (state >= frequencyOf[symbol] * 256) {
				outputBytes->push_back(state & 255);
				state = state >> 8;
			}

			// Compute the state transition and transition to the new state
			state = ComputeEncoderStateTransitionFor(state, symbol);
		}

		// Reverse flushed bytes so the decoder can read them in forward order,
		// to correctly recreate the states seen during encoding, in reverse order.
		std::reverse(outputBytes->begin(), outputBytes->end());

		// Return the final state.
		//
		// The final state is guaranteed to be in the range [0, totalFrequency * 256).
		// So, for a total frquency space of 8 bits, it will fit 16 bits.
		// For space of 16 bits, it will fit 24 bits.
		// For 24 bits, it will fit 32 bits, etc.
		//
		// For now, I don't serialize the state to bytes, because there are many
		// ways to do so. For example, using plain fixed-length byte encodings,
		// variable-length encodings, etc.
		return state;
	}

	// Decode bits given encoded bytes and state
	void Decode(uint8_t* encodedBytes,
				uint64_t encodedByteLength,
				uint64_t state,
				BitArray* outputBitArray) {

		auto outputBitLength = outputBitArray->BitLength();

		uint64_t readPosition = 0;

		for (uint64_t writePosition = 0; writePosition < outputBitLength; writePosition++) {
			// While state is smaller han the threshold, read a byte (aka "unflush") into the state.
			//
			// The threshold is the total frequency of all symbols.
			while (state < totalFrequency && readPosition < encodedByteLength) {
				state = (state << 8) | uint64_t(encodedBytes[readPosition++]);
			}

			// Compute the state transition
			auto stateTransitionResult = ComputeDecoderStateTransitionFor(state);

			// Transition to the new state
			state = stateTransitionResult.state;

			// Output the decoded symbol
			outputBitArray->WriteBit(stateTransitionResult.symbol, writePosition);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Table based encoding and decoding methods.
	//
	// Separated to ensure methods get optimized correctly.
	//
	// Although the code duplication is not very desirable,
	// attempts to merge both methods seemed to have significantly degraded performance.
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Encode bits using table. Requires encoder state transition table to be built first.
	uint64_t EncodeUsingTable(BitArray* inputBitArray, std::vector<uint8_t>* outputBytes) {
		if (!HasEncoderStateTransitionTable()) {
			throw std::exception("Encoder state transition table has not been built.");
		}

		uint64_t state = totalFrequency;

		for (int64_t readPosition = inputBitArray->BitLength() - 1; readPosition >= 0; readPosition--) {
			auto symbol = inputBitArray->ReadBitAt(readPosition);

			while (state >= frequencyOf[symbol] * 256) {
				outputBytes->push_back(state & 255);
				state = state >> 8;
			}

			state = LookupEncoderStateTransitionFor(state, symbol);
		}

		std::reverse(outputBytes->begin(), outputBytes->end());

		return state;
	}

	// Decode using table. Requires decoder state transition table to be built first.
	void DecodeUsingTable(uint8_t* encodedBytes,
						  uint64_t encodedByteLength,
						  uint64_t state,
						  BitArray* outputBitArray) {

		if (!HasDecoderStateTransitionTable()) {
			throw std::exception("Decoder state transition table has not built.");
		}

		uint64_t outputBitLength = outputBitArray->BitLength();

		uint64_t readPosition = 0;

		for (uint64_t writePosition = 0; writePosition < outputBitLength; writePosition++) {
			while (state < totalFrequency && readPosition < encodedByteLength) {
				state = (state << 8) | uint64_t(encodedBytes[readPosition++]);
			}

			auto stateTransitionResult = LookupDecoderStateTransitionFor(state);

			state = stateTransitionResult.state;

			outputBitArray->WriteBit(stateTransitionResult.symbol, writePosition);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// State transition methods
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Given a state and symbol, compute the next encoder state
	inline uint64_t ComputeEncoderStateTransitionFor(uint64_t state, uint8_t symbol) {
		// Get symbol frequency and cumulative frequency
		uint64_t frequencyOfSymbol = frequencyOf[symbol];

		// Compute quotient and remainder based on the state and frequency of the symbol
		uint64_t quotient = state / frequencyOfSymbol;
		uint64_t remainder = state % frequencyOfSymbol;

		// Compute the new state
		uint64_t newState = (totalFrequency * quotient) + cumulativeFrequencyOf[symbol] + remainder;

		return newState;
	}

	// Given a state, compute the resulting decoder state and emitted symbol
	inline StateAndSymbol ComputeDecoderStateTransitionFor(uint64_t state) {
		// Compute quotient and remainder based on the state and total frequency.
		//
		// Optimized for bitwise operations since totalFrequency is guaranteed to be a power of two.
		uint64_t quotient = state >> totalFrequencySpaceBits;
		uint64_t remainder = state & (totalFrequency - 1);

		// Find the decoded symbol based on the remainder
		uint8_t decodedSymbol = remainder < cumulativeFrequencyOf[1] ? 0 : 1;

		// Compute the new state
		uint64_t newState = (frequencyOf[decodedSymbol] * quotient) - cumulativeFrequencyOf[decodedSymbol] + remainder;

		return { newState, decodedSymbol };
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	// Table construction and lookup methods
	/////////////////////////////////////////////////////////////////////////////////////////////////////

	// Looks up encoder transition in the table.
	// Doesn't check if the table is empty or if arguments are out of range.
	inline uint64_t LookupEncoderStateTransitionFor(uint64_t state, uint8_t symbol) {
		return encoderStateTransitionTable.at((state * 2) + symbol);
	}

	// Looks up decoder transition in the table.
	// Doesn't check if the table is empty or if arguments are out of range.
	inline StateAndSymbol LookupDecoderStateTransitionFor(uint64_t state) {
		return decoderStateTransitionTable.at(state);
	}

	// Builds the encoder's state transition table
	// (optional, needs to be explicitly called to enable table-based encoding)
	void BuildEncoderStateTransitionTable() {
		if (HasEncoderStateTransitionTable()) {
			return;
		}

		// The size of the encoder table is the total frequency times 256.
		// A state value cannot be equal to or greater than this value.
		auto stateCount = totalFrequency * 256;

		// Reserve memory
		encoderStateTransitionTable.reserve(stateCount * 2);

		// Append two consecutive table entries for each state, one for symbol 0 and other for symbol 1
		for (int stateValue = 0; stateValue < stateCount; stateValue++) {
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
		auto stateCount = totalFrequency * 256;

		decoderStateTransitionTable.reserve(stateCount);

		// Append a single table entry for each state
		for (int stateValue = 0; stateValue < stateCount; stateValue++) {
			auto followingStateAndSymbol = ComputeDecoderStateTransitionFor(stateValue);

			decoderStateTransitionTable.push_back(followingStateAndSymbol);
		}
	}

	bool HasEncoderStateTransitionTable() { return encoderStateTransitionTable.size() > 0; }
	bool HasDecoderStateTransitionTable() { return decoderStateTransitionTable.size() > 0; }
};
