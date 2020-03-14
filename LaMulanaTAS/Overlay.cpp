#include "LaMulanaTAS.h"
#include "resource.h"
#include "d3d9.h"
#include "atlbase.h"

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
    if (extra_overlay)
        if (extra_overlay->ProcessKeys())
            return;
    if (Poll(VK_OEM_6))
    {
        if (pause)
            run = true;
        pause = true;
    }
    if (Poll(VK_OEM_4))
        run = true, ff = false, pause = false;
    if (Poll('P'))
    {
        run = true, ff = true, pause = false;
        memory.setvsync(false);
    }
    if (Poll('O'))
        show_overlay = !show_overlay;
    if (Poll('I'))
    {
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
    if (Poll('U'))
        LoadTAS();
    for (auto &&k : hitboxkeys)
        show_hitboxes ^= Poll(k.vk, true) << k.type;
    if (Poll(VK_OEM_MINUS, true))
        show_solids = !show_solids;
    if (Poll(VK_OEM_PLUS, true))
        show_tiles = (show_tiles + 1) % 3;
    if (Poll('E', true))
        show_exits = !show_exits;
    if (Poll('L', true))
        show_loc = !show_loc;
    if (Poll('G', true))
        hide_game = !hide_game;
    if (Poll('S', true))
        extra_overlay = extra_overlay == shopping_overlay ? nullptr : shopping_overlay;
    if (Poll('O', true))
        extra_overlay = extra_overlay == object_viewer ? nullptr : object_viewer;
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
            HR(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &window_surface));
            HR(dev->StretchRect(overlay_surf, &unscaled_game, window_surface, &display_rect, (D3DTEXTUREFILTERTYPE)*(memory.base + 0x4a326c)));
            HR(dev->EndScene());
            HR(dev->Present(nullptr, nullptr, nullptr, nullptr));
            window_surface = 0;
            // definitely not how to achieve a steady 60 fps but that's not critical here
            target_time += 16;
            int sleep_time = target_time - timeGetTime();
            if (sleep_time > 0)
                Sleep(sleep_time);
            UpdateKeys();
            ProcessKeys();
            HR(dev->BeginScene());
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
        HR(dev->CreateTexture(640, 480, 1, D3DUSAGE_RENDERTARGET, memory.display_format, D3DPOOL_DEFAULT, &overlay, nullptr));
        HR(overlay->GetSurfaceLevel(0, &overlay_surf));
    }

    HR(dev->SetRenderTarget(0, overlay_surf));
    if (hide_game)
        HR(dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 0.f, 0));
    else
        HR(dev->StretchRect(memory.postprocessed_gamesurf, &unscaled_game, overlay_surf, &unscaled_game, D3DTEXF_POINT));

    CComPtr<IDirect3DStateBlock9> oldstate;
    HR(dev->CreateStateBlock(D3DSBT_ALL, &oldstate));

    if (!font4x6)
        font4x6.reset(new BitmapFont(dev, 4, 6, tasModule, MAKEINTRESOURCE(IDB_TOMTHUMB)));
    if (!font8x12)
        font8x12.reset(new BitmapFont(dev, 8, 12, tasModule, MAKEINTRESOURCE(IDB_SMALLFONT)));
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

        HR(dev->CreateRenderTarget(256, 256, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &surf, nullptr));
        for (int i = 0; i < 256; i++)
            font4x6->Add(1.f + (i & 0x0f) * 10.f, 2.f + (i & 0xf0) / 16 * 10.f, 0, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%.2x", i));
        HR(dev->SetRenderTarget(0, surf));
        font4x6->Draw();
        HR(dev->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr));
        HR(surf->LockRect(&rect1, nullptr, D3DLOCK_READONLY));
        HR(tex->LockRect(0, &rect2, nullptr, 0));
        for (int i = 0; i < 256; i++)
            memcpy((char*)rect2.pBits + rect2.Pitch * i, (char*)rect1.pBits + rect1.Pitch * i, min(rect1.Pitch, rect2.Pitch));
        HR(surf->UnlockRect());
        HR(tex->UnlockRect(0));
        hit_hex.tex = tex;
        hit_hex.texel_w = 1.f / 256;
        hit_hex.texel_h = 1.f / 256;
        HR(oldstate->Apply());
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
            bool has_iframes = memory.create_iframes == hitbox.object->create;
            LaMulanaMemory::object &object = has_iframes ? *(LaMulanaMemory::object*)hitbox.object->arg_ptr[0] : *hitbox.object;
#if 0
            font4x6->Add(hitbox.x, hitbox.y - 6.f, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 255, 255, 255),
                strprintf("%.8x %.8x %.8x", hitbox.unk2, hitbox.unk3, hitbox.unk4));
#endif
            switch (type)
            {
            case 0:
            case 3:
                font4x6->Add(hitbox.x, hitbox.y, BMFALIGN_LEFT | BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 0), strprintf("%d", object.hp));
                if (memory.create_pot == object.create && object.arg_int[0])
                {
                    static const char *droptypes[] = { "non", "$$$", "wgt", "shu", "rol", "spr", "flr", "bom", "chk", "ctr", "bul" };
                    int type = object.arg_int[0], quant = object.arg_int[1];
                    std::string typestr = type < sizeof droptypes / sizeof *droptypes ? droptypes[type] : strprintf("%3d", type);
                    font4x6->Add(hitbox.x, hitbox.y + hitbox.h, 0, D3DCOLOR_ARGB(255, 255, 255, 0), strprintf("%s x%d", typestr.data(), quant));
                }
                //font4x6->Add(hitbox.x + hitbox.w, hitbox.y + hitbox.h, BMFALIGN_TOP | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 0, 255, 255), strprintf("%d", object.state));
                if (memory.create_mother5 == object.create)
                {
                    font4x6->Add(hitbox.x, hitbox.y + hitbox.h + 6.f, BMFALIGN_TOP | BMFALIGN_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%d", object.local[9]));
                    font4x6->Add(hitbox.x + hitbox.w, hitbox.y + hitbox.h + 6.f, BMFALIGN_TOP | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%d", object.local[2]));
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
                if (memory.create_drop == object.create)
                    font4x6->Add(hitbox.x, hitbox.y, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 255), strprintf("%d", object.arg_int[1]));
                break;
            }
            if (has_iframes)
                font4x6->Add(hitbox.x, hitbox.y - 6.f, BMFALIGN_BOTTOM, D3DCOLOR_ARGB(255, 0, 255, 255),
                    strprintf("%d %d", hitbox.object->local[1], hitbox.object->local[2]));
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
        HR(dev->SetTexture(0, 0));
        HR(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
        HR(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
        HR(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE));
        HR(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, hv.size() / 3, hv.data(), sizeof *hv.data()));
        font4x6->Draw(D3DCOLOR_ARGB(128, 0, 0, 0));
        HR(oldstate->Apply());
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

        HR(dev->SetTexture(0, hit_tex.tex));
        HR(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1));
        HR(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
        HR(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
        HR(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
        HR(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, i * 4, i * 2, idx.data(), D3DFMT_INDEX16, vert.data(), sizeof vert[0]));
        HR(oldstate->Apply());
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
        HR(dev->SetTexture(0, 0));
        HR(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
        HR(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4 * solids.count, 2 * solids.count, idx.data(), D3DFMT_INDEX32, vert.data(), sizeof vert[0]));
        font4x6->Draw();
        HR(oldstate->Apply());
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
        HR(dev->SetTexture(0, 0));
        HR(dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
        HR(dev->DrawPrimitiveUP(D3DPT_LINELIST, 6, vert.data(), sizeof vert[0]));
        HR(oldstate->Apply());
    }

    if (show_overlay)
    {
        std::string text;
        for (auto &&k : hitboxkeys)
            text.push_back(show_hitboxes & 1 << k.type ? k.dispkey : ' ');
        text.push_back(show_solids ? '-' : ' ');
        text.push_back(show_tiles ? '=' : ' ');
        text.push_back(show_exits ? 'E' : ' ');
        text.push_back(show_loc ? 'L' : ' ');
        text.push_back(hide_game ? 'G' : ' ');
        text.push_back('\n');
        if (memory.lemeza_spawned)
        {
            LaMulanaMemory::object &lemeza = *memory.lemeza_obj;
            int align, remainder;
            if (3.f == lemeza.arg_float[6])
            {
                int a = (int)round(lemeza.x * 10);
                align = (a % 3 + 3) % 3;
                remainder = (int)(0x100'0000 * ((double)lemeza.x - a / 10.));
            }
            else
            {
                int a = (int)round(lemeza.x * 25);
                align = (a % 6 + 6) % 6;
                remainder = (int)(0x200'0000 * ((double)lemeza.x - a / 25.));
            }
            text += strprintf("Align %d%c%05x\n", align, remainder < 0 ? '-' : '+', abs(remainder));
            text += strprintf(
                "X:%12.8f %s Y:%12.8f %s\n",
                lemeza.x, hexfloat(lemeza.x).data(), lemeza.y, hexfloat(lemeza.y).data());
        }
        else
            text += '\n';
        auto sec = sections.upper_bound(frame);
        if (sec != sections.begin())
            --sec;
        text += strprintf(
            "Frame %7d @%d%s",
            frame_count, frame, sec == sections.end() ? "" : strprintf(" %s:%d", sec->second.data(), frame - sec->first).data());

        font8x12->Add(OVERLAY_LEFT, OVERLAY_BOTTOM, BMFALIGN_BOTTOM | BMFALIGN_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255), text);

        text.clear();

        auto disp_pad = [&](unsigned char btn, char *disp, char *blank)
        {
            text += (memory.pad_state[btn] & 1) ? disp : blank;
        };
        auto disp_key = [&](unsigned char vk, char *disp, char *blank)
        {
            text += (memory.key_state[vk].state & 1) ? disp : blank;
        };

        for (int vk = VK_F2; vk <= VK_F9; vk++)
            if (memory.key_state[vk].state & 1)
            {
                text += 'F'; text += (char)('2' + vk - VK_F2);
            }
        text += '\n';
        auto &pad_binds = memory.cur_inputdev < 4 ? (&memory.bindings->dinput)[memory.cur_inputdev] : memory.bindings->xinput;
        disp_pad(pad_binds.prevmain, "-m ", "   ");
        disp_pad(pad_binds.nextmain, "+m ", "");
        disp_pad(pad_binds.prevsub, "-s ", "   ");
        disp_pad(pad_binds.nextsub, "+s ", "");
        disp_pad(pad_binds.msx, "\x89", " ");
        disp_pad(pad_binds.pause, "=", " ");
        disp_pad(pad_binds.prevmenu, "\x87", " ");
        disp_pad(pad_binds.nextmenu, "\x88", "");
        disp_pad(pad_binds.ok, "\x81", " ");
        disp_pad(pad_binds.cancel, "\x82", " ");
        disp_pad(pad_binds.jump, "Z", " ");
        disp_pad(pad_binds.attack, "X", " ");
        disp_pad(pad_binds.sub, "C", " ");
        disp_pad(pad_binds.item, "V", " ");
        text += (memory.pad_dir_state[0] & 1) ? '\x83' : ' ';
        text += (memory.pad_dir_state[2] & 1) ? '\x85' : ' ';
        text += (memory.pad_dir_state[3] & 1) ? '<' : ' ';
        text += (memory.pad_dir_state[1] & 1) ? '>' : ' ';
        text += '\n';
        auto &key_binds = memory.bindings->keys.p;
        disp_key(key_binds.prevmain, "-m ", "   ");
        disp_key(key_binds.nextmain, "+m ", "");
        disp_key(key_binds.prevsub, "-s ", "   ");
        disp_key(key_binds.nextsub, "+s ", "");
        disp_key(key_binds.msx, "\x89", " ");
        disp_key(key_binds.pause, "=", " ");
        disp_key(key_binds.prevmenu, "\x87", " ");
        disp_key(key_binds.nextmenu, "\x88", "");
        disp_key(key_binds.ok, "\x81", " ");
        disp_key(key_binds.cancel, "\x82", " ");
        disp_key(VK_SPACE, "\x81", " ");
        disp_key(VK_BACK, "\x82", " ");
        disp_key(VK_RETURN, "\x81", " ");
        disp_key(VK_ESCAPE, "\x82", " ");
        disp_key(key_binds.jump, "Z", " ");
        disp_key(key_binds.attack, "X", " ");
        disp_key(key_binds.sub, "C", " ");
        disp_key(key_binds.item, "V", " ");
        disp_key(VK_UP, "\x83", " ");
        disp_key(VK_DOWN, "\x85", " ");
        disp_key(VK_LEFT, "<", " ");
        disp_key(VK_RIGHT, ">", " ");
        text += '\n';

        if ((unsigned)memory.rng < 32768)
            for (; currng != memory.rng; rngsteps++)
                currng = currng * 109 + 1021 & 0x7fff;
        text += strprintf("\n\nRNG [%.6d] %5d", rngsteps, memory.rng);
        font8x12->Add(OVERLAY_RIGHT, OVERLAY_BOTTOM, BMFALIGN_BOTTOM | BMFALIGN_RIGHT, D3DCOLOR_ARGB(255, 255, 255, 255), text);
        font8x12->Draw(D3DCOLOR_ARGB(96, 0, 0, 0));
        HR(oldstate->Apply());
    }

    if (show_exits)
    {
        auto room = memory.getroom();
        if (room)
        {
            static const struct { int idx; float x, y; int align; }
            exits[] = {
                { 0, 288, OVERLAY_TOP, BMFALIGN_TOP },
                { 1, OVERLAY_RIGHT, 234, BMFALIGN_RIGHT },
                { 2, 288, OVERLAY_BOTTOM, BMFALIGN_BOTTOM },
                { 3, OVERLAY_LEFT, 234, BMFALIGN_LEFT },
            };
            for (auto i : exits)
            {
                auto exit = room->screens[memory.cur_screen].exits[i.idx];
                font8x12->Add(i.x, i.y, i.align, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%2d,%2d,%2d", exit.zone, exit.room, exit.screen));
            }
            font8x12->Add(288, 234, 0, D3DCOLOR_ARGB(255, 255, 255, 255), strprintf("%2d,%2d,%2d", memory.cur_zone, memory.cur_room, memory.cur_screen));
            font8x12->Draw(D3DCOLOR_ARGB(96, 0, 0, 0));
            HR(oldstate->Apply());
        }
    }

    if (extra_overlay)
    {
        extra_overlay->Draw();
        HR(oldstate->Apply());
    }
}