#include "LaMulanaTAS.h"
#include <vector>
#include <functional>

enum
{
    ROLL_DROP = 0,
    ROLL_FAIRY,
};

bool RNGOverlay::ProcessKeys()
{
    bool ret = false;
    if (tas.Poll(VK_RIGHT, true, TAS::POLL_REPEAT))
    {
        ret = true;
        if (++mode >= (int)modes.size())
            mode = 0;
        condition = 0;
    }
    if (tas.Poll(VK_LEFT, true, TAS::POLL_REPEAT))
    {
        ret = true;
        if (--mode < 0)
            mode = modes.size() - 1;
        condition = 0;
    }
    if (tas.Poll(VK_DOWN, true, TAS::POLL_REPEAT))
    {
        ret = true;
        if (++condition >= (int)modes[mode].conditions.size())
            condition = 0;
    }
    if (tas.Poll(VK_UP, true, TAS::POLL_REPEAT))
    {
        ret = true;
        if (--condition < 0)
            condition = modes[mode].conditions.size() - 1;
    }
    return ret;
}

struct coin
{
    int value, dx, dy;
    coin(int value, int dx, int dy) : value(value), dx(dx), dy(dy) {}
};

enum
{
    DROP_TYPE_MIXED = 0,
    DROP_TYPE_MONEY, // 1
    DROP_TYPE_ITEM, // 2-10
    DROP_TYPE_SOUL, // 12
};

enum
{
    DROP_MONEY = 0,
    DROP_ITEM,
    DROP_SOUL,
    DROP_NOTHING,
    DROP_MASK = 7,
};

// TODO test everything
static int get_drop(short rng, int type, int &count, int soul, std::vector<coin> &coins, bool luck)
{
    int dropping = DROP_NOTHING;
    coins.clear();
    switch (type)
    {
        case DROP_TYPE_SOUL:
            if (soul <= 0)
                break;
            if (!count) // yes, count means something different here
                dropping = DROP_SOUL;
            else
                switch (roll(rng, 10))
                {
                    case 0:
                    case 1:
                    case 5:
                        if (!luck)
                            break;
                    case 3:
                    case 8:
                        dropping = DROP_SOUL;
                        break;
                    default:
                        ;
                }
        case DROP_TYPE_MONEY:
        case DROP_TYPE_ITEM:
            if (count != 0)
            {
                dropping = type == DROP_TYPE_MONEY ? DROP_MONEY : DROP_ITEM;
                break;
            }
            if (luck)
                switch (roll(rng, 10))
                {
                    case 4:
                    case 6:
                    case 9:
                        dropping = DROP_SOUL;
                        if (soul > 0)
                            break;
                    default:
                        dropping = type == DROP_TYPE_MONEY ? DROP_MONEY : DROP_ITEM;
                }
            else
                switch (roll(rng, 10))
                {
                    case 3:
                    case 8:
                        dropping = type == DROP_TYPE_MONEY ? DROP_MONEY : DROP_ITEM;
                        break;
                    case 1:
                    case 5:
                    case 9:
                        dropping = DROP_SOUL;
                        if (soul > 0)
                            break;
                    default:
                        dropping = DROP_NOTHING;
                }
            break;
        case DROP_TYPE_MIXED:
            if (luck)
                switch (roll(rng, 10))
                {
                    case 3:
                    case 5:
                    case 7:
                        dropping = DROP_ITEM;
                        break;
                    case 0:
                    case 1:
                    case 8:
                        dropping = DROP_SOUL;
                        if (soul > 0)
                            break;
                    default:
                        dropping = DROP_MONEY;
                }
            else
                switch (roll(rng, 10))
                {
                    case 3:
                        switch (roll(rng, 10))
                        {
                            case 1:
                            case 4:
                            case 6:
                            case 8:
                                dropping = DROP_ITEM;
                                break;
                            default:
                                dropping = DROP_MONEY;
                        }
                        break;
                    case 1:
                    case 4:
                    case 5:
                    case 8:
                        dropping = DROP_SOUL;
                        if (soul > 0)
                            break;
                    default:
                        dropping = DROP_NOTHING;
                }
            break;
    }
    switch (dropping)
    {
        case DROP_MONEY:
        {
            short yrng = roll(rng, 32767); // but y though
            if (count <= 0 && roll(rng, 2))
                count = 2;
            else
                count = max(count, 1);
            if (luck)
                count *= 5;
            coins.clear();
            for (int n = count; n;)
            {
                int value;
                if (n > 20)
                    value = 20;
                else if (n > 10)
                    value = 10;
                else  if (n > 5)
                    value = 5;
                else  if (n > 2)
                    value = 2;
                else
                    value = 1;
                n -= value;
                static int xvels[] = { 2, 0, 6, 4, 1, 6, 2, 0, 4, 1 };
                yrng = (yrng * 109 + 1021) % 32767; // is this even intentional??? how do you do this by accident?
                int dy = 3 + 2 * (yrng % 3);
                int dx = xvels[roll(rng, 10)];
                dx *= roll(rng, 2) ? 1 : -1;
                coins.emplace_back(value, dx, dy);
            }
            break;
        }
        case DROP_ITEM:
            count = luck ? 3 : 1;
            break;
        case DROP_SOUL:
            if (soul >= 30)
                count = 0;
            else
                switch (roll(rng, 9))
                {
                    case 0:
                    case 5:
                    case 8:
                        count = -1;
                        break;
                    case 1:
                    case 3:
                    case 6:
                        count = +1;
                        break;
                    default:
                        count = 0;
                }
            break;
        default:
            count = 0;
            break;
    }
    return dropping;
}

static RNGOverlay::Result roll_drop(short rng, int type, int count, int soul, bool luck)
{
    RNGOverlay::Result ret;
    std::vector<coin> coins;
    ret.push_back(ROLL_DROP);
    ret.push_back(get_drop(rng, type, count, soul, coins, luck));
    ret.push_back(count);
    ret.push_back(coins.size());
    for (auto &&coin : coins)
    {
        ret.push_back(coin.value);
        ret.push_back(coin.dx);
        ret.push_back(coin.dy);
    }
    return ret;
}

RNGOverlay::Result::operator std::string()
{
    std::string ret;
    switch (at(0))
    {
        case ROLL_DROP:
            switch (at(1))
            {
                case DROP_MONEY:
                    ret = strprintf("%d coin%s ", at(2), at(2) > 1 ? "s" : "");
                    for (int i = 0; i < at(3); i++)
                        ret += strprintf("(%d,%d,%d) ", at(4 + i * 3), at(5 + i * 3), at(6 + i * 3));
                    ret.resize(ret.size() - 1);
                    break;
                case DROP_SOUL:
                    ret = "soul";
                    if (at(2) > 0)
                        ret += " (high roll)";
                    else if (at(2) < 0)
                        ret += " (low roll)";
                    break;
                case DROP_ITEM:
                    ret = strprintf("%dx item%c", at(2), at(2) > 1 ? "s" : "");
                    break;
                default:
                    ret = "nothing";
            }
            break;
        case ROLL_FAIRY:
            switch (at(1))
            {
                case 3:
                    return "key";
                case 2:
                    return "treasure";
                case 1:
                    return "weapon";
                default:
                    return "healing";
            }
    }
    return ret;
}

static bool is_money(const RNGOverlay::Result &result, int count = -1)
{
    return ROLL_DROP == result[0] && DROP_MONEY == result[1] && (-1 == count || count == result[2]);
}

static bool is_item(const RNGOverlay::Result &result)
{
    return ROLL_DROP == result[0] && DROP_ITEM == result[1];
}

static bool is_soul(const RNGOverlay::Result &result)
{
    return ROLL_DROP == result[0] && DROP_SOUL == result[1];
}

static bool coin_dir(const RNGOverlay::Result &result, int ref, bool dir)
{
    if (ROLL_DROP != result[0] || DROP_MONEY != result[1])
        return false;
    for (int i = 0; i < result[3]; i++)
        if (dir && result[5 + i * 3] < ref || !dir && result[5 + i * 3] > ref)
            return false;
    return true;
}

static RNGOverlay::Result roll_fairy(short rng)
{
    RNGOverlay::Result ret;
    int fairy_type;
    ret.push_back(ROLL_FAIRY);
    switch (roll(rng, 20))
    {
        case 1:
            fairy_type = 3;
            break;
        case 2:
        case 9:
        case 13:
            fairy_type = 2;
            break;
        case 4:
        case 11:
        case 17:
            fairy_type = 1;
            break;
        default:
            fairy_type = 0;
    }
    ret.push_back(fairy_type);
    return ret;
}

RNGOverlay::RNGOverlay(TAS &tas) : Overlay(tas)
{
    using namespace std::placeholders;
    modes.push_back(Mode{ "Drop (enemy, mixed)", std::bind(roll_drop, _1, DROP_TYPE_MIXED, 0, 5, false),
        {{"2 coins", std::bind(is_money, _1, 2)}, {"1 coin", std::bind(is_money, _1, 1)},
        {"weight", is_item}, {"soul", is_soul}}
    });
    modes.push_back(Mode{ "Drop (enemy, money)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 0, 5, false),
        {{ "2 coins", std::bind(is_money, _1, 2) }, { "1 coin", std::bind(is_money, _1, 1) },
        { "soul", is_soul }}
    });
    modes.push_back(Mode{ "Drop (enemy, item)", std::bind(roll_drop, _1, DROP_TYPE_ITEM, 0, 5, false),
        {{ "item", is_item }, { "soul", is_soul }}
    });
    modes.push_back(Mode{ "Drop (empty pot)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 0, 0, false),
        {{"2 coins", std::bind(is_money, _1, 2)}, {"1 coin", std::bind(is_money, _1, 1)}}
    });
    modes.push_back(Mode{ "Drop (10 coins)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 10, 0, false),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (20 coins)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 20, 0, false),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (50 coins)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 50, 0, false),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (80 coins)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 80, 0, false),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (100 coins)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 100, 0, false),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (enemy, mixed, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MIXED, 0, 5, true),
        {{"10 coins", std::bind(is_money, _1, 10)}, {"5 coins", std::bind(is_money, _1, 5)},
        {"weight", is_item}, {"soul", is_soul}}
    });
    modes.push_back(Mode{ "Drop (enemy, money, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 0, 5, true),
        {{"10 coins", std::bind(is_money, _1, 10)}, {"5 coins", std::bind(is_money, _1, 5)},
        { "soul", is_soul }}
    });
    modes.push_back(Mode{ "Drop (enemy, item, lucky)", std::bind(roll_drop, _1, DROP_TYPE_ITEM, 0, 5, true),
        {{ "item", is_item }, { "soul", is_soul }}
    });
    modes.push_back(Mode{ "Drop (empty pot, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 0, 0, true),
        {{"10 coins", std::bind(is_money, _1, 10)}, {"5 coins", std::bind(is_money, _1, 5)}}
    });
    modes.push_back(Mode{ "Drop (10 coins, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 10, 0, true),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (20 coins, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 20, 0, true),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (50 coins, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 50, 0, true),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (80 coins, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 80, 0, true),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Drop (100 coins, lucky)", std::bind(roll_drop, _1, DROP_TYPE_MONEY, 100, 0, true),
        {{ "all left", std::bind(coin_dir, _1, 0, false) }, { "all right", std::bind(coin_dir, _1, 0, true) }}
    });
    modes.push_back(Mode{ "Fairy", std::bind(roll_fairy, _1), {
        { "key", [](const Result &res) { return 3 == res[1]; } },
        { "treasure", [](const Result &res) { return 2 == res[1]; } },
        { "weapon", [](const Result &res) { return 1 == res[1]; } },
        { "healing", [](const Result &res) { return 0 == res[1]; } },
    }});
}

void RNGOverlay::Draw()
{
    auto &font8x12 = tas.font8x12;
    float x = OVERLAY_LEFT, y = OVERLAY_TOP;
    std::string text;
    short rng = memory.rng;

    text = strprintf("%s want %s\n", modes[mode].name, modes[mode].conditions[condition].name);
    text += linewrap(modes[mode].roll(rng), 78, true);
    text += "\n";
    font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
    for (auto pos = -1; -1 != (pos = text.find('\n', pos + 1));)
        y += 12;
    text.clear();

    int rows = (MAIN_OVERLAY_TOP - (int)y) / 12;
    auto &roll = modes[mode].roll;
    auto &test = modes[mode].conditions[condition].test;
    int steps = -20;
    advance_rng(rng, steps);
    unsigned r = 0, w = 0;
    for (; steps < 32768; advance_rng(rng, 1), steps++)
    {
        if (test(roll(rng)))
        {
            std::string str = strprintf("%+d", steps);
            if (str.size() * 8 + x > OVERLAY_RIGHT)
                r = rows, steps = 32768;
            else
                text += str + "\n", w = max(w, str.size()), r++;
        }
        if (r == rows)
        {
            if (rows & 1)
                font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 191, 191, 191), D3DCOLOR_ARGB(255, 255, 255, 255), text);
            else
                font8x12->Add(x, y, BMFALIGN_LEFT | BMFALIGN_TOP, D3DCOLOR_ARGB(255, 255, 255, 255), D3DCOLOR_ARGB(255, 191, 191, 191), text);
            text.clear();
            x += (w + 1) * 8;
            r = 0, w = 0;
        }
    }
    font8x12->Draw(D3DCOLOR_ARGB(192, 0, 0, 0));
}
