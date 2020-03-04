#pragma once
#include <string>
#include <vector>
#include <sal.h>
#include <memory>
#include <exception>
#include <d3d9.h>
#include <atlbase.h>

std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...);
std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...);
std::string format_field(int width, const char *name, const std::string value);
std::string format_field(int width, const char *name, _In_z_ _Printf_format_string_ const char * const fmt, ...);

template<typename T> class vararray
{
public:
    T* ptr;
    size_t count;
    vararray(T* ptr, size_t count) : ptr(ptr), count(count) {}
    T* begin() {
        return ptr;
    }
    T* end() {
        return ptr + count;
    }
};

std::string getwinerror();
HRESULT hrcheck(HRESULT hr, const char *file, int line, const char *code);
#ifdef NDEBUG
#define GLE(x) (x)
#define HR(x) (x)
#else
#define GLE(x) ((x) ? (void)0 : throw std::runtime_error(strprintf("%s:%d %s\n%s", __FILE__, __LINE__, #x, getwinerror().data())))
#define HR(x) (hrcheck((x), __FILE__, __LINE__, #x))
#endif

char *read_file_or_res(const char *name, LPCTSTR res, LPCTSTR type, size_t *size);

using unique_handle = std::unique_ptr < std::remove_pointer<HANDLE>::type, decltype(&::CloseHandle)>;
using shared_handle = std::shared_ptr < std::remove_pointer<HANDLE>::type>;

struct xyzrhwdiff
{
    float x, y, z, w;
    D3DCOLOR color;
};
struct xyzrhwdiffuv
{
    float x, y, z, w;
    D3DCOLOR color;
    float u, v;
};

#define BMFALIGN_LEFT   0
#define BMFALIGN_TOP    0
#define BMFALIGN_RIGHT  1
#define BMFALIGN_BOTTOM 0x10

class BitmapFont
{
public:
    BitmapFont(IDirect3DDevice9* dev, int w, int h, HMODULE mod, LPCTSTR res);
    void Add(float x, float y, int align, D3DCOLOR color, D3DCOLOR color2, const std::string &text, bool skip_spaces = true);
    void Add(float x, float y, int align, D3DCOLOR color, const std::string &text, bool skip_spaces = true)
    {
        Add(x, y, align, color, color, text, skip_spaces);
    }
    void Draw(D3DCOLOR backcolor = D3DCOLOR_ARGB(0,0,0,0));

private:
    BitmapFont(IDirect3DDevice9* dev, int w, int h);

    static const DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    using vertex = xyzrhwdiffuv;
    CComPtr<IDirect3DTexture9> tex;
    IDirect3DDevice9 *dev;
    int char_w, char_h;
    float char_texw, char_texh;
    std::vector<vertex> vert;
    std::vector<UINT> index;
    size_t chars;
};
