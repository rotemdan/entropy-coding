#pragma once

#include "BitArray.h"
#include "OutputBitStream.h"
#include "Utilities.h"

struct StateAndSymbol {
    uint64_t state;
    uint8_t symbol;
};

// Binary Range Asymmetric Numeral Systems rANS encoder and decoder
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
		this->probabilityOf0 = 1.0 - probabilityOf1;
		this->probabilityOf1 = probabilityOf1;

		// Total size of frequency space, in bits. Common numbers are 8, 16, 20, 24 bits.
		this->totalFrequencySpaceBits = totalFrequencySpaceBits;

		// Total frequency of all symbols
		this->totalFrequency = 1ULL << totalFrequencySpaceBits;

		// Compute frequency of symbol 0
		auto frequencyOf0 = uint64_t(round(probabilityOf0 * totalFrequency));

		// Ensure frequencies are at least 1
		frequencyOf0 = clip(frequencyOf0, 1ULL, totalFrequency - 1);

		// Lookup table for frequencies of symbols
		frequencyOf[0] = frequencyOf0;
		frequencyOf[1] = totalFrequency - frequencyOf0;

		// Lookup table for cumulative frequencies of symbols
		cumulativeFrequencyOf[0] = 0;
		cumulativeFrequencyOf[1] = frequencyOf0;
	}

	uint64_t Encode(BitArray* inputBitArray, std::vector<uint8_t>* outputBytes) {
		uint64_t state = totalFrequency;

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
			//state = LookupEncoderStateTransitionFor(state, symbol);
		}

		// Reverse flushed bytes so the decoder can read them in forward order,
		// to correctly recreate the states seen during encoding, in reverse order.
		std::reverse(outputBytes->begin(), outputBytes->end());

		// Return the final state
		return state;
	}

	void Decode(uint8_t* encodedBytes,
				uint64_t encodedByteLength,
				uint64_t state,
				BitArray* outputBitArray) {
		auto outputBitLength = outputBitArray->BitLength();

		uint64_t readPosition = 0;

		for (uint64_t writePosition = 0; writePosition < outputBitLength; writePosition++) {
			// If state reached the threshold, read a byte (aka "unflush") into the state.
			//
			// The threshold is the total frequency of all symbols.
			while (state < totalFrequency && readPosition < encodedByteLength) {
				state = (state << 8) | uint64_t(encodedBytes[readPosition++]);
			}

			// Compute the state transition
			auto stateTransitionResult = ComputeDecoderStateTransitionFor(state);
			//auto stateTransitionResult = LookupDecoderStateTransitionFor(state);

			// Transition to the new state
			state = stateTransitionResult.state;

			// Output the decoded symbol
			outputBitArray->WriteBit(stateTransitionResult.symbol, writePosition);
		}
	}

	inline uint64_t ComputeEncoderStateTransitionFor(uint64_t state, uint8_t symbol) {
		// Get symbol frequency and cumulative frequency
		uint64_t symbolFrequency = frequencyOf[symbol];
		uint64_t symbolCumulativeFrequency = cumulativeFrequencyOf[symbol];

		// Compute quotient and remainder based on the state and frequency of the symbol
		uint64_t quotient = state / symbolFrequency;
		uint64_t remainder = state % symbolFrequency;

		// Compute the new state
		uint64_t newState = (totalFrequency * quotient) + symbolCumulativeFrequency + remainder;

		return newState;
	}

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

	// Looks up encoder transition in its table
	// Doesn't check if the table is null
	inline uint64_t LookupEncoderStateTransitionFor(uint64_t state, uint8_t symbol) {
		return encoderStateTransitionTable.at((state * 2) + symbol);
	}

	// Looks up decoder transition in its table
	// Doesn't check if the table is null
	inline StateAndSymbol LookupDecoderStateTransitionFor(uint64_t state) {
		return decoderStateTransitionTable.at(state);
	}

	// Generates the encoder's state transition table (optional)
	void GenerateEncoderStateTransitionTable() {
		if (encoderStateTransitionTable.size() > 0) {
			return;
		}

		auto stateCount = totalFrequency * 256;

		encoderStateTransitionTable.reserve(stateCount * 2);

		for (int stateValue = 0; stateValue < stateCount; stateValue++) {
			auto followingStateFor0 = ComputeEncoderStateTransitionFor(stateValue, 0);
			auto followingStateFor1 = ComputeEncoderStateTransitionFor(stateValue, 1);

			encoderStateTransitionTable.push_back(followingStateFor0);
			encoderStateTransitionTable.push_back(followingStateFor1);
		}
	}

	// Generates the decoder's state transition table (optional)
	void GenerateDecoderStateTransitionTable() {
		if (decoderStateTransitionTable.size() > 0) {
			return;
		}

		auto stateCount = totalFrequency * 256;

		decoderStateTransitionTable.reserve(stateCount);

		for (int stateValue = 0; stateValue < stateCount; stateValue++) {
			auto followingStateAndSymbol = ComputeDecoderStateTransitionFor(stateValue);

			decoderStateTransitionTable.push_back(followingStateAndSymbol);
		}
	}
};
