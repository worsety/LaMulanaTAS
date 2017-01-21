#pragma once
#include <string>
#include <vector>
#include <sal.h>

std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...);
std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...);