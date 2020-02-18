// This is more documentation than anything else
#pragma once

#include <windows.h>
#include "xinput.h"

#ifdef __cplusplus
extern "C" {
#endif
    void __stdcall TasInit(int);
    SHORT __stdcall TasGetKeyState(int nVirtKey);
    DWORD __stdcall TasIncFrame(void);
    int __stdcall TasRender(void);
    DWORD __stdcall TasTime(void);
    DWORD __stdcall TasXInputGetState(DWORD idx, XINPUT_STATE *state);
    void TasSleep(int);
#ifdef __cplusplus
}
#endif
