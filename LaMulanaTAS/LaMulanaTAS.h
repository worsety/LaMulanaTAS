// This is more documentation than anything else
#pragma once

#include "Memory.h"
#include <map>
#include <unordered_set>
#include <functional>
#include <windows.h>
#include "xinput.h"

enum
{
    PAD_START,
    PAD_SELECT,
    PAD_L3,
    PAD_R3,
    PAD_L1,
    PAD_R1,
    PAD_A,
    PAD_B,
    PAD_X,
    PAD_Y,
    PAD_L2,
    PAD_R2,
    PAD_UP = 0x80,
    PAD_DOWN,
    PAD_LEFT,
    PAD_RIGHT,
};

static const WORD XINPUT_BTN[] = {
    XINPUT_GAMEPAD_START,
    XINPUT_GAMEPAD_BACK,
    XINPUT_GAMEPAD_LEFT_THUMB,
    XINPUT_GAMEPAD_RIGHT_THUMB,
    XINPUT_GAMEPAD_LEFT_SHOULDER,
    XINPUT_GAMEPAD_RIGHT_SHOULDER,
    XINPUT_GAMEPAD_A,
    XINPUT_GAMEPAD_B,
    XINPUT_GAMEPAD_X,
    XINPUT_GAMEPAD_Y,
};

static const WORD XINPUT_DPAD[] = {
    XINPUT_GAMEPAD_DPAD_UP,
    XINPUT_GAMEPAD_DPAD_DOWN,
    XINPUT_GAMEPAD_DPAD_LEFT,
    XINPUT_GAMEPAD_DPAD_RIGHT,
};

// 78x32+4 cells of 8x12
#define OVERLAY_LEFT 8
#define OVERLAY_RIGHT 632
#define OVERLAY_TOP 42
#define MAIN_OVERLAY_TOP 426
#define OVERLAY_BOTTOM 474

class Overlay;

class TAS
{
public:
    LaMulanaMemory memory;
    int frame, frame_count, rngsteps;
    std::map<int, std::string> sections;
    std::map<int, std::unordered_set<unsigned char*>> frame_keys;
    std::map<int, std::unordered_set<unsigned char*>> frame_btns;
    std::map<int, std::list<std::function<void()>>> frame_actions;
    std::unordered_set<unsigned char> cur_frame_keys;
    std::unordered_set<unsigned char> cur_frame_btns;
    std::vector<LaMulanaMemory::objfixup> objfixups;
    short currng = -1;
    using input_iter = std::map<int, std::unordered_set<unsigned char*>>::iterator;
    std::map<std::string, unsigned char*> name2vk, name2btn;
    IDirect3DDevice9 *curdev;
    std::unique_ptr<BitmapFont> font4x6, font8x12;
    struct {
        CComPtr<IDirect3DTexture9> tex;
        float texel_w, texel_h;
    } hit_parts, hit_hex;
    CComPtr<IDirect3DTexture9> overlay;
    CComPtr<IDirect3DSurface9> overlay_surf;
    int repeat_delay, repeat_speed;
    LARGE_INTEGER start_time, cur_time, timer_freq;

    struct keystate {
        union {
            struct {
                unsigned char held : 1;
                unsigned char pressed : 1;
                unsigned char released : 1;
                unsigned char repeat : 1;
                unsigned char passthrough : 1;
            };
            unsigned char bits;
        };
        unsigned char repeat_counter;
    } keys[256];

    void UpdateKeys()
    {
        bool active = !!memory.kb_enabled;
        for (int vk = 0; vk < 256; vk++)
        {
            bool held = active && GetAsyncKeyState(vk) < 0;
            keys[vk].pressed = held && !keys[vk].held;
            keys[vk].released = !held && keys[vk].held;
            keys[vk].held = keys[vk].passthrough = held;
            if (keys[vk].pressed)
            {
                keys[vk].repeat = true;
                keys[vk].repeat_counter = repeat_delay;
            }
            else if (held && --keys[vk].repeat_counter == 0)
            {
                keys[vk].repeat = true;
                keys[vk].repeat_counter = repeat_speed;
            }
            else
            {
                keys[vk].repeat = false;
            }
        }
        keys[VK_SHIFT].passthrough = false;
        keys[VK_LSHIFT].passthrough = false;
        keys[VK_RSHIFT].passthrough = false;
    }

    enum POLLTYPE { POLL_HELD, POLL_PRESSED, POLL_RELEASED, POLL_REPEAT };
    bool Poll(int vk, bool mod = false, POLLTYPE type = POLL_PRESSED)
    {
        if (mod != !!keys[VK_SHIFT].held)
            return false;
        keys[vk].passthrough = false;
        return !!(keys[vk].bits & 1 << type);
    }

    bool initialised, run = true, pause, resetting, has_reset, ff;
    bool scripting = true, passthrough = true;
    bool show_overlay = true, show_exits, show_solids, show_loc, hide_game;
    int show_tiles;
    unsigned show_hitboxes = 1 << 7 | 1 << 9 | 1 << 11;  // unknown types, I want to know if anyone sees them
    Overlay *shopping_overlay, *object_viewer, *rng_overlay;
    Overlay *extra_overlay;

    TAS(char *base);
    void LoadBindings();
    bool KeyPressed(int vk);
    DWORD GetXInput(DWORD idx, XINPUT_STATE *state);
    void IncFrame();
    void Overlay();
    void DrawOverlay();
    void ProcessKeys();

    void LoadTAS();

    int LagFrames();
};

class Overlay
{
public:
    TAS &tas;
    LaMulanaMemory &memory;
    Overlay(TAS &tas) : tas(tas), memory(tas.memory) {}
    virtual bool ProcessKeys() { return false; } // return true to inhibit the main overlay's processing
    virtual void Draw() {};
};

class ShoppingOverlay : public Overlay
{
public:
    ShoppingOverlay(TAS &tas) : Overlay(tas) {}
    void Draw() override;
};

class ObjectViewer : public Overlay
{
public:
    enum
    {
        MODE_MIN = -1,
        STACK = -1,
        MODE_MAX = 59,
    };
    int mode = STACK;
    int scroll = 0;
    LaMulanaMemory::object *obj = nullptr;
    ObjectViewer(TAS &tas) : Overlay(tas) {}
    bool ProcessKeys() override;
    void Draw() override;
};

struct RNGOverlay : public Overlay
{
    struct Result : public std::vector<int>
    {
        operator std::string();
    };
    struct Condition
    {
        const char *name;
        std::function<bool(const Result&)> test;
    };
    struct Mode
    {
        const char *name;
        std::function<Result(short)> roll;
        std::vector<Condition> conditions;
    };
    std::vector<Mode> modes;
    int mode = 0;
    int condition = 0;
    RNGOverlay(TAS &tas);
    bool ProcessKeys() override;
    void Draw() override;
};

extern HMODULE tasModule;

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
