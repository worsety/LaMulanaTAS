#include "LaMulanaTAS.h"
#include <string>
#include <windows.h>

void ShoppingOverlay::Draw()
{
    if (memory.obj_queue[27] < 0)
        return;
    int shop_no = 0;
    auto &font8x12 = tas.font8x12;
    float x = OVERLAY_LEFT, y = OVERLAY_TOP;
    for (auto *shop = &memory.objects[(*memory.obj_queue)[27]]; shop; shop = shop->queue_next)
    {
        if (shop->create != memory.create_shop)
            continue;
        ++shop_no;

        LaMulanaMemory::object *cursor, *text_obj, *next_obj;
        cursor = (LaMulanaMemory::object*)shop->local_ptr[1];
        text_obj = (LaMulanaMemory::object*)shop->local_ptr[2];
        next_obj = &shop[1];

        std::string text;
        switch (shop->local_int[0])
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
        text += strprintf("Card %6d\n", shop->local_int[1]);
        text += strprintf("Object %4d\n", shop->idx);
        switch (shop->private_int[0])
        {
            case 0:
                text += "Entering\n\n\n\n";
                goto objs;
            case 1:
                text += "Chatting\n\n\n\n";
                goto objs;
            case 2:
                text += "Left\n\n\n\n";
                goto objs;
            case 3:
                break;
            case 1000:
                text += "Leaving\n\n\n\n";
            default:
                goto objs;
        }
        if (shop->private_int[0] != 3)
        {
            text += "\n\n\n\n";
            goto objs;
        }

        int slot = shop->private_int[15] == 0 ? cursor->private_int[0] : shop->private_int[14];
        if (IsBadReadPtr(&shop->private_int[1 + slot], 92))
        {
            text += strprintf("0x%8x\n", slot * 4);
            text += "BAD\n\n\n";
        }
        else
        {
            text += strprintf("Slot  %c%4d\n", shop->private_int[15] ? cursor->private_int[0] == 1 ? '\x81' : '\x82' : ' ', slot);
            text += strprintf("Item %6d\n", shop->private_int[21 + slot]);
            text += strprintf("Price %c%4d\n", shop->private_int[11 + slot] ? '\x81' : '\x82', shop->private_int[1 + slot]);
            text += strprintf("Flag %6d\n", shop->private_int[7 + slot]);
        }
    objs:
        text += "\nCursor\n";
        if (memory.IsObjPtr(cursor))
        {
            text += strprintf("Object %4d\n", cursor->idx);
            text += strprintf("Type %6x\n", (char*)cursor->create - memory.base);
            text += strprintf("Depth %5d\n", cursor->GetDepth());
        }
        else
        {
            text += "BAD\n\n\n";
        }
        text += "\nText\n";
        if (memory.IsObjPtr(text_obj))
        {
            text += strprintf("Object %4d\n", text_obj->idx);
            text += strprintf("Type %6x\n", (char*)text_obj->create - memory.base);
            text += strprintf("Depth %5d\n", text_obj->GetDepth());
        }
        else
        {
            text += "BAD\n\n\n";
        }
        text += "\nNext\n";
        if (memory.IsObjPtr(next_obj))
        {
            text += strprintf("Object %4d\n", next_obj->idx);
            text += strprintf("Type %6x\n", (char*)next_obj->create - memory.base);
            text += strprintf("Depth %5d\n", next_obj->GetDepth());
            text += strprintf("p[0] %6d\n", next_obj->private_int[0]);
            text += strprintf("p[1] %6d\n", next_obj->private_int[1]);
        }
        else
        {
            text += "BAD\n\n\n";
        }
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), text);
        x += 8 * 13;
    }
    font8x12->Draw(D3DCOLOR_ARGB(96, 0, 0, 0));
}