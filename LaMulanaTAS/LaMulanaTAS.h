#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
	void __fastcall TASInit(char *base);
	SHORT WINAPI TASGetKeyState(_In_ int nVirtKey);
	DWORD WINAPI TASOnFrame(void);
	void TASRender(void);
#ifdef __cplusplus
}
#endif

extern HMODULE tasModule;