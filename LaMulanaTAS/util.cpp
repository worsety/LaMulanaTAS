#include "util.h"
#include <cstdarg>
#include <cwchar>

#if _MSC_VER < 1900
#error Visual C++ 2015 required for compliant vsnprintf
#endif
std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...)
{
	std::vector<char> buf(64);
	std::va_list v;
	va_start(v, fmt);
	int len = std::vsnprintf(&buf[0], buf.size() - 1, fmt, v);
	va_end(v);
	if (len + 1 >(int)buf.size())
	{
		buf.resize(len + 1);
		va_start(v, fmt);
		len = std::vsnprintf(&buf[0], buf.size(), fmt, v);
		va_end(v);
	}
	if (len < 0)
		throw std::runtime_error("vsnprintf failed");
	return std::string(&buf[0], len);
}

std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...)
{
	std::vector<wchar_t> buf(64);
	std::va_list v;
	va_start(v, fmt);
	int len = std::vswprintf(&buf[0], buf.size() - 1, fmt, v);
	va_end(v);
	if (len + 1 >(int)buf.size())
	{
		buf.resize(len + 1);
		va_start(v, fmt);
		len = std::vswprintf(&buf[0], buf.size(), fmt, v);
		va_end(v);
	}
	if (len < 0)
		throw std::runtime_error("vsnprintf failed");
	return std::wstring(&buf[0], len);
}