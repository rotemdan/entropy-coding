#pragma once

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
