// captcli - command line tool to talk to a Canon CAPT (LBP-810) printer over USB.
//   captcli list                     enumerate USB printers and their IEEE-1284 IDs
//   captcli raw <cmd> [hex...]       send one CAPT command (e.g. A0A0) and dump the reply
//   captcli status                   open no session, print decoded status
//   captcli testpage [density]       print a generated A4 test page (density 0..63, default 0x1F)
//   captcli render <file.bmp>        save the generated test page as BMP
//   captcli spool <file> [pages]     write the test page as a driver spool stream (for lmtest)
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <intrin.h>
#include <vector>
#include "usbdev.h"
#include "../common/engine.h"
#include "../common/scoa.h"
#include "../common/spool.h"

static void Hex(const char* tag, const BYTE* p, int n)
{
    printf("%s n=%d:", tag, n);
    for (int i = 0; i < n; i++)
        printf(" %02X", p[i]);
    printf("\n");
}

static bool OpenFirst(UsbPrinter& prn)
{
    for (auto& i : EnumUsbPrinters()) {
        if (i.deviceId.find("CAPT") != std::string::npos || i.deviceId.find("LBP") != std::string::npos)
            return prn.Open(i.path);
    }
    fprintf(stderr, "No CAPT printer found\n");
    return false;
}

class UsbTransport : public capt::Transport {
public:
    explicit UsbTransport(UsbPrinter& p) : p_(p) {}
    bool Write(const uint8_t* d, size_t n) override { return p_.Write(d, DWORD(n)); }
    int Read(uint8_t* d, size_t n, unsigned ms) override { return p_.Read(d, DWORD(n), ms); }
private:
    UsbPrinter& p_;
};

// A4 geometry from CAP1K resource 200/100 (300 dpi units): printable 2368x3388, left 56, top 60.
struct Geometry {
    int paperW, paperH;      // 600 dpi dots
    int printW, printH;      // printable area
    int left, top;
};
static Geometry A4Geometry()
{
    Geometry g;
    g.paperW = 2100 * 600 / 254;
    g.paperH = 2970 * 600 / 254;
    g.printW = 2368 * 2;
    g.printH = 3388 * 2;
    g.left = 56 * 2;
    g.top = 60 * 2;
    return g;
}

// Renders a test page into a top-down 1bpp bitmap (1 = black).
static std::vector<uint8_t> RenderTestPage(const Geometry& g, int& stride)
{
    struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; } bi = {};
    bi.h.biSize = sizeof(bi.h);
    bi.h.biWidth = g.printW;
    bi.h.biHeight = -g.printH;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.pal[0] = { 255, 255, 255, 0 };
    bi.pal[1] = { 0, 0, 0, 0 };
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    // Note: WHITENESS would set every bit to 1, which is black for the printer.
    RECT all = { 0, 0, g.printW, g.printH };
    FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    HPEN pen = CreatePen(PS_SOLID, 4, RGB(0, 0, 0));
    SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 2, 2, g.printW - 2, g.printH - 2);
    // 10 mm ruler ticks
    for (int mm = 10; mm * 600 / 25.4 < g.printW; mm += 10) {
        int x = int(mm * 600 / 25.4);
        MoveToEx(dc, x, 0, nullptr); LineTo(dc, x, (mm % 50) ? 60 : 140);
    }
    for (int mm = 10; mm * 600 / 25.4 < g.printH; mm += 10) {
        int y = int(mm * 600 / 25.4);
        MoveToEx(dc, 0, y, nullptr); LineTo(dc, (mm % 50) ? 60 : 140, y);
    }
    SetBkMode(dc, TRANSPARENT);
    LOGFONTW lf = {};
    lf.lfHeight = -300;
    lf.lfWeight = FW_BOLD;
    wcscpy_s(lf.lfFaceName, L"Arial");
    HFONT big = CreateFontIndirectW(&lf);
    lf.lfHeight = -100;
    lf.lfWeight = FW_NORMAL;
    HFONT fontSmall = CreateFontIndirectW(&lf);
    SelectObject(dc, big);
    TextOutW(dc, 400, 400, L"Canon LBP-810", 13);
    SelectObject(dc, fontSmall);
    const wchar_t* lines[] = {
        L"CAPT test page - x64 driver project (captcli)",
        L"\x0422\x0435\x0441\x0442\x043E\x0432\x0430\x044F \x0441\x0442\x0440\x0430\x043D\x0438\x0446\x0430: "
        L"\x043F\x0440\x044F\x043C\x0430\x044F \x043F\x0435\x0447\x0430\x0442\x044C \x043F\x043E USB",
        L"SCoA encoder: bit-exact port of CAP1K.DLL",
        L"A4, 600 dpi, printable 4736 x 6776 dots, margins 5 mm",
    };
    for (int i = 0; i < 4; i++)
        TextOutW(dc, 400, 800 + i * 160, lines[i], int(wcslen(lines[i])));
    // Gray patches (ordered dither 8x8)
    static const int bayer[8][8] = {
        { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
        { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
        { 3, 35, 11, 43, 1, 33, 9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 },
        { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };
    GdiFlush();
    uint8_t* p = static_cast<uint8_t*>(bits);
    stride = ((g.printW + 31) / 32) * 4;
    for (int k = 0; k < 9; k++) {
        int x0 = 400 + k * 440, y0 = 1700;
        int level = (k + 1) * 64 / 10;
        for (int y = y0; y < y0 + 400; y++)
            for (int x = x0; x < x0 + 400; x++)
                if (bayer[y & 7][x & 7] < level)
                    p[y * stride + (x >> 3)] |= uint8_t(0x80 >> (x & 7));
    }
    // Fine line fan
    for (int i = 0; i < 60; i++) {
        MoveToEx(dc, 2400, 4200, nullptr);
        LineTo(dc, 400 + i * 70, 2400);
    }
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, 2600, 4400, 4300, 6100);
    GdiFlush();

    std::vector<uint8_t> out(p, p + size_t(stride) * g.printH);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteObject(pen);
    DeleteObject(big);
    DeleteObject(fontSmall);
    DeleteDC(dc);
    return out;
}

static capt::PageParams A4Params(const Geometry& g, int stride, int density)
{
    capt::PageParams pp = {};
    pp.modelCode = capt::kModelCodeLbp810;
    pp.paperCode = capt::PaperCodeFor(DMPAPER_A4);
    pp.flags = 0x01;
    pp.density = uint8_t(density);
    pp.density2[0] = pp.density2[1] = pp.density2[2] = 0x1F;
    pp.resolution = 0x11;
    pp.const14[0] = 3; pp.const14[1] = 1; pp.const14[2] = 1; pp.const14[3] = 1;
    pp.refinement = 2;
    pp.marginLeft = uint16_t(g.left);
    pp.marginTop = uint16_t(g.top);
    pp.lineBytes = uint16_t(stride);
    pp.lines = uint16_t(g.printH + 2);
    pp.paperWidth = uint16_t(g.paperW);
    pp.paperHeight = uint16_t(g.paperH);
    return pp;
}

static int WriteSpool(const wchar_t* path, int pages)
{
    Geometry g = A4Geometry();
    int stride = 0;
    auto bmp = RenderTestPage(g, stride);
    std::vector<uint8_t> out;
    spool::JobHeader jh;
    spool::DocInfo di;
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&jh), reinterpret_cast<uint8_t*>(&jh) + sizeof(jh));
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&di), reinterpret_cast<uint8_t*>(&di) + sizeof(di));
    for (int i = 0; i < pages; i++)
        spool::PutPage(out, A4Params(g, stride, 0x1F), bmp.data(), stride, g.printH);
    spool::Put(out, spool::PKT_JOB_END, nullptr, 0);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") || !f)
        return 1;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("wrote %zu bytes (%d pages)\n", out.size(), pages);
    return 0;
}

static int TestPage(int density)
{
    UsbPrinter prn;
    if (!OpenFirst(prn))
        return 1;
    UsbTransport tr(prn);
    capt::EngineCallbacks cb;
    cb.log = [](const std::string& s) { printf("%s\n", s.c_str()); };
    capt::Engine eng(tr, cb);

    Geometry g = A4Geometry();
    int stride = 0;
    auto bmp = RenderTestPage(g, stride);
    capt::Page page;
    scoa::EncodeBand(bmp.data(), stride, stride, g.printH, page.chunks);
    page.chunks.push_back(scoa::EndOfPageChunk());

    capt::PageParams& pp = page.params;
    pp.modelCode = capt::kModelCodeLbp810;
    pp.paperCode = capt::PaperCodeFor(DMPAPER_A4);
    pp.flags = 0x01;
    pp.source = 0;
    pp.density = uint8_t(density);
    pp.density2[0] = pp.density2[1] = pp.density2[2] = 0x1F;
    pp.mediaType = 0;
    pp.resolution = 0x11;
    pp.const14[0] = 3; pp.const14[1] = 1; pp.const14[2] = 1; pp.const14[3] = 1;
    pp.refinement = 2;
    pp.tonerSave = 0;
    pp.marginLeft = uint16_t(g.left);
    pp.marginTop = uint16_t(g.top);
    pp.lineBytes = uint16_t(stride);
    pp.lines = uint16_t(g.printH + 2);
    pp.paperWidth = uint16_t(g.paperW);
    pp.paperHeight = uint16_t(g.paperH);

    if (!eng.BeginJob()) {
        printf("BeginJob failed\n");
        eng.AbortJob();
        return 1;
    }
    bool ok = eng.QueuePage(page);
    printf("QueuePage: %s\n", ok ? "ok" : "FAILED");
    if (ok) {
        ok = eng.EndJob();
        printf("EndJob: %s\n", ok ? "ok" : "FAILED");
    } else {
        eng.AbortJob();
    }
    return ok ? 0 : 1;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: captcli list | raw <cmd> [hex bytes] | status | testpage [density]\n");
        return 2;
    }
    std::wstring verb = argv[1];
    if (verb == L"list") {
        for (auto& i : EnumUsbPrinters())
            printf("%ls\n  ID: %s\n", i.path.c_str(), i.deviceId.c_str());
        return 0;
    }
    if (verb == L"raw" && argc >= 3) {
        UsbPrinter prn;
        if (!OpenFirst(prn))
            return 1;
        unsigned cmd = wcstoul(argv[2], nullptr, 16);
        std::vector<BYTE> pkt = { BYTE(cmd & 0xFF), BYTE(cmd >> 8), 0, 0 };
        for (int i = 3; i < argc; i++)
            pkt.push_back(BYTE(wcstoul(argv[i], nullptr, 16)));
        pkt[2] = BYTE(pkt.size());
        pkt[3] = BYTE(pkt.size() >> 8);
        Hex("W", pkt.data(), int(pkt.size()));
        if (!prn.Write(pkt.data(), DWORD(pkt.size()))) {
            fprintf(stderr, "write failed %lu\n", GetLastError());
            return 1;
        }
        BYTE buf[4096];
        for (int k = 0; k < 4; k++) {
            int n = prn.Read(buf, sizeof(buf), 1500);
            if (n <= 0)
                break;
            Hex("R", buf, n);
        }
        return 0;
    }
    if (verb == L"status") {
        UsbPrinter prn;
        if (!OpenFirst(prn))
            return 1;
        UsbTransport tr(prn);
        capt::EngineCallbacks cb;
        cb.log = [](const std::string& s) { printf("%s\n", s.c_str()); };
        capt::Engine eng(tr, cb);
        if (!eng.RefreshStatus() || !eng.QueryTray())
            return 1;
        printf("condition: %s\n", capt::ConditionName(eng.CurrentCondition()));
        return 0;
    }
    if (verb == L"render" && argc >= 3) {
        Geometry g = A4Geometry();
        int stride = 0;
        auto bmp = RenderTestPage(g, stride);
        size_t black = 0;
        for (uint8_t b : bmp)
            black += __popcnt(b);
        printf("%dx%d stride %d, black pixels %zu\n", g.printW, g.printH, stride, black);
        FILE* f = nullptr;
        if (_wfopen_s(&f, argv[2], L"wb") || !f)
            return 1;
        BITMAPFILEHEADER fh = {};
        struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; } bi = {};
        bi.h.biSize = sizeof(bi.h);
        bi.h.biWidth = g.printW;
        bi.h.biHeight = -g.printH;
        bi.h.biPlanes = 1;
        bi.h.biBitCount = 1;
        bi.pal[0] = { 255, 255, 255, 0 };
        bi.pal[1] = { 0, 0, 0, 0 };
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi);
        fh.bfSize = DWORD(fh.bfOffBits + bmp.size());
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi, sizeof(bi), 1, f);
        fwrite(bmp.data(), 1, bmp.size(), f);
        fclose(f);
        return 0;
    }
    if (verb == L"spool" && argc >= 3)
        return WriteSpool(argv[2], argc >= 4 ? _wtoi(argv[3]) : 1);
    if (verb == L"testpage") {
        int density = argc >= 3 ? int(wcstoul(argv[2], nullptr, 0)) : 0x1F;
        return TestPage(density);
    }
    fprintf(stderr, "bad arguments\n");
    return 2;
}
