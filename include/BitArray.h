#pragma once

#include <cstdint>

class BitArray {
   private:
	uint8_t* bytes;
	uint64_t bitLength;

   public:
	BitArray(uint8_t* bytes, uint64_t bitLength)
		: bytes(bytes), bitLength(bitLength) {}

	uint8_t ReadBitAt(uint64_t bitReadPosition) {
		auto byteIndex = bitReadPosition / 8;
		auto bitIndexInByte = bitReadPosition % 8;

		uint8_t bit = (bytes[byteIndex] >> bitIndexInByte) & 1;

		return bit;
	}

	void WriteBit(uint8_t bitValue, uint64_t bitWritePosition) {
		auto byteIndex = bitWritePosition / 8;
		auto bitIndexInByte = bitWritePosition % 8;

		bytes[byteIndex] |= bitValue << bitIndexInByte;
	}

	uint64_t BitLength() { return bitLength; }

	uint64_t ByteLength() { return (bitLength + 7) / 8; }

	uint8_t* Data() { return bytes; }
};
