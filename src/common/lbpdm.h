// Private part of the driver's DEVMODE (the "OEM extra" of the plug-in pair CNLB810R/CNLB810U).
// Both plug-ins must report and convert it identically, so the logic lives here.
// Settings that exist as GPD features (paper size, orientation, resolution, toner density, toner
// save, image refinement) are kept by Unidrv; this block holds everything else the Canon UI has.
#pragma once
#include <windows.h>
#include <cstring>
#include <cwchar>

namespace lbpdm {

constexpr DWORD kSignature = 'L810';
constexpr DWORD kVersion = 0x00020000;

// Halftone modes, numbered like CAP1UIK.
enum : WORD { HT_SMOOTH = 0, HT_PATTERN1 = 1, HT_PATTERN2 = 2 };

constexpr int kMinScale = 10, kMaxScale = 200;   // CAP1UIK spin range
constexpr int kMinAdjust = -80, kMaxAdjust = 80;   // brightness / contrast

#pragma pack(push, 4)
struct Extra {
    // OEM_DMEXTRAHEADER
    DWORD dwSize;
    DWORD dwSignature;
    DWORD dwVersion;
    // "Page Setup"
    WORD outputPaper;      // 0 = same as the page size, else DMPAPER_*
    WORD nup;              // pages per sheet: 1, 2, 4, 8, 9
    WORD nupOrder[4];      // page order of the 2-, 4-, 8- and 9-up layouts, as CAP1UIK keeps them:
                           // 2-up 0 = normal, 1 = reversed; others bit 0 = start at the right,
                           // bit 1 = columns first (0 across from left, 1 across from right,
                           // 2 down from left, 3 down from right)
    WORD manualScale;      // "Manual scaling" checked
    WORD scale;            // manual scaling, percent
    // "Finishing"
    WORD ignoreMismatch;   // print even if a paper size mismatch is detected
    WORD emfSpool;         // spool in EMF format
    // "Quality"
    WORD halftone;
    SHORT brightness;      // -80..80, even (CAP1UIK keeps (value + 100) / 2)
    SHORT contrast;
    // Overlay
    WORD overlayFront;     // 0 = background, 1 = foreground
    WORD overlayInch;      // units shown in the dialog: 0 = cm, 1 = inch
    LONG overlayLeft, overlayTop, overlayWidth, overlayHeight;   // 0.01 mm
    WCHAR overlayFile[MAX_PATH];
    // Profile ("Configuration") last selected
    WCHAR profile[64];
};
#pragma pack(pop)

inline void SetDefaults(Extra& e)
{
    memset(&e, 0, sizeof(e));
    e.dwSize = sizeof(Extra);
    e.dwSignature = kSignature;
    e.dwVersion = kVersion;
    e.nup = 1;
    e.scale = 100;
    e.halftone = HT_PATTERN1;
    e.overlayWidth = 100;
    e.overlayHeight = 100;
}

inline void Validate(Extra& e)
{
    e.dwSize = sizeof(Extra);
    e.dwSignature = kSignature;
    e.dwVersion = kVersion;
    if (e.nup != 1 && e.nup != 2 && e.nup != 4 && e.nup != 8 && e.nup != 9)
        e.nup = 1;
    if (e.nupOrder[0] > 1)
        e.nupOrder[0] = 0;
    for (int i = 1; i < 4; i++)
        if (e.nupOrder[i] > 3)
            e.nupOrder[i] = 0;
    e.manualScale = e.manualScale ? 1 : 0;
    if (e.scale < kMinScale || e.scale > kMaxScale)
        e.scale = 100;
    e.ignoreMismatch = e.ignoreMismatch ? 1 : 0;
    e.emfSpool = e.emfSpool ? 1 : 0;
    if (e.halftone > HT_PATTERN2)
        e.halftone = HT_PATTERN1;
    if (e.brightness < kMinAdjust || e.brightness > kMaxAdjust)
        e.brightness = 0;
    if (e.contrast < kMinAdjust || e.contrast > kMaxAdjust)
        e.contrast = 0;
    e.overlayFront = e.overlayFront ? 1 : 0;
    e.overlayInch = e.overlayInch ? 1 : 0;
    e.overlayFile[MAX_PATH - 1] = 0;
    e.profile[63] = 0;
}

// Copies a possibly older/smaller block of ours onto defaults.
inline void Import(Extra& out, const void* in, DWORD cbIn)
{
    Extra e;
    SetDefaults(e);
    const auto* h = static_cast<const Extra*>(in);
    if (in && cbIn >= 3 * sizeof(DWORD) && h->dwSignature == kSignature) {
        DWORD n = h->dwSize < cbIn ? h->dwSize : cbIn;
        if (n > sizeof(Extra))
            n = sizeof(Extra);
        if (n > 3 * sizeof(DWORD))
            memcpy(reinterpret_cast<BYTE*>(&e) + 3 * sizeof(DWORD), reinterpret_cast<const BYTE*>(in) + 3 * sizeof(DWORD),
                   n - 3 * sizeof(DWORD));
    }
    Validate(e);
    out = e;
}

// IPrintOemCommon::DevMode for OEMDMPARAM-like parameter blocks (printoem.h).
enum { kDmSize = 1, kDmDefault = 2, kDmConvert = 3, kDmMerge = 4 };   // OEMDM_*
template <class P> HRESULT HandleDevMode(DWORD mode, P* p)
{
    if (!p)
        return E_INVALIDARG;
    switch (mode) {
    case kDmSize:
        p->cbBufSize = sizeof(Extra);
        return S_OK;
    case kDmDefault:
        if (!p->pOEMDMOut || p->cbBufSize < sizeof(Extra))
            return E_INVALIDARG;
        SetDefaults(*static_cast<Extra*>(p->pOEMDMOut));
        return S_OK;
    case kDmConvert:
    case kDmMerge: {
        if (!p->pOEMDMOut || p->cbBufSize < sizeof(Extra))
            return E_INVALIDARG;
        // pOEMDMIn may be any older version of ours (or garbage): never trust its size.
        const void* in = p->pOEMDMIn;
        DWORD cbIn = in ? static_cast<const Extra*>(in)->dwSize : 0;
        if (cbIn > sizeof(Extra) + 4096)
            cbIn = 0;
        Extra e;
        Import(e, in, cbIn);
        *static_cast<Extra*>(p->pOEMDMOut) = e;
        return S_OK;
    }
    }
    return E_INVALIDARG;
}

} // namespace lbpdm
