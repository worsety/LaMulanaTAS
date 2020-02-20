#include "LaMulanaTAS.h"

TAS *tas = nullptr;

void __stdcall TasInit(int patchver)
{
    if (patchver != 3)
    {
        MessageBox(nullptr, L"DLL and EXE versions do not match\nPlease repatch your EXE.", nullptr, MB_OK);
        ExitProcess(1);
    }
    tas = new TAS((char*)GetModuleHandle(nullptr) - 0x400000);
}

SHORT __stdcall TasGetKeyState(int nVirtKey)
{
    return tas->KeyPressed(nVirtKey) ? 0x8000 : 0;
}

DWORD __stdcall TasXInputGetState(DWORD idx, XINPUT_STATE *state)
{
    return tas->GetXInput(idx, state);
}

DWORD __stdcall TasIncFrame(void)
{
    tas->IncFrame();
    return timeGetTime();
}

int __stdcall TasRender(void)
{
    tas->Overlay();
    return (int)tas->memory.game_horz_offset;
}

DWORD __stdcall TasTime(void)
{
    return tas->frame_count * 17;
}

void TasSleep(int duration)
{
    if (!tas->ff)
        (*tas->memory.sleep)(duration);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    tasModule = hModule;
    char *rva0 = (char*)GetModuleHandle(nullptr) - 0x400000;
    *(void**)(rva0 + 0xdb9060) = TasInit;
    *(void**)(rva0 + 0xdb9064) = TasGetKeyState;
    *(void**)(rva0 + 0xdb9068) = TasIncFrame;
    *(void**)(rva0 + 0xdb906c) = TasRender;
    *(void**)(rva0 + 0xdb9070) = TasTime;
    *(void**)(rva0 + 0xdb9074) = TasSleep;
    *(void**)(rva0 + 0xdb9078) = TasXInputGetState;
    return TRUE;
}
