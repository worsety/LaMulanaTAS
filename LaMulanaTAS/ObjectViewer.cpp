#include "LaMulanaTAS.h"

bool ObjectViewer::ProcessKeys()
{
    bool ret = false;
    if (tas.Poll(VK_LEFT, true, TAS::POLL_REPEAT))
    {
        if (--mode < MODE_MIN)
            mode = MODE_MAX;
        if (mode >= 0)
            obj = memory.AsObjPtr(&memory.objects[(*memory.obj_queue)[mode]]);
        ret = true;
    }
    if (tas.Poll(VK_RIGHT, true, TAS::POLL_REPEAT))
    {
        if (++mode > MODE_MAX)
            mode = MODE_MIN;
        if (mode >= 0)
            obj = memory.AsObjPtr(&memory.objects[(*memory.obj_queue)[mode]]);
        ret = true;
    }
    // scrolling is delayed until overlay display because objects may be created or destroyed before then
    scroll = 0;
    if (tas.Poll(VK_UP, true, TAS::POLL_REPEAT))
    {
        scroll = -1;
        ret = true;
    }
    if (tas.Poll(VK_DOWN, true, TAS::POLL_REPEAT))
    {
        scroll = 1;
        ret = true;
    }
    return ret;
}

void ObjectViewer::Draw()
{
    // The majority of space will be taken up by the arguments and locals
    // Fortunately there are half as many pointers as other arguments because they need more space
    // Since there are 32 rows of text allocated for the additional overlay, using columns simplifies the layout
    auto &font8x12 = tas.font8x12;
    float x = OVERLAY_LEFT, y = OVERLAY_TOP;
    int width;
    std::string text;

    if (obj && mode >= 0 && (!obj->extant || obj->priority != mode))
        mode = STACK;

    switch (mode)
    {
        case STACK:
            if (!obj)
                obj = &memory.objects[0];
            text = "Alloc stack\n";
            break;
        default:
            if (!obj)
                obj = memory.AsObjPtr(&memory.objects[(*memory.obj_queue)[mode]]);
            text = strprintf("Priority %d\n", mode);
    }

    if (obj && scroll)
    {
        LaMulanaMemory::object *newobj;
        switch (mode)
        {
            case STACK:
                newobj = scroll > 0 ? obj->AllocNext() : obj->AllocPrev();
                if (scroll > 0 && newobj->extant > obj->extant
                    || scroll < 0 && newobj->extant < obj->extant)
                    newobj = nullptr;
                break;
            default:
                newobj = scroll > 0 ? obj->queue_next : obj->queue_prev;
        }
        if (newobj)
            obj = newobj;
    }

    if (obj)
    {
        auto *first = obj, *last = obj;
        switch (mode)
        {
            case STACK:
                if (obj)
                for (int n = 1; n < 7;)
                {
                    if (first->AllocPrev()->extant >= first->extant)
                        first = first->AllocPrev(), n++;
                    if (last->AllocNext()->extant <= last->extant)
                        last = last->AllocNext(), n++;
                }
                break;
            default:
                for (int n = 1; n < 7;)
                {
                    if (!first->queue_prev && !last->queue_next)
                        break;
                    if (first->queue_prev)
                        first = first->queue_prev, n++;
                    if (last->queue_next)
                        last = last->queue_next, n++;
                }
        }
        for (auto *o = first; o;)
        {
            text += strprintf("%3x %2d %+5d %3s\n%16.16s\n", o->idx, o->priority, o->GetDepth(), o == obj ? "<--" : "", memory.GetObjName(o).data());
            if (o == last)
                break;
            switch (mode)
            {
                case STACK:
                    o = o->AllocNext();
                    break;
                default:
                    o = o->queue_next;
            }
        }
    }
    font8x12->Add(x, MAIN_OVERLAY_TOP - 15 * 12, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
    text.clear();

    if (obj)
    {
        // main data
        auto f = [&text, &width](const char *name, auto... params)
        {
            text += format_field(width, name, params...);
        };
        width = 16;
        {
            f("Object", "%3x %+5d", obj->idx, obj->GetDepth());
            f("", "%s", memory.GetObjName(obj).data());
            f("ID", "%d", obj->unique_id);
            int p = -1;
            for (auto *o = obj; o; o = o->queue_prev)
                p++;
            f("Priority", "%d:%d", obj->priority, p);
            f("Run", "%5s %4s", ~obj->processing_flags & 2 ? "Pause" : "", ~obj->processing_flags & 1 ? "Lamp" : "");
            f("Loc", "%02d-%02d-%02d", obj->zone, obj->scene, obj->screen);
            f("", "%7.2f %7.2f\n", obj->x, obj->y);
            f("", "%7.2f %7.2f", obj->prev_x, obj->prev_y);
            f("", "%7.2f %7.2f", obj->scene_x, obj->scene_y);
            f("", "%7.2f %7.2f", obj->screen_x, obj->screen_y);
            f("HP", "%d", obj->hp);
            f("State", "%d", obj->state);
            f("Frame", "%d", obj->anim_frame);
            font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
            text.clear();
        }
        x += 17 * 8; // 17
        x += 14 * 8; // 31
        for (auto x : obj->local)
            text += strprintf("%11d\n", x);
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
        text.clear();
        x += 12 * 8; // 43
        for (auto x : obj->arg_int)
            text += strprintf("%11d\n", x);
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
        text.clear();
        x += 12 * 8; // 55
        for (auto x : obj->arg_float)
            text += format_float(x, 13) + "\n";
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
        text.clear();
        x += 15 * 8; // 70
        for (auto x : obj->arg_ptr)
        {
            auto pobj = memory.AsObjPtr(x);
            int offset = (int)x - (int)memory.base;
            if (pobj)
                text += strprintf("  obj %3x\n%9.9s\n", pobj->idx, memory.GetObjName(pobj).data());
            else if (!x)
                text += "     null\n\n";
            else if (offset >= 0x401000 && offset < 0xdbc000)
                text += strprintf("+%08x\n\n", offset);
            else
                text += strprintf(" %08x\n\n", x);
        }
        font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
        text.clear();
    }
    font8x12->Draw(D3DCOLOR_ARGB(192, 0, 0, 0));
}