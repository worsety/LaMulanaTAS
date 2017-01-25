#pragma once
#include <string>
#include <vector>
#include <sal.h>
#include <memory>
#include <exception>
#include <d3d9.h>
#include <atlbase.h>

std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...);
std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...);

template<typename T> class vararray
{
public:
	T* ptr;
	size_t count;
	vararray(T* ptr, size_t count) : ptr(ptr), count(count) {}
	T* begin() {
		return ptr;
	}
	T* end() {
		return ptr + count;
	}
};
