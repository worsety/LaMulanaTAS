#include "util.h"
#include <cstdarg>
#include <cwchar>
#include <algorithm>
#include "windows.h"

#if _MSC_VER < 1900
#error Visual C++ 2015 required for compliant vsnprintf
#endif
std::string strprintf(_In_z_ _Printf_format_string_ const char * const fmt, ...)
{
	std::vector<char> buf(64);
	std::va_list v;
	va_start(v, fmt);
	int len = std::vsnprintf(&buf[0], buf.size() - 1, fmt, v);
	va_end(v);
	if (len + 1 > (int)buf.size())
	{
		buf.resize(len + 1);
		va_start(v, fmt);
		len = std::vsnprintf(&buf[0], buf.size(), fmt, v);
		va_end(v);
	}
	if (len < 0)
		throw std::runtime_error("vsnprintf failed");
	return std::string(&buf[0], len);
}

std::wstring wstrprintf(_In_z_ _Printf_format_string_ const wchar_t * const fmt, ...)
{
	std::vector<wchar_t> buf(64);
	std::va_list v;
	va_start(v, fmt);
	int len = std::vswprintf(&buf[0], buf.size() - 1, fmt, v);
	va_end(v);
	if (len + 1 >(int)buf.size())
	{
		buf.resize(len + 1);
		va_start(v, fmt);
		len = std::vswprintf(&buf[0], buf.size(), fmt, v);
		va_end(v);
	}
	if (len < 0)
		throw std::runtime_error("vsnprintf failed");
	return std::wstring(&buf[0], len);
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

HRESULT d3d9check(HRESULT hr, const char *file, int line, const char *code)
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

BitmapFont::BitmapFont(IDirect3DDevice9* dev, int w, int h) : dev(dev), char_w(w), char_h(h)
{
	int tex_w = rounduppo2(w * 16), tex_h = rounduppo2(h * 8);
	char_texw = 1.f * w / tex_w;
	char_texh = 1.f * h / tex_h;
	D3D9CHECKED(dev->CreateTexture(tex_w, tex_h, 1, 0, D3DFMT_A8, D3DPOOL_MANAGED, &tex, nullptr));
}

BitmapFont::BitmapFont(IDirect3DDevice9* dev, int w, int h, HMODULE mod, int res) : BitmapFont(dev, w, h)
{
	DIBSECTION dib;
	unique_handle bmp(::LoadImage(mod, MAKEINTRESOURCE(res), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION), ::DeleteObject);
	WINCHECKED(bmp);
	WINCHECKED(::GetObject(bmp.get(), sizeof dib, &dib));

	D3DLOCKED_RECT rect;
	D3D9CHECKED(tex->LockRect(0, &rect, nullptr, 0));
	unsigned char *srcline = (unsigned char*)dib.dsBm.bmBits + dib.dsBm.bmWidthBytes * (dib.dsBm.bmHeight - 1);
	unsigned char *dstline = (unsigned char*)rect.pBits;
	const int bpp = dib.dsBm.bmBitsPixel;
	for (int y = h * 8; --y >= 0; srcline -= dib.dsBm.bmWidthBytes, dstline += rect.Pitch)
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
	D3D9CHECKED(tex->UnlockRect(0));
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

void BitmapFont::Add(float x, float y, int align, D3DCOLOR color, const std::string &text)
{
	std::vector<std::string> lines = split(text, '\n');
	if (align & BMFALIGN_RIGHT)
	{
		x -= std::max_element(lines.begin(), lines.end(), [](std::string &left, std::string &right) { return left.size() < right.size(); })->size() * char_w;
	}
	if (align & BMFALIGN_BOTTOM) {
		std::vector<std::string> lines = split(text, '\n');
		y -= lines.size() * char_h;
	}

	if (vert.size () < (chars + text.size()) * 4)
		vert.resize((chars + text.size()) * 4);
	float draw_x = x, draw_y = y;
	for (int c : text)
	{
		if (c == '\n')
		{
			draw_y += char_h, draw_x = x;
			continue;
		}
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
	D3D9CHECKED(dev->SetFVF(fvf));
	D3D9CHECKED(dev->SetTexture(0, tex));
	D3D9CHECKED(dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
	D3D9CHECKED(dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
	if (backcolor)
	{
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_LERP));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_LERP));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG0, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG0, D3DTA_TEXTURE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
		D3D9CHECKED(dev->SetRenderState(D3DRS_TEXTUREFACTOR, backcolor));
	}
	else
	{
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
		D3D9CHECKED(dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTOP_SELECTARG1));
	}
	D3D9CHECKED(dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, chars * 4, chars * 2, index.data(), D3DFMT_INDEX32, vert.data(), sizeof *vert.data()));
	chars = 0;
}
