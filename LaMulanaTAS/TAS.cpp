// LaMulanaTAS.cpp : Defines the exported functions for the DLL application.
//

#include "LaMulanaTAS.h"
#include "util.h"
#include <utility>
#include <queue>
#include <map>
#include <unordered_set>
#include <string>
#include <fstream>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <regex>
#include <functional>

#ifndef _NDEBUG
#define D3D_DEBUG_INFO
#endif
#include "xinput.h"

HMODULE tasModule;

std::vector<std::pair<int, unsigned char>> LaMulanaMemory::objfixup::data_f;
void(*LaMulanaMemory::objfixup::orig_create_f)(object*);

/*

@frame
+frames
duration=input,input,^input,... (infinity if duration omitted)
save=slot
load=slot
rng=value
rng=value-steps # 12 after a load to counter the rng from the dust from landing
showrng
goto=frame

The possible inputs:
up,right,down,left
jump,main,sub,item
+m,-m,+s,-s
ok,cancel
ok2,ok3,cancel2,cancel3
esc,f1-f9
*/

TAS::TAS(char *base) : memory(base), frame(-1), frame_count(0)
{
    DWORD keyboard_delay, keyboard_speed;
    GLE(SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &keyboard_delay, 0));
    GLE(SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &keyboard_speed, 0));
    repeat_delay = 15 + 15 * keyboard_delay;
    repeat_speed = 23 - 7 * keyboard_speed / 10;

    shopping_overlay = new ShoppingOverlay(*this);
}

void TAS::LoadBindings()
{
    name2vk["up"] = VK_UP;
    name2vk["right"] = VK_RIGHT;
    name2vk["down"] = VK_DOWN;
    name2vk["left"] = VK_LEFT;
    name2vk["jump"] = memory.bindings->keys.p.jump;
    name2vk["main"] = memory.bindings->keys.p.attack;
    name2vk["sub"] = memory.bindings->keys.p.sub;
    name2vk["item"] = memory.bindings->keys.p.item;
    name2vk["+m"] = memory.bindings->keys.p.nextmain;
    name2vk["-m"] = memory.bindings->keys.p.prevmain;
    name2vk["+s"] = memory.bindings->keys.p.prevsub;
    name2vk["-s"] = memory.bindings->keys.p.nextsub;
    name2vk["+menu"] = memory.bindings->keys.p.nextmenu;
    name2vk["-menu"] = memory.bindings->keys.p.prevmenu;
    name2vk["ok"] = memory.bindings->keys.p.ok;
    name2vk["cancel"] = memory.bindings->keys.p.cancel;
    name2vk["msx"] = memory.bindings->keys.p.msx;
    name2vk["ok2"] = name2vk["space"] = VK_SPACE;
    name2vk["ok3"] = name2vk["return"] = VK_RETURN;
    name2vk["cancel2"] = name2vk["bksp"] = VK_BACK;
    name2vk["cancel3"] = name2vk["esc"] = VK_ESCAPE;
    name2vk["pause"] = name2vk["f1"] = memory.bindings->keys.p.pause;
    name2vk["f2"] = memory.bindings->keys.f2;
    name2vk["f3"] = memory.bindings->keys.f3;
    name2vk["f4"] = memory.bindings->keys.f4;
    name2vk["f5"] = memory.bindings->keys.f5;
    name2vk["f6"] = memory.bindings->keys.f6;
    name2vk["f7"] = memory.bindings->keys.f7;
    name2vk["f8"] = memory.bindings->keys.f8;
    name2vk["f9"] = memory.bindings->keys.f9;
    name2vk["p-up"] = PAD_UP;
    name2vk["p-down"] = PAD_DOWN;
    name2vk["p-left"] = PAD_LEFT;
    name2vk["p-right"] = PAD_RIGHT;
    name2vk["p-jump"] = PAD_BTN + memory.bindings->xinput.jump;
    name2vk["p-main"] = PAD_BTN + memory.bindings->xinput.attack;
    name2vk["p-sub"] = PAD_BTN + memory.bindings->xinput.sub;
    name2vk["p-item"] = PAD_BTN + memory.bindings->xinput.item;
    name2vk["p+m"] = PAD_BTN + memory.bindings->xinput.nextmain;
    name2vk["p-m"] = PAD_BTN + memory.bindings->xinput.prevmain;
    name2vk["p+s"] = PAD_BTN + memory.bindings->xinput.nextsub;
    name2vk["p-s"] = PAD_BTN + memory.bindings->xinput.prevsub;
    name2vk["p+menu"] = PAD_BTN + memory.bindings->xinput.nextmenu;
    name2vk["p-menu"] = PAD_BTN + memory.bindings->xinput.prevmenu;
    name2vk["p-ok"] = PAD_BTN + memory.bindings->xinput.ok;
    name2vk["p-cancel"] = PAD_BTN + memory.bindings->xinput.cancel;
    name2vk["p-pause"] = PAD_BTN + memory.bindings->xinput.pause;
    name2vk["p-msx"] = PAD_BTN + memory.bindings->xinput.msx;
}

class parsing_exception :std::exception {
public:
    const std::string reason;
    parsing_exception(const std::string &reason_) : reason(reason_) {};
    parsing_exception(const parsing_exception& copy) : reason(copy.reason) {};
    parsing_exception() : reason("") {}
    const char *what() { return reason.data(); }
};

void TAS::LoadTAS()
{
    std::ifstream f;
    f.exceptions(std::ifstream::badbit);
    f.open("script.txt", std::ios::in | std::ios::binary);
    if (f.fail())
    {
        //MessageBox(nullptr, L"Couldn't load script.txt", L"TAS error", MB_OK);
        return;
    }

    LoadBindings();

    int curframe = 0;
    size_t p = 0;
    int linenum = 0;
    std::string line, token;
    frame_inputs.clear();
    frame_inputs.emplace(0, std::unordered_set<int>());
    frame_actions.clear();
    sections.clear();
    objfixups.clear();

    std::map<std::string, int> marks;

    std::regex
        re_atframe("@([0-9]+)"),
        re_addframes("\\+([0-9]+)"),
        re_inputs("([0-9]*)=((\\^?[-+a-z0-9]+)(,(\\^?[-+a-z0-9]+))*)"),
        re_goto("goto=([0-9]+)"),
        re_load("load=([0-9]+)"),
        re_save("save=([0-9]+)"),
        re_rng("rng(=([0-9]+))?([+-][0-9]+)?"),
        re_fixup("fixup=([a-z]+):([[:xdigit:]]+):([[:xdigit:]]+):((?:[[:xdigit:]][[:xdigit:]])+)"),
        re_mark("([a-zA-Z0-9]+)(::?)"),
        re_markrel("([a-zA-Z0-9]+):([0-9]+)");
    try {
        while (!f.eof())
        {
            std::getline(f, line);
            linenum++;
            size_t comment = line.find('#');
            if (comment != std::string::npos)
                line.resize(comment);

            std::istringstream is(line);
            while (is >> token)
            {
                std::smatch m;
                if (std::regex_match(token, m, re_atframe))
                {
                    curframe = std::stoi(m[1]);
                    continue;
                }
                if (std::regex_match(token, m, re_addframes))
                {
                    curframe += std::stoi(m[1]);
                    continue;
                }
                if (std::regex_match(token, m, re_mark))
                {
                    marks[m[1]] = curframe;
                    if (m[2] == "::")
                        sections[curframe] = m[1];
                    continue;
                }
                if (std::regex_match(token, m, re_markrel))
                {
                    try
                    {
                        curframe = marks.at(m[1]) + std::stoi(m[2]);
                    }
                    catch (std::out_of_range&)
                    {
                        throw parsing_exception(strprintf("Undefined mark '%s' on line %d", m[1].str().data(), linenum));
                    }
                    continue;
                }
                if (std::regex_match(token, m, re_inputs))
                {
                    int frames = 0;
                    if (m[1].length())
                        frames = std::stoi(m[1]);
                    frame_inputs.emplace(curframe + frames, (--frame_inputs.upper_bound(curframe + frames))->second);
                    frame_inputs.emplace(curframe, (--frame_inputs.upper_bound(curframe))->second);
                    if (frames > 0)
                    {
                        frame_inputs.emplace(curframe + frames - 1, (--frame_inputs.upper_bound(curframe + frames - 1))->second);
                    }

                    size_t pos = 0;
                    while (pos != std::string::npos)
                    {
                        size_t end = m[2].str().find(',', pos);
                        std::string input = m[2].str().substr(pos, end == std::string::npos ? end : end - pos);
                        bool release = false;
                        if (input[0] == '^')
                            release = true, input = input.substr(1);
                        int vk;
                        try
                        {
                            vk = name2vk.at(input);

                        }
                        catch (std::out_of_range&)
                        {
                            throw parsing_exception(strprintf("Unknown input '%s' on line %d", input.data(), linenum));
                        }

                        frame_iter last_frame = frame_inputs.lower_bound(curframe + frames);
                        if (frames == 0)
                            last_frame = frame_inputs.end();
                        for (auto i = frame_inputs.find(curframe); i != last_frame; ++i)
                            if (release)
                                i->second.erase(vk);
                            else
                                i->second.insert(vk);

                        pos = end == std::string::npos ? end : end + 1;
                    }
                    continue;
                }
                if (std::regex_match(token, m, re_goto))
                {
                    frame_actions.emplace(curframe, std::list<std::function<void()>>());
                    int gotoframe = std::stoi(m[1]);
                    frame_actions.find(curframe)->second.push_back([this, gotoframe]() { frame = gotoframe; });
                    continue;
                }
                if (std::regex_match(token, m, re_load))
                {
                    frame_actions.emplace(curframe, std::list<std::function<void()>>());
                    int slot = std::stoi(m[1]);
                    frame_actions.find(curframe)->second.push_back([this, slot]() { memory.loadslot(slot); });
                    continue;
                }
                if (std::regex_match(token, m, re_save))
                {
                    frame_actions.emplace(curframe, std::list<std::function<void()>>());
                    int slot = std::stoi(m[1]);
                    frame_actions.find(curframe)->second.push_back([this, slot]() { memory.saveslot(slot); });
                    continue;
                }
                if (std::regex_match(token, m, re_rng))
                {
                    frame_actions.emplace(curframe, std::list<std::function<void()>>());
                    int seed = m[1].length() ? std::stoi(m[2]) : -1;
                    int stepcount = m[3].length() ? std::stoi(m[3]) : 0;
                    frame_actions.find(curframe)->second.push_back([this, seed, stepcount]() {
                        int rng = seed >= 0 ? seed : memory.rng, steps = stepcount;
                        for (; steps > 0; --steps)
                            rng = rng * 109 + 1021 & 0x7fff;
                        for (; steps < 0; ++steps)
                            rng = (rng + 31747) * 2405 & 0x7fff;
                        memory.rng = rng;
                        currng = -1;
                    });
                    continue;
                }
                if (std::regex_match(token, m, re_fixup))
                {
                    int type = std::stoi(m[2], 0, 16), off = std::stoi(m[3], 0, 16);
                    std::vector<std::pair<int, unsigned char>> data(m[4].length() / 2);
                    for (int i = 0; i != m[4].length(); i += 2)
                    {
                        data[i >> 1].first = off + (i >> 1);
                        data[i >> 1].second = std::stoi(m[4].str().substr(i, 2), 0, 16);
                    }
                    objfixups.emplace_back(&memory, type, data);
                    if (m[1] == "obj")
                    {
                        if (type >= 204)
                            throw parsing_exception(strprintf("Object fixup type %x out of bounds on line %d", type, linenum));
                        if (off >= sizeof(LaMulanaMemory::object))
                            throw parsing_exception(strprintf("Object fixup offset %x out of bounds on line %d", off, linenum));
                        if (!has_reset)
                            continue;
                        frame_actions.emplace(curframe, std::list<std::function<void()>>());
                        frame_actions.find(curframe)->second.push_back([&fixup = *--objfixups.end()]() {
                            fixup.inject();
                        });
                        frame_actions.emplace(curframe + 1, std::list<std::function<void()>>());
                        frame_actions.find(curframe + 1)->second.push_back([&fixup = *--objfixups.end()]() {
                            fixup.remove();
                        });
                    }
                    else
                        throw parsing_exception(strprintf("Unrecognised fixup type '%s' on line %d", m[1].str().data(), linenum));
                    continue;
                }
                if ("break" == token)
                {
                    frame_actions.emplace(curframe, std::list<std::function<void()>>());
                    frame_actions.find(curframe)->second.push_back([this]() { pause = true; });
                    continue;
                }
                throw parsing_exception(strprintf("Unrecognised expression '%s' on line %d", token.data(), linenum));
            }
        }
    }
    catch (parsing_exception& ex)
    {
        MessageBoxA(nullptr, ex.what(), "TAS parsing error", MB_OK);
    }
}

bool TAS::KeyPressed(int vk)
{
    if (resetting)
        return false;
    return (scripting && cur_frame_inputs.count(vk))
        || (passthrough && keys[vk].passthrough);
}

DWORD TAS::GetXInput(DWORD idx, XINPUT_STATE *state)
{
    DWORD ret = ERROR_DEVICE_NOT_CONNECTED;
    if (passthrough)
        ret = (*memory.XInputGetState)(idx, state);
    if (ret != ERROR_SUCCESS)
        memset(state, 0, sizeof *state);

    if (scripting)
    {
        for (auto &&i : cur_frame_inputs)
        {
            if (i < 0x100)
                continue;
            ret = ERROR_SUCCESS;
            if (i >= PAD_START && i <= PAD_Y)
                state->Gamepad.wButtons |= XINPUT_BTN[i - PAD_START];
            else if (i >= PAD_UP && i <= PAD_RIGHT)
                state->Gamepad.wButtons |= XINPUT_DPAD[i - PAD_UP];
            else if (i == PAD_L2)
                state->Gamepad.bLeftTrigger = 255;
            else if (i == PAD_R2)
                state->Gamepad.bRightTrigger = 255;
        }
    }
    return ret;
}

void TAS::IncFrame()
{
    if (!initialised)
    {
        LoadTAS();
        initialised = true;
    }

    UpdateKeys();
    ProcessKeys();

    frame++;

    auto action_iter = frame_actions.find(frame);
    if (action_iter != frame_actions.end())
        for (auto x : action_iter->second)
            x();

    cur_frame_inputs.clear();
    auto input_iter = --frame_inputs.upper_bound(frame);
    if (frame_inputs.end() != input_iter)
        cur_frame_inputs = input_iter->second;

    if (frame >= 0)
    {
        frame_count++;
        resetting = false;
    }
    if (currng < 0)
    {
        currng = memory.rng;
        rngsteps = 0;
    }
    if (pause)
        run = false;

    if (_InterlockedExchange(&memory.winproc_inputdev, 0) && passthrough)
        memory.cur_inputdev = 4;
    else if (scripting)
        for (auto &&i : cur_frame_inputs)
            if (i < 256)
            {
                memory.cur_inputdev = 4;
                break;
            }
}
