#pragma once

#include <vector>

class OutputBitStream {
   private:
	std::vector<uint8_t> bytes;
	uint64_t bitLength = 0;

   public:
	OutputBitStream(uint64_t initialBitCapacity) {
		bytes.reserve((initialBitCapacity + 7) / 8);
	}

	OutputBitStream(std::vector<uint8_t> bytes) : bytes(bytes) {}

	void WriteBit(uint8_t bit) {
		auto byteIndex = bitLength / 8;
		auto bitIndexInByte = bitLength % 8;

		if (byteIndex == bytes.size()) {
			bytes.push_back(0);
		}

		bytes[byteIndex] |= bit << bitIndexInByte;

		bitLength += 1;
	}

	uint64_t BitLength() { return bitLength; }

	uint64_t ByteLength() { return bytes.size(); }

	uint8_t* Data() { return bytes.data(); }
};
