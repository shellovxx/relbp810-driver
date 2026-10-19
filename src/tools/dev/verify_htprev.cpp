// Development check (x86 only): compares our Quality page sample renderer (htpreview.cpp) with
// the original one inside CAP1UIK.DLL (sub_6A9152E0) for every halftoning mode and a grid of
// brightness/contrast values.
// usage: verify_htprev <path of CAP1UIK.DLL>
#include <windows.h>
#include <cstdio>
#include <vector>
#include "../../driver/ui/htpreview.h"

typedef HBITMAP(__stdcall* RenderFn)(HGLOBAL res, int mode, unsigned short brightness, unsigned short contrast);

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        printf("usage: verify_htprev <CAP1UIK.DLL>\n");
        return 2;
    }
    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) {
        printf("cannot load %ls (%lu)\n", argv[1], GetLastError());
        return 1;
    }
    auto render = reinterpret_cast<RenderFn>(reinterpret_cast<BYTE*>(m) + 0x152E0);
    auto crtSrand = reinterpret_cast<void(__cdecl*)(unsigned)>(GetProcAddress(GetModuleHandleW(L"msvcrt.dll"), "srand"));
    HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(208), RT_BITMAP);
    HGLOBAL res = r ? LoadResource(m, r) : nullptr;
    auto* bi = static_cast<const BITMAPINFOHEADER*>(LockResource(res));
    if (!bi || !crtSrand) {
        printf("resource 208 or msvcrt!srand missing\n");
        return 1;
    }
    const int w = bi->biWidth, h = bi->biHeight;
    const int stride = 2 * ((((w + 7) >> 3) + 1) >> 1);
    const int values[] = { 0, 1, 10, 25, 33, 49, 50, 51, 66, 75, 90, 99, 100 };
    int cases = 0, bad = 0;
    for (int mode = 0; mode < 3; mode++) {
        for (int b : values) {
            for (int c : values) {
                unsigned seed = 1 + unsigned(cases) * 7919u;
                crtSrand(seed);
                HBITMAP hb = render(res, mode, (unsigned short)b, (unsigned short)c);
                std::vector<BYTE> ref(size_t(stride) * h);
                LONG got = hb ? GetBitmapBits(hb, LONG(ref.size()), ref.data()) : 0;
                if (hb)
                    DeleteObject(hb);
                ui::CrtRand rng;
                rng.seed = seed;
                std::vector<uint8_t> ours = ui::HalftonePreview(bi, mode, b, c, rng);
                int diff = 0;
                if (got != LONG(ref.size()) || ours.size() != ref.size()) {
                    diff = -1;
                } else {
                    // compare visible pixels only
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            int i = y * stride + x / 8, bit = 0x80 >> (x & 7);
                            if ((ref[i] & bit) != (ours[i] & bit))
                                diff++;
                        }
                }
                cases++;
                if (diff) {
                    bad++;
                    if (bad <= 20)
                        printf("mode %d brightness %d contrast %d: %d pixels differ\n", mode, b, c, diff);
                }
            }
        }
    }
    printf("%d/%d cases identical\n", cases - bad, cases);
    return bad ? 1 : 0;
}
