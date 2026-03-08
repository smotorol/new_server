#pragma once
#include <cstdio>

template <size_t N>
inline void copy_cstr(char(&dst)[N], const char* src)
{
	if (!src) {
		dst[0] = '\0';
		return;
	}
	std::snprintf(dst, N, "%s", src);
}
