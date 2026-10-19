// Sample image of the "Quality" page: the monochrome rendering CAP1UIK.DLL shows for the chosen
// halftoning, brightness and contrast (sub_6A91537D and helpers, ported 1:1, including its
// buffer layout, so that the pixels match the original).
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace ui {

// LCG of the Microsoft C runtime (rand); the original uses MSVCRT's per-thread generator.
struct CrtRand {
    uint32_t seed = 1;
    int Next()
    {
        seed = seed * 214013u + 2531011u;
        return int((seed >> 16) & 0x7FFF);
    }
};

// dib: 24-bit BITMAPINFOHEADER + bits (resource 208). mode: 0 = smoothing, 1 = pattern 1,
// 2 = pattern 2. brightness/contrast: 0..100, 50 = neutral (Canon's stored values).
// Returns WORD-aligned rows, top to bottom, 1 = white, as CreateBitmap(w, h, 1, 1) expects.
std::vector<uint8_t> HalftonePreview(const BITMAPINFOHEADER* dib, int mode, int brightness, int contrast,
                                     CrtRand& rng);

} // namespace ui
