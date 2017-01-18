// LaMulanaTAS.cpp : Defines the exported functions for the DLL application.
//

#include "LaMulanaTAS.h"
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
#include "d3d9.h"

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
esc,f1-f9
*/

static const struct { off_t ptr_offset, count_offset; } hitbox_ptrs[] =
{
	{ 0x6D785C, 0x6D7168 },
	{ 0x6D7248, 0x6D718C },
	{ 0x6D78E8, 0x6D7348 },
	{ 0x6D7E70, 0x6D6F70 },
	{ 0x6D78F8, 0x6D6FC4 },
	{ 0x6D7164, 0x6D739C },
	{ 0x6D7894, 0x6D73A0 },
	{ 0x6D7E78, 0x6D726C },
	{ 0x6D72E4, 0x6D7C08 },
	{ 0x6D7398, 0x6D7270 },
	{ 0x6D7A1C, 0x6D7E74 },
	{ 0x6D734C, 0x6D78EC }, // this one's definitely always empty but including it in the list anyway for now
	{ 0x6D7188, 0x6D7A0C },
};

class LaMulanaMemory
{
public:
	struct hitbox
	{
		float x, y, w, h;
		int unk1, type;
		void *object;
		int unk2, unk3, unk4;
	};
	char *base;
	LaMulanaMemory(char *base_) : base(base_) {}

	/*
	jump, main, sub, item
	ok,cancel,?,?
	f1,esc,+m,-m
	+s,-s,?,?
	-menu,+menu,up,right
	down,left,esc,f2
	f3,f4,f5,f6
	f7,f8,f9

	Unsure why there are two bound to esc by default, they must have different functions but idk what
	*/
	unsigned char * KeyMappings()
	{
		unsigned char *settings = *(unsigned char**)(base + 0xDB6F14);
		if (!settings)
			return NULL;
		return settings + 0x5c;
	}

	short * RNG()
	{
		return (short *)(base + 0x6D6C58);
	}

	IDirect3D9 * id3d9()
	{
		return *(IDirect3D9**)(base + 0xDB9998);
	}

	IDirect3DDevice9 * id3d9dev()
	{
		return *(IDirect3DDevice9**)(base + 0xDB999C);
	}

	void saveslot(int slot)
	{
		std::ostringstream s;
		s << "lm_" << std::setbase(16) << std::setprecision(2) << slot << ".sav";
		((void(*)())(base + 0x4846F0))();
		((void(*)(const char *savedata, int size, const char *filename))(base + 0x475670))(*(char**)(base + 0x6D7E48), 16384, s.str().data());
	}

	void loadslot(int slot)
	{
		static char fakeloadmenu[512];
		*(int*)(fakeloadmenu + 0x108) = 1;
		*(short*)(fakeloadmenu + 0xf4) = slot / 5;
		*(short*)(fakeloadmenu + 0xe0) = slot % 5;
		((void(*)(void))(base + 0x607E90))(); // clears objects
		((void(*)(void*))(base + 0x44A960))(fakeloadmenu); // loads save, intended to be called from menu code but it only references these three fields
	}

	int gethitboxcount(int type)
	{
		return *(int*)(base + hitbox_ptrs[type].count_offset);
	}

	hitbox * gethitboxes(int type)
	{
		return *(hitbox**)(base + hitbox_ptrs[type].ptr_offset);
	}
};

class TAS
{
public:
	LaMulanaMemory memory;
	int frame;
	std::map<int, std::unordered_set<int>> frame_inputs;
	std::map<int, std::list<std::function<void()>>> frame_actions;
	using frame_iter = std::map<int, std::unordered_set<int>>::iterator;
	std::map<std::string, int> name2vk;

	struct keystate {
		unsigned char held : 1;
		unsigned char pressed : 1;
		unsigned char released : 1;
	} keys[256];

	void UpdateKeys()
	{
		BYTE keystate[256];
		GetKeyboardState(keystate);
		// there's a very efficient way to do this with an xor but whatever, legibility
		for (int vk = 0; vk < 256; vk++)
		{
			bool held = !!(keystate[vk] & 0x80);
			keys[vk].pressed = held && !keys[vk].held;
			keys[vk].released = !held && keys[vk].held;
			keys[vk].held = held;
		}
	}

	bool running, resetting, show_overlay, hide_game;
	unsigned show_hitboxes;

	TAS(char *base);
	bool KeyPressed(int vk);
	void IncFrame();
	void Overlay();

	void LoadTAS();
};

TAS::TAS(char *base) : memory(base), frame(-1), running(true), show_overlay(true), hide_game(false), show_hitboxes(1 << 7 | 1 << 9 | 1 << 11)
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
	try {
		f.open("script.txt", std::ios::in | std::ios::binary);
	}
	catch (std::exception&)
	{
		MessageBox(NULL, L"Couldn't load script.txt", L"TAS error", MB_OK);
		return;
	}

	int curframe = 0;
	size_t p = 0;
	int linenum = 0;
	std::string line, token;
	std::ostringstream reason;
	frame_inputs.clear();
	frame_inputs.emplace(0, std::unordered_set<int>());
	frame_actions.clear();

	std::map<std::string, int> marks;

	std::regex re_atframe("@([0-9]+)"), re_addframes("\\+([0-9]+)"), re_inputs("([0-9]*)=((\\^?[-+a-z0-9]+)(,(\\^?[-+a-z0-9]+))*)"),
		re_goto("goto=([0-9]+)"), re_load("load=([0-9]+)"), re_save("save=([0-9]+)"), re_rng("rng(=([0-9]+))?([+-][0-9]+)?"),
		re_mark("([a-zA-Z0-9]+):"), re_markrel("([a-zA-Z0-9]+):([0-9]+)");
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
						reason << "Undefined mark " << m[1] << " on line " << linenum;
						throw parsing_exception(reason.str());
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
							reason << "Unknown input \"" << input << "\" on line " << linenum;
							throw parsing_exception(reason.str());
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
						int rng = seed >= 0 ? seed : *memory.RNG(), steps = stepcount;
						for (; steps > 0; --steps)
							rng = rng * 109 + 1021 & 0x7fff;
						for (; steps < 0; ++steps)
							rng = (rng + 31747) * 2405 & 0x7fff;
						*memory.RNG() = rng;
					});
					continue;
				}
				if ("pause" == token)
				{
					frame_actions.emplace(curframe, std::list<std::function<void()>>());
					frame_actions.find(curframe)->second.push_back([this]() { running = false; });
					continue;
				}
				reason << "Unrecognised expression '" << token << "' on line " << linenum;
				throw parsing_exception(reason.str());
			}
		}
	}
	catch (parsing_exception& ex)
	{
		MessageBoxA(NULL, ex.what(), "TAS parsing error", MB_OK);
	}
}

bool TAS::KeyPressed(int vk)
{
	if (resetting)
		return false;
	auto iter = --frame_inputs.upper_bound(frame);
	return frame_inputs.end() != iter && 0 != iter->second.count(vk)
		|| *(memory.base + 0x6D5820) && !!(GetKeyState(vk) & 0x8000);
}


void TAS::IncFrame()
{
	do
	{
		if (GetForegroundWindow() != *(HWND*)(memory.base + 0xDB6FB8))
			continue;
		UpdateKeys();

		if (keys[VK_OEM_6].pressed)
		{
			running = false;
			break;
		}
		if (keys['O'].pressed)
			show_overlay = !show_overlay;
		static const struct {
			int vk, type;
		} hitboxkeys[] =
		{
			{'1', 0},
			{'2', 1},
			{'3', 2},
			{'4', 3},
			{'5', 4},
			{'6', 5},
			{'7', 6},
			{'8', 8},
			{'9', 10},
			{'0', 12},
		};
		for (auto k : hitboxkeys)
			show_hitboxes ^= keys[k.vk].pressed << k.type;
		if (keys[VK_OEM_MINUS].pressed)
			show_hitboxes = 1 << 7 | 1 << 9 | 1 << 11;
		if (keys[VK_OEM_PLUS].pressed)
			show_hitboxes = -1;
		if (keys[VK_BACK].pressed)
			hide_game = !hide_game;
		if (keys['P'].pressed)
		{
			running = true;
			if (*(int*)(memory.base + 0xdbb4d4) != -16)
			{
				((D3DPRESENT_PARAMETERS*)(memory.base + 0xDB6D90))->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
				*(memory.base + 0x6D7BAB) = 0; // force device reset
				*(int*)(memory.base + 0xdbb4d4) = -16;
			}
		}
		if (keys[VK_OEM_4].pressed)
		{
			running = true;
			if (*(int*)(memory.base + 0xdbb4d4) != 0)
			{
				((D3DPRESENT_PARAMETERS*)(memory.base + 0xDB6D90))->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
				*(memory.base + 0x6D7BAB) = 0; // force device reset
				*(int*)(memory.base + 0xdbb4d4) = 0;
			}
		}
		if (keys['R'].pressed)
			LoadTAS();
		if (keys['T'].pressed) {
			LoadTAS();
			((void(*)(void))(memory.base + 0x607E90))(); // clear object processing lists
			// zero objects, the game's very bad at actually resetting things, which is related to some of its bugs
			char *objptr = *(char**)(memory.base + 0xDB95D8);
			for (int i = 0; i < 0x600; i++, objptr += 820)
			{
				// see initialisation function at 0x607C90
				memset(objptr, 0, 0x280);
				memset(objptr + 0x288, 0, 0x2d8 - 0x288);
				*(int*)(objptr + 0x2e4) = 0;
				memset(objptr + 0x2e8, 0, 820 - 0x2e8);
			}
			((void(*)())(memory.base + 0x4D9FB0))(); // perform a normal reset
			*(memory.base + 0x6D4B6F) = 0; // reset quick save
			*(short*)(memory.base + 0x6D2218) = -1; // reset time attack cursor
			frame = -2;
			running = resetting = true;
		}
	} while (!running && *(int*)(memory.base + 0xDB6FD0) != 5);

	frame++;
	auto iter = frame_actions.find(frame);
	if (iter != frame_actions.end())
		for (auto x : iter->second)
			x();
	// P toggles frame limiter

	if (frame >= 0)
		resetting = false;
}

// If I could use D3DX this would be soooo much easier and faster
// Intentionally avoiding setting any unnecessary state so we don't have to restore it
// Only works if filtering is off
void TAS::Overlay()
{
	IDirect3DDevice9 *dev = memory.id3d9dev();

	if (hide_game)
		dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 0.f, 0);
	struct hitboxvec
	{
		float x, y, z, w;
		D3DCOLOR color;
	};
	std::vector<hitboxvec> hv;
	for (int type = 0; type <= 12; type++)
	{
		if (!(show_hitboxes & 1 << type))
			continue;
		LaMulanaMemory::hitbox *hitbox = memory.gethitboxes(type);
		int count = memory.gethitboxcount(type);
		int i = hv.size();
		hv.resize(i + count * 6);
		for (; count > 0; count--, hitbox++)
		{
			for (int vert = 0; vert < 6; vert++, i++)
			{
				int right = vert >= 1 && vert <= 3;
				int down = vert >= 2 && vert <= 4;
				hv[i].x = hitbox->x + right * hitbox->w - 0.5f;
				hv[i].y = hitbox->y + down * hitbox->h - 0.5f;
				hv[i].z = 0;
				hv[i].w = 1;
				switch (type)
				{
				case 0: // lemeza
				case 3: // enemy hitbox
					hv[i].color = D3DCOLOR_ARGB(128, 0, 255, 0);
					break;
				case 1: // lemeza's weapons
				case 8: // divine retribution
				case 10: // spikes
					hv[i].color = D3DCOLOR_ARGB(128, 255, 0, 0);
					break;
				case 4: // enemy hurtbox
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
		DWORD oldfvf, olddestblend;
		dev->SetTexture(0, 0);
		dev->GetRenderState(D3DRS_DESTBLEND, &olddestblend);
		dev->GetFVF(&oldfvf);
		dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, hv.size() / 3, hv.data(), sizeof *hv.data());
		dev->SetFVF(oldfvf);
		dev->SetRenderState(D3DRS_DESTBLEND, olddestblend);
	}

	(*(void(**) (void))(memory.base + 0x6D8F74))();

	if (!show_overlay) return;

	std::wostringstream os;
	float x = *(int*)(memory.base + 0xDB998C) ? (*(float**)(memory.base + 0xDB9988))[0x1E0 / 4] : 0;
	os << "X:" << std::setw(12) << std::setprecision(8) << std::fixed << x;
	os << " " << std::setbase(16) << std::setw(8) << *(unsigned*)&x << std::endl;
	os << "Frame " << std::setbase(10) << std::setw(7) << frame << " RNG " << std::setw(5) << *memory.RNG();

	IDirect3DSurface9 *surface = NULL;
	IDirect3DVertexBuffer9 *vbuf = NULL;
	IDirect3DTexture9 *text_tex = NULL;
	HDC dc = NULL;
	D3DLOCKED_RECT locked;
	struct vertex
	{
		float x, y, z, w;
		D3DCOLOR color;
		float u, v;
	} v[4];

	RECT textbox{ 0, 0, 400, 40 };

	static HFONT font;
	if (!font)
		font = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_RASTER_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, FIXED_PITCH, TEXT("Lucida Console"));


	dev->CreateTexture(textbox.right, textbox.bottom, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &text_tex, NULL);
	dev->SetTexture(0, text_tex);

	text_tex->GetSurfaceLevel(0, &surface);
	surface->GetDC(&dc);
	SelectObject(dc, font);
	SetTextColor(dc, 0xfffff);
	SetBkMode(dc, TRANSPARENT);
	if (!DrawText(dc, os.str().data(), -1, &textbox, DT_NOPREFIX | DT_LEFT | DT_BOTTOM))
		return;
	surface->ReleaseDC(dc);
	surface->Release();

	text_tex->LockRect(0, &locked, NULL, 0);
	for (int y = 0; y < textbox.bottom; y++)
		for (int x = 0; x < textbox.right; x++)
		{
			DWORD &pixel = *(DWORD*)((char*)locked.pBits + y * locked.Pitch + x * 4);
			pixel = pixel << 8 | 0xffffff;
		}
	text_tex->UnlockRect(0);
	text_tex->Release();

	memset(v, 0, sizeof(vertex) * 4);
	D3DVIEWPORT9 viewport;
	dev->GetViewport(&viewport);
	for (int i = 0; i < 4; i++)
	{
		int right = i & 1 ^ (i >> 1);
		int down = i >> 1;
		v[i].x = 10 + right * textbox.right * 2 - 0.5f;
		v[i].y = viewport.Height - (1 - down) * textbox.bottom * 2 - 10.5f;
		v[i].u = 1.f * right;
		v[i].v = 1.f * down;
		v[i].w = 1;
		v[i].color = D3DCOLOR_ARGB(224, 255, 255, 255);
	}
	dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof *v);
}

TAS *tas = NULL;

void __fastcall TASInit(char *base)
{
	tas = new TAS(base);
}

SHORT WINAPI TASGetKeyState(_In_ int nVirtKey)
{
	return tas->KeyPressed(nVirtKey) ? 0x8000 : 0;
}

DWORD WINAPI TASOnFrame(void)
{
	tas->IncFrame();
	return timeGetTime();
}

void TASRender(void)
{
	tas->Overlay();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}