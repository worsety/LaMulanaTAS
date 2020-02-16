// LaMulanaTAS.cpp : Defines the exported functions for the DLL application.
//

#include "LaMulanaTAS.h"
#include "LaMulanaMemory.h"
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
#include "resource.h"

#ifndef _NDEBUG
#define D3D_DEBUG_INFO
#endif
#include "d3d9.h"
#include "atlbase.h"

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

class TAS
{
public:
	LaMulanaMemory memory;
	int frame, frame_count, rngsteps;
	std::map<int, std::string> sections;
	std::map<int, std::unordered_set<int>> frame_inputs;
	std::map<int, std::list<std::function<void()>>> frame_actions;
	std::vector<LaMulanaMemory::objfixup> objfixups;
	short currng = -1;
	using frame_iter = std::map<int, std::unordered_set<int>>::iterator;
	std::map<std::string, int> name2vk;
	IDirect3DDevice9 *curdev;
	std::unique_ptr<BitmapFont> font4x6, font8x12;
	struct {
		CComPtr<IDirect3DTexture9> tex;
		float texel_w, texel_h;
	} hit_parts, hit_hex;
	CComPtr<IDirect3DTexture9> overlay;
	CComPtr<IDirect3DSurface9> overlay_surf;

	struct keystate {
		unsigned char held : 1;
		unsigned char pressed : 1;
		unsigned char released : 1;
	} keys[256];

	void UpdateKeys()
	{
		if (!memory.kb_enabled)
			return;
		// there's a very efficient way to do this with an xor but whatever, legibility
		for (int vk = 0; vk < 256; vk++)
		{
			bool held = GetAsyncKeyState(vk) < 0;
			keys[vk].pressed = held && !keys[vk].held;
			keys[vk].released = !held && keys[vk].held;
			keys[vk].held = held;
		}
	}

	bool run = true, pause, resetting, has_reset, ff;
	bool scripting = true, kbpassthrough = true;
	bool show_overlay = true, show_exits, show_solids, show_loc, hide_game;
	int show_tiles;
	unsigned show_hitboxes = 1 << 7 | 1 << 9 | 1 << 11;  // unknown types, I want to know if anyone sees them

	TAS(char *base);
	bool KeyPressed(int vk);
	void IncFrame();
	void Overlay();
	void DrawOverlay();
	void ProcessKeys();

	void LoadTAS();
};

TAS::TAS(char *base) : memory(base), frame(-1), frame_count(0)
{
	name2vk["up"] = VK_UP;
	name2vk["right"] = VK_RIGHT;
	name2vk["down"] = VK_DOWN;
	name2vk["left"] = VK_LEFT;
	name2vk["jump"] = 'Z';
	name2vk["main"] = 'X';
	name2vk["sub"] = 'C';
	name2vk["item"] = 'V';
	name2vk["+m"] = 'A';
	name2vk["-m"] = 'Q';
	name2vk["+s"] = 'S';
	name2vk["-s"] = 'W';
	name2vk["ok"] = 'Z';
	name2vk["cancel"] = 'X';
	name2vk["ok2"] = VK_SPACE;
	name2vk["ok3"] = VK_RETURN;
	name2vk["cancel2"] = VK_BACK;
	name2vk["cancel3"] = VK_ESCAPE;
	name2vk["f1"] = VK_F1;
	name2vk["f2"] = VK_F2;
	name2vk["f3"] = VK_F3;
	name2vk["f4"] = VK_F4;
	name2vk["f5"] = VK_F5;
	name2vk["f6"] = VK_F6;
	name2vk["f7"] = VK_F7;
	name2vk["f8"] = VK_F8;
	name2vk["f9"] = VK_F9;
	memset(keys, 0, sizeof keys);

	LoadTAS();
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
				if ("pause" == token)
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
	auto iter = --frame_inputs.upper_bound(frame);
	return (scripting && frame_inputs.end() != iter && 0 != iter->second.count(vk))
		|| (kbpassthrough && memory.kb_enabled && keys[vk].held);
}

static const struct {
	int vk, dispkey, type;
} hitboxkeys[] =
{
	{ '1', '1', 0 },
	{ '2', '2', 1 },
	{ '3', '3', 2 },
	{ '4', '4', 3 },
	{ '5', '5', 4 },
	{ '6', '6', 5 },
	{ '7', '7', 6 },
	{ '8', '8', 8 },
	{ '9', '9', 10 },
	{ '0', '0', 12 },
};

void TAS::ProcessKeys()
{
	if (keys[VK_OEM_6].pressed)
	{
		if (pause)
			run = true;
		pause = true;
	}
	if (keys['O'].pressed)
		show_overlay = !show_overlay;
	if (keys['K'].pressed)
		show_exits = !show_exits;
	if (keys['L'].pressed)
		show_loc = !show_loc;

	for (auto &&k : hitboxkeys)
		show_hitboxes ^= keys[k.vk].pressed << k.type;
	if (keys[VK_OEM_MINUS].pressed)
		show_solids = !show_solids;
	if (keys[VK_OEM_PLUS].pressed)
		show_tiles = (show_tiles + 1) % 3;
	if (keys[VK_OEM_1].pressed)
		hide_game = !hide_game;
	if (keys['P'].pressed)
	{
		run = true, ff = true, pause = false;
		memory.setvsync(false);
	}
	if (keys[VK_OEM_4].pressed)
		run = true, ff = false, pause = false;
	if (keys['R'].pressed)
		LoadTAS();
	if (keys['T'].pressed) {
		has_reset = true;
		LoadTAS();
		memory.kill_objects();
		memory.scrub_objects();
		memory.reset_game();
		memory.has_quicksave = 0;
		memory.timeattack_cursor = -1;
		frame = -2;
		frame_count = 0;
		run = resetting = true;
	}
}

void TAS::IncFrame()
{
	UpdateKeys();
	ProcessKeys();

	frame++;
	auto iter = frame_actions.find(frame);
	if (iter != frame_actions.end())
		for (auto x : iter->second)
			x();
	// P toggles frame limiter

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
}

static const RECT unscaled_game{ 0, 0, 640, 480 };

void TAS::Overlay()
{
	if (memory.game_state < 4)
		return;

	int target_time = timeGetTime();

	DrawOverlay();
	if (!run) {
		IDirect3DDevice9 *dev = memory.id3d9dev();
		CComPtr<IDirect3DSurface9> window_surface;
		RECT display_rect{
			(LONG)memory.game_horz_offset,
			(LONG)memory.game_vert_offset,
			(LONG)(memory.game_horz_offset + memory.game_horz_scale * memory.game_width),
			(LONG)(memory.game_vert_offset + memory.game_vert_scale * memory.game_height),
		};
		while (!run && memory.game_state != 5) {
			D3D9CHECKED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &window_surface));
			D3D9CHECKED(dev->StretchRect(overlay_surf, &unscaled_game, window_surface, &display_rect, D3DTEXF_LINEAR));
			D3D9CHECKED(dev->EndScene());
			D3D9CHECKED(dev->Present(nullptr, nullptr, nullptr, nullptr));
			window_surface = 0;
			// definitely not how to achieve a steady 60 fps but that's not critical here
			target_time += 16;
			int sleep_time = target_time - timeGetTime();
			if (sleep_time > 0)
				Sleep(sleep_time);
			UpdateKeys();
			ProcessKeys();
			D3D9CHECKED(dev->BeginScene());
			DrawOverlay();
		}
	}

	memory.postprocessed_gamesurf = overlay_surf;
}

void TAS::DrawOverlay()
{
	IDirect3DDevice9 *dev = memory.id3d9dev();
	if (curdev != dev)
	{
		font4x6.reset();
		font8x12.reset();
		hit_parts.tex.p = hit_hex.tex.p = 0;
		overlay = nullptr, overlay_surf = nullptr;
		curdev = dev;
	}

	if (!overlay) {
		D3D9CHECKED(dev->CreateTexture(640, 480, 1, D3DUSAGE_RENDERTARGET, memory.display_format, D3DPOOL_DEFAULT, &overlay, nullptr));
		D3D9CHECKED(overlay->GetSurfaceLevel(0, &overlay_surf));
	}

	D3D9CHECKED(dev->SetRenderTarget(0, overlay_surf));
	if (hide_game)
		D3D9CHECKED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 0.f, 0));
	else
		D3D9CHECKED(dev->StretchRect(memory.postprocessed_gamesurf, &unscaled_game, overlay_surf, &unscaled_game, D3DTEXF_POINT));

	CComPtr<IDirect3DStateBlock9> oldstate;
	D3D9CHECKED(dev->CreateStateBlock(D3DSBT_ALL, &oldstate));

	if (!font4x6)
		font4x6.reset(new BitmapFont(dev, 4, 6, tasModule, IDB_TOMTHUMB));
	if (!font8x12)
		font8x12.reset(new BitmapFont(dev, 8, 12, tasModule, IDB_SMALLFONT));
	if (!hit_parts.tex)
	{
		LaMulanaMemory::texture tex;
		memset(&tex, 0, sizeof tex);
		// this is 160x160, I hope this doesn't make a npot texture if not supported
		memory.loadgfx("hit_parts.png", &tex);
		hit_parts.tex.p = tex.tex;
		hit_parts.texel_w = 1.f / tex.w;
		hit_parts.texel_h = 1.f / tex.h;
	}
	if (!hit_hex.tex)
	{
		CComPtr<IDirect3DTexture9> tex;
		CComPtr<IDirect3DSurface9> surf;
		D3DLOCKED_RECT rect1, rect2;

		D3D9CHECKED(dev->CreateRenderTarget(256, 256, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &surf, nullptr));
		for (int i = 0; i < 256; i++)
			font4x6->Add(1.f + (i & 0x0f) * 10.f, 2.f + (i & 0xf0) / 16 * 10.f, 0, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%.2x", i));
		D3D9CHECKED(dev->SetRenderTarget(0, surf));
		font4x6->Draw();
		D3D9CHECKED(dev->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr));
		D3D9CHECKED(surf->LockRect(&rect1, nullptr, D3DLOCK_READONLY));
		D3D9CHECKED(tex->LockRect(0, &rect2, nullptr, 0));
		for (int i = 0; i < 256; i++)
			memcpy((char*)rect2.pBits + rect2.Pitch * i, (char*)rect1.pBits + rect1.Pitch * i, min(rect1.Pitch, rect2.Pitch));
		D3D9CHECKED(surf->UnlockRect());
		D3D9CHECKED(tex->UnlockRect(0));
		hit_hex.tex = tex;
		hit_hex.texel_w = 1.f / 256;
		hit_hex.texel_h = 1.f / 256;
		D3D9CHECKED(oldstate->Apply());
	}

	std::vector<xyzrhwdiff> hv;
	for (int type = 0; type <= 12; type++)
	{
		if (!(show_hitboxes & 1 << type))
			continue;
		int i = hv.size();
		auto hitboxes = memory.gethitboxes(type);
		hv.resize(i + hitboxes.count * 6);
		for (auto &&hitbox : hitboxes)
		{
			bool has_iframes = memory.iframes_create == hitbox.object->create;
			LaMulanaMemory::object &object = has_iframes ? *(LaMulanaMemory::object*)hitbox.object->local_ptr[0] : *hitbox.object;
#if 0
			font4x6->Add(hitbox.x, hitbox.y - 6.f, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 255, 255, 255),
				strprintf("%.8x %.8x %.8x", hitbox.unk2, hitbox.unk3, hitbox.unk4));
#endif
			switch (type)
			{
			case 0:
			case 3:
				font4x6->Add(hitbox.x, hitbox.y, BMFALIGN_LEFT | BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 0), strprintf("%d", object.hp));
				if (memory.pot_create == object.create && object.local_int[0])
				{
					static const char *droptypes[] = { "non", "$$$", "wgt", "shu", "rol", "spr", "flr", "bom", "chk", "ctr", "bul" };
					int type = object.local_int[0], quant = object.local_int[1];
					std::string typestr = type < sizeof droptypes / sizeof *droptypes ? droptypes[type] : strprintf("%3d", type);
					font4x6->Add(hitbox.x, hitbox.y + hitbox.h, 0, D3DCOLOR_ARGB(255, 255, 255, 0), strprintf("%s x%d", typestr.data(), quant));
				}
				//font4x6->Add(hitbox.x + hitbox.w, hitbox.y + hitbox.h, BMFALIGN_TOP | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 0, 255, 255), strprintf("%d", object.state));
				if (memory.mother5_create == object.create)
				{
					font4x6->Add(hitbox.x, hitbox.y + hitbox.h + 6.f, BMFALIGN_TOP | BMFALIGN_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%d", object.private_int[9]));
					font4x6->Add(hitbox.x + hitbox.w, hitbox.y + hitbox.h + 6.f, BMFALIGN_TOP | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%d", object.private_int[2]));
				}
				break;
			case 1:
			case 4:
			case 5:
			case 8:
			case 10:
				font4x6->Add(hitbox.x + hitbox.w, hitbox.y, BMFALIGN_RIGHT | BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 255, 0, 0), strprintf("%d", hitbox.damage));
				break;
			case 12:
				if (memory.drop_create == object.create)
					font4x6->Add(hitbox.x, hitbox.y, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 255), strprintf("%d", object.local_int[1]));
				break;
			}
			if (has_iframes)
				font4x6->Add(hitbox.x, hitbox.y - 6.f, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 255),
					strprintf("%d %d", hitbox.object->private_int[1], hitbox.object->private_int[2]));
			for (int vert = 0; vert < 6; vert++, i++)
			{
				int right = vert >= 1 && vert <= 3;
				int down = vert >= 2 && vert <= 4;
				hv[i].x = hitbox.x + right * hitbox.w - 0.5f;
				hv[i].y = hitbox.y + down * hitbox.h - 0.5f;
				hv[i].z = 0;
				hv[i].w = 1;
				switch (type)
				{
				case 0: // lemeza
					hv[i].color = D3DCOLOR_ARGB(128, 0, 255, 0);
					break;
				case 3: // enemy hurtbox
					hv[i].color = D3DCOLOR_ARGB(64, 0, 255, 0);
					break;
				case 1: // lemeza's weapons
				case 8: // scaling damage, omni
				case 10: // scaling damage, directional
					hv[i].color = D3DCOLOR_ARGB(128, 255, 0, 0);
					break;
				case 4: // enemy hitbox
					hv[i].color = D3DCOLOR_ARGB(64, 255, 0, 0);
					break;
				case 2: // lemeza's shield
					hv[i].color = D3DCOLOR_ARGB(128, 0, 0, 255);
					break;
				case 6: // enemy shields
					hv[i].color = D3DCOLOR_ARGB(64, 0, 0, 255);
					break;
				case 5: // shieldable attack
					hv[i].color = D3DCOLOR_ARGB(128, 255, 0, 255);
					break;
				case 12: // drops
					hv[i].color = D3DCOLOR_ARGB(128, 0, 255, 0);
					break;
				default: // unknown: 7, 9.  11 is unused
					hv[i].color = D3DCOLOR_ARGB(192, 255, 105, 180);
				}
			}
		}
	}
	if (hv.size())
	{
		D3D9CHECKED(dev->SetTexture(0, 0));
		D3D9CHECKED(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
		D3D9CHECKED(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
		D3D9CHECKED(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
		D3D9CHECKED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, hv.size() / 3, hv.data(), sizeof *hv.data()));
		font4x6->Draw(D3DCOLOR_ARGB(128, 0, 0, 0));
		D3D9CHECKED(oldstate->Apply());
	}

	if (show_tiles)
	{
		auto hit_tex = show_tiles == 1 ? hit_parts : hit_hex;
		const auto room = memory.getroom();
		const auto scroll = memory.flags1[1] & 0x400 ? LaMulanaMemory::scrolling() : memory.scroll_db[memory.scroll_dbidx];
		int w = 64, h = 48;
		if (room)
			w = room->w, h = room->h;

		std::vector<USHORT> idx(6 * 64 * 48);
		std::vector<xyzrhwdiffuv> vert(4 * 64 * 48);

		int i = 0;
		for (int y = max(0, (int)(scroll.y / 10)); y < min(h, 48 + (int)(scroll.y / 10)); y++)
			for (int x = max(0, (int)(scroll.x / 10)); x < min(w, 64 + (int)(scroll.x / 10)); x++)
			{
				unsigned char tile = memory.gettile_effective(x, y);
				if (!tile)
					continue;

				for (int j = 0; j < 4; j++)
				{
					float u = (tile & 0x0f) * (10.f * hit_tex.texel_w), v = (tile & 0xf0) / 16 * (10.f * hit_tex.texel_h);
					vert[i * 4 + j].x = x * 10 + 10 * !!(j & 1) - scroll.x - 0.5f;
					vert[i * 4 + j].y = y * 10 + 10 * !!(j & 2) - scroll.y - 0.5f;
					vert[i * 4 + j].u = u + (10.f * hit_tex.texel_w) * !!(j & 1);
					vert[i * 4 + j].v = v + (10.f * hit_tex.texel_h) * !!(j & 2);
					vert[i * 4 + j].z = 0;
					vert[i * 4 + j].w = 1;
					vert[i * 4 + j].color = D3DCOLOR_ARGB(128, 255, 255, 255);
				}

				idx[i * 6 + 0] = i * 4 + 0;
				idx[i * 6 + 1] = i * 4 + 1;
				idx[i * 6 + 2] = i * 4 + 3;
				idx[i * 6 + 3] = i * 4 + 3;
				idx[i * 6 + 4] = i * 4 + 2;
				idx[i * 6 + 5] = i * 4 + 0;

				i++;
			}

		D3D9CHECKED(dev->SetTexture(0, hit_tex.tex));
		D3D9CHECKED(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1));
		D3D9CHECKED(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
		D3D9CHECKED(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
		D3D9CHECKED(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, i * 4, i * 2, idx.data(), D3DFMT_INDEX16, vert.data(), sizeof vert[0]));
		D3D9CHECKED(oldstate->Apply());
	}

	if (show_solids)
	{
		int i = 0;
		auto &solids = memory.getsolids();
		std::vector<xyzrhwdiff> vert(solids.count * 4);
		std::vector<int> idx;
		idx.reserve(solids.count * 6);
		const float degtorad = 3.1415926535897932384626433832795f / 180.f;
		for (auto &&solid : solids)
		{
#if 1 // TODO: figure out the field at 0x24
			font4x6->Add(solid.x, solid.y, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 255, 255, 255),
				strprintf("%6.4f %6.4f %x",
					solid.vel_x, solid.vel_y, solid.unk24));
#endif
			float pivot_x = solid.x + solid.pivot_x, pivot_y = solid.y + solid.pivot_y;
			float angle = solid.rotdeg * degtorad;
			for (int j = 0; j < 4; j++, i)
			{
				float x = solid.x + solid.w * !!(j & 1), y = solid.y + solid.h * !!(j & 2);
				vert[i + j].x = pivot_x + cosf(angle) * (x - pivot_x) - sinf(angle) * (y - pivot_y) - 0.5f;
				vert[i + j].y = pivot_y + sinf(angle) * (x - pivot_x) + cosf(angle) * (y - pivot_y) - 0.5f;
				vert[i + j].z = 0; vert[i + j].w = 1; vert[i + j].color = D3DCOLOR_ARGB(64, 160, 160, 192);
			}
			idx.insert(idx.end(), { i, i + 1, i + 3, i + 3, i + 2, i });
			i += 4;
		}
		D3D9CHECKED(dev->SetTexture(0, 0));
		D3D9CHECKED(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
		D3D9CHECKED(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4 * solids.count, 2 * solids.count, idx.data(), D3DFMT_INDEX32, vert.data(), sizeof vert[0]));
		font4x6->Draw();
		D3D9CHECKED(oldstate->Apply());
	}

	if (show_loc && memory.lemeza_spawned)
	{
		LaMulanaMemory::object &lemeza = *memory.lemeza_obj;
		std::vector<xyzrhwdiff> vert(12);
		vert[0].x = vert[1].x = lemeza.x + 10.f;
		vert[2].x = vert[3].x = lemeza.x + 19.f;
		vert[4].x = vert[5].x = lemeza.x + 29.f;
		vert[6].y = vert[7].y = lemeza.y + 12.f;
		vert[8].y = vert[9].y = lemeza.y + 40.f;
		vert[10].y = vert[11].y = lemeza.y + 47.f;
		for (size_t i = 0; i < 6; i++)
			vert[i].y = -0.5f + 480.f * (i & 1);
		for (size_t i = 6; i < vert.size(); i++)
			vert[i].x = -0.5f + 640.f * (i & 1);
		for (auto &v : vert)
			v.z = 0, v.color = D3DCOLOR_ARGB(192, 255, 255, 0);
		D3D9CHECKED(dev->SetTexture(0, 0));
		D3D9CHECKED(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
		D3D9CHECKED(dev->DrawPrimitiveUP(D3DPT_LINELIST, 6, vert.data(), sizeof vert[0]));
		D3D9CHECKED(oldstate->Apply());
	}

	if (show_overlay)
	{
		std::string text;
		for (auto &&k : hitboxkeys)
			text.push_back(show_hitboxes & 1 << k.type ? k.dispkey : ' ');
		text.push_back(show_solids ? '-' : ' ');
		text.push_back(show_tiles ? '=' : ' ');
		text.push_back(show_exits ? 'K' : ' ');
		text.push_back(show_loc ? 'L' : ' ');
		text.push_back('\n');
		if (memory.lemeza_spawned)
		{
			LaMulanaMemory::object &lemeza = *memory.lemeza_obj;
			text += strprintf(
				"X:%12.8f %.8x Y:%12.8f %.8x\n",
				lemeza.x, *(unsigned*)&lemeza.x, lemeza.y, *(unsigned*)&lemeza.y);
		}
		else
			text += '\n';
		auto sec = sections.upper_bound(frame);
		if (sec != sections.begin())
			--sec;
		text += strprintf(
			"Frame %7d @%d%s",
			frame_count, frame, sec == sections.end() ? "" : strprintf(" %s:%d", sec->second.data(), frame - sec->first).data());

		font8x12->Add(10, 470, BMFALIGN_BOTTOM | BMFALIGN_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255), text);

		text.clear();
		static const struct { const char *name, *disp, *blank; }
		inputs[] =
		{
			{"f1", "F1 ", ""},
			{"f2", "F2 ", ""},
			{"f3", "F3 ", ""},
			{"f4", "F4 ", ""},
			{"f5", "F5 ", ""},
			{"f6", "F6 ", ""},
			{"f7", "F7 ", ""},
			{"f8", "F8 ", ""},
			{"f9", "F9 ", ""},
			{"ok", "Z", " "},
			{"cancel", "X\n", " \n"},
			{"+m", "+m ", ""},
			{"-m", "-m ", ""},
			{"+s", "+s ", ""},
			{"-s", "-s ", ""},
			{"jump", "Z", " "},
			{"main", "X", " "},
			{"sub", "C", " "},
			{"item", "V", " "},
			{"up", " U", "  "},
			{"down", "D", " "},
			{"left", "<", " "},
			{"right", ">", " "},
		};
		for (auto input : inputs)
			text += KeyPressed(name2vk[input.name]) ? input.disp : input.blank;
		if ((unsigned)memory.rng < 32768)
			for (; currng != memory.rng; rngsteps++)
				currng = currng * 109 + 1021 & 0x7fff;
		text += strprintf("\n\nRNG [%.6d] %5d", rngsteps, memory.rng);
		font8x12->Add(630, 470, BMFALIGN_BOTTOM | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 255, 255, 255), text);
		font8x12->Draw(D3DCOLOR_ARGB(96, 0, 0, 0));
		D3D9CHECKED(oldstate->Apply());
	}

	if (show_exits)
	{
		auto room = memory.getroom();
		if (room)
		{
			static const struct { int idx; float x, y; int align; }
			exits[] = {
				{ 0, 288, 10, BMFALIGN_TOP },
				{ 1, 630, 234, BMFALIGN_RIGHT },
				{ 2, 288, 470, BMFALIGN_BOTTOM },
				{ 3, 10, 234, BMFALIGN_LEFT },
			};
			for (auto i : exits)
			{
				auto exit = room->screens[memory.cur_screen].exits[i.idx];
				font8x12->Add(i.x, i.y, i.align, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%2d,%2d,%2d", exit.zone, exit.room, exit.screen));
			}
			font8x12->Add(288, 234, 0, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%2d,%2d,%2d", memory.cur_zone, memory.cur_room, memory.cur_screen));
			font8x12->Draw(D3DCOLOR_ARGB(96, 0, 0, 0));
			D3D9CHECKED(oldstate->Apply());
		}
	}
}

TAS *tas = nullptr;

void __stdcall TasInit(int apiver)
{
	if (apiver != 2)
		ExitProcess(1);
	tas = new TAS((char*)GetModuleHandle(nullptr) - 0x400000);
}

SHORT __stdcall TasGetKeyState(int nVirtKey)
{
	return tas->KeyPressed(nVirtKey) ? 0x8000 : 0;
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
	return TRUE;
}
