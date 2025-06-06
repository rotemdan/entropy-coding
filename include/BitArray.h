#pragma once

#include <cstdint>

class BitArray {
   private:
	uint8_t* bytes;
	int64_t bitLength;

   public:
	BitArray(uint8_t* bytes, int64_t bitLength)
		: bytes(bytes), bitLength(bitLength) {}

	inline uint8_t ReadBitAt(int64_t bitReadPosition) {
		auto byteIndex = bitReadPosition / 8;
		auto bitIndexInByte = bitReadPosition % 8;

		uint8_t bit = (bytes[byteIndex] >> bitIndexInByte) & 1;

		return bit;
	}

	inline void WriteBitAt(int64_t bitWritePosition, uint8_t bitValue) {
		auto byteIndex = bitWritePosition / 8;
		auto bitIndexInByte = bitWritePosition % 8;

		bytes[byteIndex] |= bitValue << bitIndexInByte;
	}

	int64_t BitLength() { return bitLength; }

	int64_t ByteLength() { return (bitLength + 7) / 8; }

	uint8_t* Data() { return bytes; }
};
