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

class LaMulanaMemory
{
public:
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
		void(*lm_saveslot)() = (void(*)())(base + 0x484F00);
		__asm
		{
			mov esi, [slot];
			call[lm_saveslot]
		}
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

	bool k_ff, k_step, k_run, k_reset, k_reload;
	bool running, resetting;

	TAS(char *base);
	bool KeyPressed(int vk);
	void IncFrame();
	void Overlay();

	void LoadTAS();
};

TAS::TAS(char *base) : memory(base), frame(-1), running(true)
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

	std::regex re_atframe("@([0-9]*)"), re_addframes("\\+([0-9]*)"), re_inputs("([0-9]*)=((\\^?[-+a-z0-9]+)(,(\\^?[-+a-z0-9]+))*)"),
		re_goto("goto=([0-9]+)"), re_load("load=([0-9]+)"), re_save("save=([0-9]+)"), re_rng("rng=([0-9]+)(-([0-9]+))?");
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
					int rng = std::stoi(m[1]);
					int rewind = 0;
					if (m[3].length())
						rewind = std::stoi(m[3]);
					while (rewind--)
						rng = (rng + 31747) * 2405 & 0x7fff;
					frame_actions.find(curframe)->second.push_back([this, rng]() { *memory.RNG() = rng; });
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
		bool step = !!(GetKeyState(VK_OEM_6) & 0x8000);
		if (!k_step && step)
		{
			running = false;
			k_step = step;
			break;
		}
		k_step = step;
		bool ff = !!(GetKeyState('P') & 0x8000);
		bool run = !!(GetKeyState(VK_OEM_4) & 0x8000);
		bool reload = !!(GetKeyState('R') & 0x8000);
		bool reset = !!(GetKeyState('T') & 0x8000);
		if (!k_ff && ff)
		{
			running = true;
			((D3DPRESENT_PARAMETERS*)(memory.base + 0xDB6D90))->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
			*(memory.base + 0x6D7BAB) = 0; // force device reset
			*(int*)(memory.base + 0xdbb4d4) = -15;
		}
		if (!k_run && run)
		{
			running = true;
			((D3DPRESENT_PARAMETERS*)(memory.base + 0xDB6D90))->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			*(memory.base + 0x6D7BAB) = 0; // force device reset
			*(int*)(memory.base + 0xdbb4d4) = 0;
		}
		if (!k_reload && reload)
			LoadTAS();
		if (!k_reset && reset) {
			*(int*)(memory.base + 0xDB6FD0) = 7;
			frame = -2;
			running = resetting = true;
		}
		k_ff = ff; k_run = run; k_reload = reload; k_reset = reset;
	} while (!running && *(int*)(memory.base + 0xDB6FD0) != 5);

	frame++;
	auto iter = frame_actions.find(frame);
	if (iter != frame_actions.end())
		for (auto x : iter->second)
			x();
	// P toggles 16x

	if (frame == 0)
		resetting = false;
}

// If I could use D3DX this would be soooo much easier and faster
// Intentionally avoiding setting any unnecessary state so we don't have to restore it
// Only works if filtering is off
void TAS::Overlay()
{
	(*(void(**) (void))(memory.base + 0x6D8F74))();

	std::wostringstream os;
	os << "Frame " << std::setw(7) << frame << " RNG " << std::setw(5) << *memory.RNG();

	IDirect3DDevice9 *dev = memory.id3d9dev();
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
	} *v;

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
	if (!DrawText(dc, os.str().data(), -1, &textbox, DT_NOPREFIX | DT_SINGLELINE | DT_LEFT | DT_BOTTOM))
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

	dev->CreateVertexBuffer(sizeof(vertex) * 4, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPOOL_MANAGED, &vbuf, NULL);
	dev->SetStreamSource(0, vbuf, 0, sizeof *v);
	vbuf->Lock(0, 0, (void**)&v, 0);
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
	vbuf->Unlock();
	vbuf->Release();
	dev->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);
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