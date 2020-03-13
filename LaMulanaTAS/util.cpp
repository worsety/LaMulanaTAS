#include "util.h"
#include "LaMulanaTAS.h"
#include <cstdarg>
#include <cwchar>
#include <algorithm>
#include "windows.h"

#if _MSC_VER < 1900
#error Visual C++ 2015 required for compliant vsnprintf
#endif

static std::string vstrprintf(_In_z_ _Printf_format_string_ const char * const fmt, std::va_list v)
{
    std::vector<char> buf(64);
    int len = std::vsnprintf(&buf[0], buf.size() - 1, fmt, v);
    if (len + 1 > (int)buf.size())
    {
        buf.resize(len + 1);
        len = std::vsnprintf(&buf[0], buf.size(), fmt, v);
    }
    if (len < 0)
        throw std::runtime_error("vsnprintf failed");
    return std::string(&buf[0], len);
}

static std::wstring vwstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, std::va_list v)
{
    std::vector<wchar_t> buf(64);
    int len = std::vswprintf(&buf[0], buf.size() - 1, fmt, v);
    if (len + 1 >(int)buf.size())
    {
        buf.resize(len + 1);
        len = std::vswprintf(&buf[0], buf.size(), fmt, v);
    }
    if (len < 0)
        throw std::runtime_error("vsnprintf failed");
    return std::wstring(&buf[0], len);
}

std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...)
{
    std::va_list v;
    std::wstring ret;
    va_start(v, fmt);
    ret = vwstrprintf(fmt, v);
    va_end(v);
    return ret;
}

std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...)
{
    std::va_list v;
    std::string ret;
    va_start(v, fmt);
    ret = vstrprintf(fmt, v);
    va_end(v);
    return ret;
}

std::string format_field(int width, const char *name, std::string value)
{
    std::string val = " " + value;
    std::string ret = strprintf("%-*.*s\n", width, width, name);
    int pos = max(1, width - (int)val.size()),
        size = width - pos;
    ret.replace(pos, size, val.c_str(), size);
    return ret;
}

std::string format_field(int width, const char *name, _In_z_ _Printf_format_string_ const char * const fmt, ...)
{
    std::va_list v;
    va_start(v, fmt);
    std::string value = vstrprintf(fmt, v);
    va_end(v);
    return format_field(width, name, value);
}

std::string getwinerror(HRESULT hr)
{
    LPSTR messageBuffer = nullptr;
    size_t size = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, 0, (LPSTR)&messageBuffer, 0, nullptr);
    std::string ret(messageBuffer, size);
    ::LocalFree(messageBuffer);
    return ret;
};

std::string getwinerror()
{
    return getwinerror(::GetLastError());
};

HRESULT hrcheck(HRESULT hr, const char *file, int line, const char *code)
{
    if (SUCCEEDED(hr))
        return hr;
    throw std::runtime_error(strprintf("%s:%d %s\n%s", file, line, code, getwinerror(hr).data()));
};

int rounduppo2(int x)
{
    x--;
    x |= x >> 16;
    x |= x >> 8;
    x |= x >> 4;
    x |= x >> 2;
    x |= x >> 1;
    return x + 1;
}

char *read_file_or_res(const char *name, LPCTSTR resName, LPCTSTR type, size_t *size)
{
    FILE *f;
    if (!fopen_s(&f, name, "rb"))
    {
        fseek(f, 0, SEEK_END);
        *size = ftell(f);
        char *ret = (char*)malloc(*size + 1);
        ret[*size] = 0;
        if (ret)
        {
            char *p = ret;
            fseek(f, 0, SEEK_SET);
            while (auto read = fread(p, 1, *size, f))
                p += read, size -= read;
        }
        fclose(f);
        return ret;
    }

    auto resource = FindResource(tasModule, resName, type);
    if (resource)
    {
        auto handle = LoadResource(tasModule, resource);
        *size = SizeofResource(tasModule, resource);
        GLE(handle);
        void *data = LockResource(handle);
        char *ret = (char*)malloc(*size + 1);
        if (!ret)
            return nullptr;
        ret[*size] = 0;
        memcpy(ret, data, *size);
        return ret;
    }
    return nullptr;
}

BitmapFont::BitmapFont(IDirect3DDevice9* dev, int w, int h) : dev(dev), char_w(w), char_h(h)
{
    int tex_w = rounduppo2(w * 16), tex_h = rounduppo2(h * 16);
    char_texw = 1.f * w / tex_w;
    char_texh = 1.f * h / tex_h;
    HR(dev->CreateTexture(tex_w, tex_h, 1, 0, D3DFMT_A8, D3DPOOL_MANAGED, &tex, nullptr));
}

BitmapFont::BitmapFont(IDirect3DDevice9* dev, int w, int h, HMODULE mod, LPCTSTR res) : BitmapFont(dev, w, h)
{
    DIBSECTION dib;
    unique_handle bmp(::LoadImage(mod, res, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION), ::DeleteObject);
    GLE(bmp);
    GLE(::GetObject(bmp.get(), sizeof dib, &dib));

    D3DLOCKED_RECT rect;
    HR(tex->LockRect(0, &rect, nullptr, 0));
    unsigned char *srcline = (unsigned char*)dib.dsBm.bmBits + dib.dsBm.bmWidthBytes * (dib.dsBm.bmHeight - 1);
    unsigned char *dstline = (unsigned char*)rect.pBits;
    const int bpp = dib.dsBm.bmBitsPixel;
    for (int y = h * 16; --y >= 0; srcline -= dib.dsBm.bmWidthBytes, dstline += rect.Pitch)
        for (int x = 0; x < dib.dsBm.bmWidth; x++)
        {
            if (bpp == 1)
                dstline[x] = srcline[x >> 3] & 0x80 >> (x & 7) ? 255 : 0;
            else if (bpp == 4)
                dstline[x] = (srcline[x >> 1] & 2 >> (x & 1)) * 17;
            else if (bpp == 8)
                dstline[x] = srcline[x];
            else if (bpp == 16)
            {
                int rgb555 = ((short*)srcline)[x];
                dstline[x] = (int)round(2.74 * ((rgb555 & 31) + (rgb555 >> 5 & 31) + (rgb555 >> 10 & 31)));
            }
            else if (bpp == 24)
                dstline[x] = (srcline[x * 3] + srcline[x * 3 + 1] + srcline[x * 3 + 2] + 1) / 3;
            else if (bpp == 32)
                dstline[x] = (srcline[x * 4] + srcline[x * 4 + 1] + srcline[x * 4 + 2] + 1) / 3;
        }
    HR(tex->UnlockRect(0));
}

std::vector<std::string> split(const std::string &text, char delim) {
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;
    while ((end = text.find(delim, start)) != std::string::npos) {
        tokens.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    tokens.push_back(text.substr(start));
    return tokens;
}

void BitmapFont::Add(float x, float y, int align, D3DCOLOR color_odd, D3DCOLOR color_even, const std::string &text, bool skip_spaces)
{
    std::vector<std::string> lines = split(text, '\n');

    if (vert.size () < (chars + text.size()) * 4)
        vert.resize((chars + text.size()) * 4);
    auto line = lines.begin();
    float draw_x = x, draw_y = y;
    if (align & BMFALIGN_RIGHT)
        draw_x -= char_w * line++->size();
    if (align & BMFALIGN_BOTTOM)
        draw_y -= lines.size() * char_h;
    bool odd = true;
    D3DCOLOR color = color_odd;
    for (unsigned char c : text)
    {
        switch (c)
        {
            case '\n':
                draw_y += char_h, draw_x = x;
                odd = !odd;
                color = odd ? color_odd : color_even;
                if (align & BMFALIGN_RIGHT)
                    draw_x -= char_w * line++->size();
                continue;
            case ' ':
                if (skip_spaces)
                {
                    draw_x += char_w;
                    continue;
                }
            default:
                for (int j = 0; j < 4; j++)
                {
                    vert[4 * chars + j].x = draw_x + !!(j & 1) * char_w - 0.5f;
                    vert[4 * chars + j].y = draw_y + !!(j & 2) * char_h - 0.5f;
                    vert[4 * chars + j].u = (c & 15) * char_texw + !!(j & 1) * char_texw;
                    vert[4 * chars + j].v = (c >> 4) * char_texh + !!(j & 2) * char_texh;
                    vert[4 * chars + j].z = 0;
                    vert[4 * chars + j].w = 1;
                    vert[4 * chars + j].color = color;
                }
                draw_x += char_w, chars++;
        }
    }

    if (index.size() < chars * 6)
    {
        int oldidx = index.size() / 6;
        index.resize(chars * 6);
        for (size_t i = oldidx; i < chars; i++)
            for (int j = 0; j < 6; j++)
                index[6 * i + j] = 4 * i + (j >= 1 && j <= 3) + 2 * (j >= 2 && j <= 4);
    }
}

void BitmapFont::Draw(D3DCOLOR backcolor)
{
    if (!chars)
        return;
    HR(dev->SetFVF(fvf));
    HR(dev->SetTexture(0, tex));
    HR(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    HR(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
    if (backcolor)
    {
        HR(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_LERP));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_LERP));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG0, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG0, D3DTA_TEXTURE));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
        HR(dev->SetRenderState(D3DRS_TEXTUREFACTOR, backcolor));
    }
    else
    {
        HR(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE));
        HR(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
        HR(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTOP_SELECTARG1));
    }
    HR(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, chars * 4, chars * 2, index.data(), D3DFMT_INDEX32, vert.data(), sizeof *vert.data()));
    chars = 0;
}
