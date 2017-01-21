#pragma once
#include "util.h"
#include "d3d9.h"

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
		((void(*)())(base + 0x4846F0))();
		((void(*)(const char *savedata, int size, const char *filename))(base + 0x475670))(*(char**)(base + 0x6D7E48), 16384, strprintf("lm_%.2x.sav", slot).data());
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

	vararray<hitbox> gethitboxes(int type)
	{
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
		return vararray<hitbox>(*(hitbox**)(base + hitbox_ptrs[type].ptr_offset), *(int*)(base + hitbox_ptrs[type].count_offset));
	}
};