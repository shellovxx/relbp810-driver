// LBP-810 paper table (CAP1K.DLL resource 200/100) keyed by CNLB810.GPD option names.
#pragma once
#include <cstdint>
#include <cstring>

namespace paper {

enum Units : int { kMil = 1000, kTenthMm = 254 };  // 1/1000 inch, 1/10 mm

struct PaperInfo {
    const char* keyword;   // GPD PaperSize option name
    int16_t dmPaper;       // DEVMODE paper id used by Canon (570 = index card)
    uint8_t code;          // D0A0 byte 4
    int units;
    int width, height;     // in `units`
    int printW, printH;    // printable area, 300 dpi dots
    int left, top;         // printable origin, 300 dpi dots
};

constexpr PaperInfo kPapers[] = {
    { "LETTER",      1,   0x0D, kMil,     8500, 11000, 2432, 3180, 59, 60 },
    { "LEGAL",       5,   0x0C, kMil,     8500, 14000, 2432, 4080, 59, 60 },
    { "A4",          9,   0x02, kTenthMm, 2100, 2970,  2368, 3388, 56, 60 },
    { "B5",          13,  0x07, kTenthMm, 1820, 2570,  2016, 2915, 67, 60 },
    { "EXECUTIVE",   7,   0x0A, kMil,     7250, 10500, 2048, 3030, 63, 60 },
    { "ENV_DL",      27,  0x18, kTenthMm, 1100, 2200,  1184, 2478, 58, 60 },
    { "ENV_10",      20,  0x16, kMil,     4125, 9500,  1120, 2730, 60, 60 },
    { "ENV_C5",      28,  0x15, kTenthMm, 1620, 2290,  1792, 2585, 60, 60 },
    { "ENV_MONARCH", 37,  0x17, kMil,     3875, 7500,  1056, 2130, 54, 60 },
    { "INDEX3X5",    570, 0x40, kMil,     3000, 5000,  768,  1380, 66, 60 },
};

inline const PaperInfo* FindByKeyword(const char* key)
{
    if (!key)
        return nullptr;
    for (auto& p : kPapers)
        if (!strcmp(p.keyword, key))
            return &p;
    return nullptr;
}

inline const PaperInfo* FindByDmPaper(int dm)
{
    for (auto& p : kPapers)
        if (p.dmPaper == dm)
            return &p;
    return nullptr;
}

// Canon converts with truncation (CAP1K sub_411970 = a*b/c).
inline int ToDots(int v, int units, int dpi) { return v * dpi / units; }

// Toner density slider positions -2..+2 map to these values (CAP1UIK sub_6A90E2B1).
inline uint8_t DensityFromKeyword(const char* key)
{
    if (key) {
        if (!strcmp(key, "LIGHT2")) return 0;
        if (!strcmp(key, "LIGHT1")) return 15;
        if (!strcmp(key, "DARK1")) return 47;
        if (!strcmp(key, "DARK2")) return 63;
    }
    return 31;
}

} // namespace paper
