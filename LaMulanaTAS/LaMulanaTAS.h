#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
	void __fastcall TASInit(char *base);
	SHORT WINAPI TASGetKeyState(_In_ int nVirtKey);
	DWORD WINAPI TASgetTime(void);
#ifdef __cplusplus
}
#endif
