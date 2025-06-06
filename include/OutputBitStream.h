#pragma once

#include <vector>

class OutputBitStream {
   private:
	std::vector<uint8_t> bytes;
	int64_t bitLength = 0;

   public:
	OutputBitStream(int64_t initialBitCapacity) {
		bytes.reserve((initialBitCapacity + 7) / 8);
	}

	OutputBitStream(std::vector<uint8_t> bytes) : bytes(bytes) {
		// Ensure the given vector is empty
		bytes.clear();
	}

	inline void WriteBit(uint8_t bit) {
		auto byteIndex = bitLength / 8;
		auto bitIndexInByte = bitLength % 8;

		if (byteIndex == bytes.size()) {
			bytes.push_back(0);
		}

		bytes[byteIndex] |= bit << bitIndexInByte;

		bitLength += 1;
	}

	int64_t BitLength() { return bitLength; }

	int64_t ByteLength() { return bytes.size(); }

	uint8_t* Data() { return bytes.data(); }
};
