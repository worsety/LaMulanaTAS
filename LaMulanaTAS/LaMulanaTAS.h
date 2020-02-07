// This is more documentation than anything else
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
	void __stdcall TasInit(int);
	SHORT _stdcall TasGetKeyState(int nVirtKey);
	DWORD _stdcall TasIncFrame(void);
	void TasRender(void);
	DWORD __stdcall TasTime(void);
	void TasSleep(int);
#ifdef __cplusplus
}
#endif
