// This is more documentation than anything else
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
	void __stdcall TasInit(int);
	SHORT __stdcall TasGetKeyState(int nVirtKey);
	DWORD __stdcall TasIncFrame(void);
	int __stdcall TasRender(void);
	DWORD __stdcall TasTime(void);
	void TasSleep(int);
#ifdef __cplusplus
}
#endif
