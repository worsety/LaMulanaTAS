#include "LaMulanaTAS.h"
#include <string>
#include <windows.h>

static const int WIDTH = 18;

void ShoppingOverlay::Draw()
{
    if (memory.obj_queue[27] < 0)
        return;
    int shop_no = 0;
    auto &font8x12 = tas.font8x12;
    float x = OVERLAY_LEFT, y = OVERLAY_TOP;
    for (auto *shop = &memory.objects[(*memory.obj_queue)[27]]; memory.AsObjPtr(shop); shop = shop->queue_next)
    {
        if (shop->create != memory.create_shop)
            continue;
        ++shop_no;

        LaMulanaMemory::object *cursor, *text_obj, *next_obj;
        cursor = (LaMulanaMemory::object*)shop->arg_ptr[1];
        text_obj = (LaMulanaMemory::object*)shop->arg_ptr[2];
        next_obj = &shop[1];

        std::string text;
        auto f = [&text] (const char *name, auto... params)
        {
            text += format_field(WIDTH, name, params...);
        };
        switch (shop->arg_int[0])
        {
            case 0:
                text += "Dialogue\n";
                break;
            case 1:
                text += "Shop\n";
                break;
            case 2:
                text += "Tree\n";
                break;
            default:
                text += "Unk %d\n"; // I think I saw some code for a type 3, not that any exist in the game?
                break;
        }
        f("Card", "%d", shop->arg_int[1]);
        f("Object", "%d", shop->idx);
        switch (shop->local[0])
        {
            case 0:
                text += "Entering\n\n\n\n\n";
                goto objs;
            case 1:
                text += "Chatting\n\n\n\n\n";
                goto objs;
            case 2:
                text += "Left\n\n\n\n\n";
                goto objs;
            case 3:
                break;
            case 1000:
                text += "Leaving\n\n\n\n\n";
            default:
                goto objs;
        }
        if (shop->local[0] != 3)
        {
            text += "\n\n\n\n\n";
            goto objs;
        }

        int slot = shop->local[15] == 0 ? cursor->local[0] : shop->local[14];
        if (IsBadReadPtr(&shop->local[1 + slot], 92))
        {
            text += strprintf("0x%8x\n", slot * 4);
            text += "BAD\n\n\n\n";
        }
        else
        {
            f("Slot", "%c%d", shop->local[15] ? cursor->local[0] == 1 ? '\x81' : '\x82' : ' ', slot);
            f("Item", "%d", shop->local[21 + slot]);
            f("Price", "%d", shop->local[1 + slot]);
            f("Flag", "%d", shop->local[7 + slot]);
            text += shop->local[11 + slot] ? "In stock\n" : "Out of stock\n";
        }
    objs:
        text += "\nCursor\n";
        if (memory.AsObjPtr(cursor))
        {
            f("Object", "%d", cursor->idx);
            f("Type", memory.GetObjName(cursor));
            f("Depth", "%d", cursor->GetDepth());
        }
        else
        {
            text += "BAD\n\n\n";
        }
        text += "\nText\n";
        if (memory.AsObjPtr(text_obj))
        {
            f("Object", "%d", text_obj->idx);
            f("Type", memory.GetObjName(text_obj));
            f("Depth", "%d", text_obj->GetDepth());
        }
        else
        {
            text += "BAD\n\n\n";
        }
        text += "\nNext\n";
        if (memory.AsObjPtr(next_obj))
        {
            f("Object", "%d", next_obj->idx);
            f("Type", memory.GetObjName(next_obj));
            f("Depth", "%d", next_obj->GetDepth());
            f("p[0]", "%d", next_obj->local[0]);
            f("p[1]", "%d", next_obj->local[1]);
            f("p[2]", "%d", next_obj->local[2]);
            f("p[3]", "%d", next_obj->local[3]);
            f("p[4]", "%d", next_obj->local[4]);
        }
        else
        {
            text += "BAD\n\n\n";
        }
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
        x += 8 * (WIDTH + 2);
    }
    font8x12->Draw(D3DCOLOR_ARGB(192, 0, 0, 0));
}