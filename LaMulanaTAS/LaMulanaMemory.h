#pragma once
#include "util.h"
#include "d3d9.h"

class LaMulanaMemory
{
	char *base;
public:
	LaMulanaMemory(char *base_) : base(base_) {}

	union object
	{
		char raw[0x334];
		struct {
			char pad_list[0x2d8];
			int nextidx, previdx; // used for object allocation
			short priority, pad_idx;
			int idx;
			char unk_list[8];
			object *next, *prev; // used for processing order within a priority
		};
		struct {
			bool(*create)(object*);
			void(*update)(object*);
			void(*drawlist)(object*);
			bool(*update_postcoldet)(object*);
			void(*draw)(object*);
			void(*destroy)(object*);
			void(*hitbox)(object*);
			char pad_locals[4];
			// although I'm calling them locals, there is other private storage in objects
			// also arguments passed at construction time go into these three arrays
			int local_int[32];
			void *local_ptr[16];
			char unk_locals[0x80];
			float local_float[32];
			float x, y;
		};
		struct {
			char pad_hp[0x204];
			int hp;
		};
		struct {
			char pad_tex[0x238];
			IDirect3DTexture9 *texture;
			char pad_tex2[0x1c];
			float texel_w, texel_h;
		};
	};

	struct hitbox
	{
		float x, y, w, h;
		int damage, type;
		object *object;
		int unk2, unk3, unk4;
	};

	struct mapid
	{
		char zone, room, screen;
	};

	struct screen
	{
		wchar_t *name;
		unsigned char unk04;
		union
		{
			struct
			{
				mapid up, right, down, left;
			};
			mapid exits[4]; // up, right, down, left
		};
		unsigned char unk11[3];
		int unk14;
	};

	struct room
	{
		unsigned char unk00[4];
		unsigned char unk05[3];
		int unk08[2];
		screen *screens;
		short w, h;
		unsigned char *tiles; // w*h rowwise
	};

	// not 1:1 with fields
	struct zone
	{
		unsigned char unk00[8];
		room *rooms;
		void *unk0c[2];
		int unk14;
	};

	struct map
	{
		int num_zones;
		zone *zones;
	};

	struct bossmap
	{
		int unk00[2];
		room *rooms;
		int unk0c[3];
	};

	struct scrolling {
		float x, y;
		int unk08[6];
	};

	struct solid {
		float x;
		float y;
		float w;
		float h;
		float rotdeg;
		float pivot_x;
		float pivot_y;
		float vel_x;
		float vel_y;
		int unk24;
		object *obj;
	};

	char &kb_enabled = *(base + 0x6D5820);
	HWND &window = *(HWND*)(base + 0xDB6FB8);
	void(*&post_process)() = *(void(**)())(base + 0x6D8F74);

	short &RNG = *(short *)(base + 0x6D6C58);
	int &game_state = *(int*)(base + 0xDB6FD0);
	char &has_quicksave = *(base + 0x6D4B6F);
	short &timeattack_cursor = *(short*)(base + 0x6D2218);
	int &lemeza_spawned = *(int*)(base + 0xDB998C);
	object *&lemeza_obj = *(object**)(base + 0xDB9988);
	unsigned(&flags1)[5] = *(unsigned(*)[5])(base + 0x6D7BC8);
	scrolling(&scroll)[2] = *(scrolling(*)[2])(base + 0xDB7E00);

	char &in_timeattack = *(char*)(base + 0x6D6FDB);
	map &map_main = *(map*)(base + 0xDB70C0);
	map &map_timeattack = *(map*)(base + 0xDB7DD4);
	bossmap *&map_boss = *(bossmap**)(base + 0xDB7DE4);
	short &boss_mapidx = *(short*)(base + 0x6D7BA8);
	short &boss_room = *(short*)(base + 0x6D6FD8);
	unsigned char(&tile_overlay)[64][48] = *(unsigned char(*)[64][48])(base + 0xDB71B8); //colwise
	char &cur_zone = *(char*)(base + 0xDB702E);
	char &cur_room = *(char*)(base + 0xDB7006);
	char &cur_screen = *(char*)(base + 0xDB7007);

	unsigned char &scroll_dbidx = *(unsigned char*)(base + 0xDB70CB);
	scrolling(&scroll_db)[2] = *(scrolling(*)[2])(base + 0xDB7E00);

	unsigned char &solids_dbidx = *(unsigned char*)(base + 0xDB70B2);
	solid*(&solids_db)[2] = *(solid*(*)[2])(base + 0xDB70F4);
	int(&solids_count)[2] = *(int(*)[2])(base + 0xDB70CC);

	void(*const kill_objects)() = (void(*)())(base + 0x607E90);
	void(*const reset_game)() = (void(*)())(base + 0x4D9FB0);

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
			return nullptr;
		return settings + 0x5c;
	}

	IDirect3D9 * id3d9()
	{
		return *(IDirect3D9**)(base + 0xDB9998);
	}

	IDirect3DDevice9 * id3d9dev()
	{
		return *(IDirect3DDevice9**)(base + 0xDB999C);
	}

	void scrub_objects()
	{
		// the game's very bad at actually resetting things, which is related to some of its bugs
		char *objptr = *(char**)(base + 0xDB95D8);
		for (int i = 0; i < 0x600; i++, objptr += 820)
		{
			// see initialisation function at 0x607C90
			memset(objptr, 0, 0x280);
			memset(objptr + 0x288, 0, 0x2d8 - 0x288);
			*(int*)(objptr + 0x2e4) = 0;
			memset(objptr + 0x2e8, 0, 820 - 0x2e8);
		}
	}

	int getspeed()
	{
		return 16 + *(int*)(base + 0xdbb4d4);
	}

	void setspeed(int frameinterval)
	{
		*(int*)(base + 0xdbb4d4) = frameinterval - 16;
	}

	void setvsync(bool vsync)
	{
		UINT presentint = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
		if (((D3DPRESENT_PARAMETERS*)(base + 0xDB6D90))->PresentationInterval != presentint)
		{
			((D3DPRESENT_PARAMETERS*)(base + 0xDB6D90))->PresentationInterval = presentint;
			*(base + 0x6D7BAB) = 0; // force device reset
		}
	}

	void saveslot(int slot)
	{
		// create quick save data
		((void(*)())(base + 0x4846F0))();
		// save quick save data
		((void(*)(const char *savedata, int size, const char *filename))(base + 0x475670))(*(char**)(base + 0x6D7E48), 16384, strprintf("lm_%.2x.sav", slot).data());
	}

	void loadslot(int slot)
	{
		static char fakeloadmenu[512];
		*(int*)(fakeloadmenu + 0x108) = 1;
		*(short*)(fakeloadmenu + 0xf4) = slot / 5;
		*(short*)(fakeloadmenu + 0xe0) = slot % 5;
		kill_objects(); // clears objects
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

	map & getmap()
	{
		return in_timeattack ? map_timeattack : map_main;
	}

	room *getroom()
	{
		if (flags1[1] & 0x400)
			return &map_boss[boss_mapidx].rooms[boss_room];
		else if (cur_zone >= 0)
			return &getmap().zones[cur_zone].rooms[cur_room];
		return nullptr;
	}

	unsigned char gettile_map(int x, int y)
	{
		room *here = getroom();
		return here ? here->tiles[y * here->w + x] : 0;
	}

	unsigned char gettile_effective(int x, int y)
	{
		const auto &scroll = scroll_db[scroll_dbidx];
		int ov_x = x - (int)(scroll.x * 0.1f);
		int ov_y = y - (int)(scroll.y * 0.1f);
		unsigned char overlay = (ov_x >= 0 && ov_x < 64 && ov_y >= 0 && ov_y < 48) ? tile_overlay[ov_x][ov_y] :  0;
		unsigned char maptile = gettile_map(x, y);
		if (flags1[2] & 0x4000 || overlay >= 0x7f || overlay != 0 && maptile < 0x80)
			return overlay;
		return gettile_map(x, y);
	}

	vararray<solid> getsolids()
	{
		return vararray<solid>(solids_db[solids_dbidx^1], solids_count[solids_dbidx^1]);
	}
};