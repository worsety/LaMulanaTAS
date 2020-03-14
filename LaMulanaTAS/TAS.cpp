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
    object_viewer = new ObjectViewer(*this);
}

unsigned char hardcoded_bindings[] = {
    VK_UP,
    VK_RIGHT,
    VK_DOWN,
    VK_LEFT,
    VK_SPACE,
    VK_RETURN,
    VK_BACK,
    VK_ESCAPE,
};

// This is pretty upsetting.  The first time the games polls input devices it hasn't loaded bindings yet
void TAS::LoadBindings()
{
    static unsigned char
        up = VK_UP,
        down = VK_DOWN,
        left = VK_LEFT,
        right = VK_RIGHT,
        space = VK_SPACE,
        ret = VK_RETURN,
        bksp = VK_BACK,
        esc = VK_ESCAPE,
        pad_up = PAD_UP,
        pad_down = PAD_DOWN,
        pad_left = PAD_LEFT,
        pad_right = PAD_RIGHT;
    name2vk["up"] = &up;
    name2vk["down"] = &down;
    name2vk["left"] = &left;
    name2vk["right"] = &right;
    name2vk["jump"] = &memory.bindings->keys.p.jump;
    name2vk["main"] = &memory.bindings->keys.p.attack;
    name2vk["sub"] = &memory.bindings->keys.p.sub;
    name2vk["item"] = &memory.bindings->keys.p.item;
    name2vk["+m"] = &memory.bindings->keys.p.nextmain;
    name2vk["-m"] = &memory.bindings->keys.p.prevmain;
    name2vk["+s"] = &memory.bindings->keys.p.prevsub;
    name2vk["-s"] = &memory.bindings->keys.p.nextsub;
    name2vk["+menu"] = &memory.bindings->keys.p.nextmenu;
    name2vk["-menu"] = &memory.bindings->keys.p.prevmenu;
    name2vk["ok"] = &memory.bindings->keys.p.ok;
    name2vk["cancel"] = &memory.bindings->keys.p.cancel;
    name2vk["msx"] = &memory.bindings->keys.p.msx;
    name2vk["ok2"] = name2vk["space"] = &space;
    name2vk["ok3"] = name2vk["return"] = &ret;
    name2vk["cancel2"] = name2vk["bksp"] = &bksp;
    name2vk["cancel3"] = name2vk["esc"] = &esc;
    name2vk["pause"] = name2vk["f1"] = &memory.bindings->keys.p.pause;
    name2vk["f2"] = &memory.bindings->keys.f2;
    name2vk["f3"] = &memory.bindings->keys.f3;
    name2vk["f4"] = &memory.bindings->keys.f4;
    name2vk["f5"] = &memory.bindings->keys.f5;
    name2vk["f6"] = &memory.bindings->keys.f6;
    name2vk["f7"] = &memory.bindings->keys.f7;
    name2vk["f8"] = &memory.bindings->keys.f8;
    name2vk["f9"] = &memory.bindings->keys.f9;
    name2btn["p-up"] = &pad_up;
    name2btn["p-down"] = &pad_down;
    name2btn["p-left"] = &pad_left;
    name2btn["p-right"] = &pad_right;
    name2btn["p-jump"] = &memory.bindings->xinput.jump;
    name2btn["p-main"] = &memory.bindings->xinput.attack;
    name2btn["p-sub"] = &memory.bindings->xinput.sub;
    name2btn["p-item"] = &memory.bindings->xinput.item;
    name2btn["p+m"] = &memory.bindings->xinput.nextmain;
    name2btn["p-m"] = &memory.bindings->xinput.prevmain;
    name2btn["p+s"] = &memory.bindings->xinput.nextsub;
    name2btn["p-s"] = &memory.bindings->xinput.prevsub;
    name2btn["p+menu"] = &memory.bindings->xinput.nextmenu;
    name2btn["p-menu"] = &memory.bindings->xinput.prevmenu;
    name2btn["p-ok"] = &memory.bindings->xinput.ok;
    name2btn["p-cancel"] = &memory.bindings->xinput.cancel;
    name2btn["p-pause"] = &memory.bindings->xinput.pause;
    name2btn["p-msx"] = &memory.bindings->xinput.msx;
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

    LoadBindings(); // except not really :(

    int curframe = 0;
    size_t p = 0;
    int linenum = 0;
    std::string line, token;
    frame_keys.clear();
    frame_keys.emplace(0, std::unordered_set<unsigned char*>());
    frame_btns.clear();
    frame_btns.emplace(0, std::unordered_set<unsigned char*>());
    frame_actions.clear();
    sections.clear();
    objfixups.clear();

    std::map<std::string, int> marks;
    std::unordered_set<std::string> use;
    int true_depth = 0, false_depth = 0;

    auto err = [&linenum](const std::string s, auto... params)
    {
        if (sizeof...(params))
            throw parsing_exception(strprintf((s + " [%d]").data(), params..., linenum));
        else
            throw parsing_exception(strprintf("%s [%d]", s.data(), linenum));
    };

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
                if (token == "!use" || token == "!if")
                {
                    std::string s;
                    if (!(is >> s))
                        err(token + " must be followed by a name");
                    if (token == "!use" && 0 == false_depth)
                        use.insert(s);
                    else if (token == "!if")
                        if (false_depth > 0 || 0 == use.count(s))
                            false_depth++;
                        else
                            true_depth++;
                    continue;
                }
                if (token == "!else")
                {
                    if (false_depth > 1)
                        ;
                    else if (1 == false_depth)
                        false_depth--, true_depth++;
                    else if (true_depth > 0)
                        true_depth--, false_depth++;
                    else
                        err("Unmatched !else");
                    continue;
                }
                if (token == "!endif")
                {
                    if (false_depth > 0)
                        false_depth--;
                    else if (true_depth > 0)
                        true_depth--;
                    else
                        err("Unmatched !endif");
                    continue;
                }
                if (false_depth > 0)
                    continue;
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
                        err("Undefined mark '%s'", m[1].str().data());
                    }
                    continue;
                }
                if (std::regex_match(token, m, re_inputs))
                {
                    int frames = 0;
                    if (m[1].length())
                        frames = std::stoi(m[1]);
                    for (auto *frame_inputs : { &frame_keys, &frame_btns })
                    {
                        frame_inputs->emplace(curframe + frames, (--frame_inputs->upper_bound(curframe + frames))->second);
                        frame_inputs->emplace(curframe, (--frame_inputs->upper_bound(curframe))->second);
                        if (frames > 0)
                        {
                            frame_inputs->emplace(curframe + frames - 1, (--frame_inputs->upper_bound(curframe + frames - 1))->second);
                        }
                    }

                    size_t pos = 0;
                    while (pos != std::string::npos)
                    {
                        size_t end = m[2].str().find(',', pos);
                        std::string input = m[2].str().substr(pos, end == std::string::npos ? end : end - pos);
                        bool release = false;
                        if (input[0] == '^')
                            release = true, input = input.substr(1);
                        unsigned char *inptr;
                        bool is_btn = 0 != name2btn.count(input);
                        try
                        {
                            if (is_btn)
                                inptr = name2btn.at(input);
                            else
                                inptr = name2vk.at(input);
                        }
                        catch (std::out_of_range&)
                        {
                            err("Unknown input '%s'", input.data());
                        }

                        auto &frame_inputs = is_btn ? frame_btns : frame_keys;
                        input_iter last_frame = frame_inputs.lower_bound(curframe + frames);
                        if (frames == 0)
                            last_frame = frame_inputs.end();
                        for (auto i = frame_inputs.find(curframe); i != last_frame; ++i)
                            if (release)
                                i->second.erase(inptr);
                            else
                                i->second.insert(inptr);

                        pos = end == std::string::npos ? end : end + 1;
                    }
                    continue;
                }
                if (std::regex_match(token, m, re_goto))
                {
                    int gotoframe = std::stoi(m[1]);
                    frame_actions.emplace(curframe, 0).first->second.push_back([this, gotoframe]() { frame = gotoframe; });
                    continue;
                }
                if (std::regex_match(token, m, re_load))
                {
                    int slot = std::stoi(m[1]);
                    frame_actions.emplace(curframe, 0).first->second.push_back([this, slot]() { memory.loadslot(slot); });
                    continue;
                }
                if (std::regex_match(token, m, re_save))
                {
                    int slot = std::stoi(m[1]);
                    frame_actions.emplace(curframe, 0).first->second.push_back([this, slot]() { memory.saveslot(slot); });
                    continue;
                }
                if (std::regex_match(token, m, re_rng))
                {
                    int seed = m[1].length() ? std::stoi(m[2]) : -1;
                    int stepcount = m[3].length() ? std::stoi(m[3]) : 0;
                    frame_actions.emplace(curframe, 0).first->second.push_back([this, seed, stepcount]() {
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
                            err("Object fixup type %x out of bounds", type);
                        if (off >= sizeof(LaMulanaMemory::object))
                            err("Object fixup offset %x out of bounds", off);
                        if (!has_reset)
                            continue;
                        frame_actions.emplace(curframe, 0).first->second.push_back([&fixup = *--objfixups.end()]() {
                            fixup.inject();
                        });
                        frame_actions.emplace(curframe + 1, 0).first->second.push_back([&fixup = *--objfixups.end()]() {
                            fixup.remove();
                        });
                    }
                    else
                        err("Unrecognised fixup type '%s'", m[1].str().data());
                    continue;
                }
                if ("break" == token)
                {
                    frame_actions.emplace(curframe, 0).first->second.push_back([this]() { pause = true; });
                    continue;
                }
                err("Unrecognised expression '%s'", token.data());
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
    return (scripting && cur_frame_keys.count(vk))
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
        for (auto &&i : cur_frame_btns)
        {
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
        memory.LoadObjNames();
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

    cur_frame_keys.clear();
    cur_frame_btns.clear();
    auto key_iter = --frame_keys.upper_bound(frame);
    if (frame_keys.end() != key_iter)
        for (auto &&k : key_iter->second)
            cur_frame_keys.insert(*k);
    auto btn_iter = --frame_btns.upper_bound(frame);
    if (frame_btns.end() != btn_iter)
        for (auto &&b : btn_iter->second)
            cur_frame_btns.insert(*b);

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

    if (passthrough && _InterlockedExchange(&memory.winproc_inputdev, 0)
        || scripting && !cur_frame_keys.empty())
        memory.cur_inputdev = 4;
}
