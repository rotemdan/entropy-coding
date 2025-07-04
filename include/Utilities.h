#pragma once

#include <cstdint>

namespace EntropyCodingUtilities {

template <typename T>
T clip(T num, T min, T max) {
	if (num < min) {
		return min;
	}

	if (num > max) {
		return max;
	}

	return num;
}

}  // namespace EntropyCodingUtilities
