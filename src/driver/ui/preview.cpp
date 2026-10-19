// Custom controls of the Canon pages: the page preview (MPView_Setup_Image/MPView_Finsh_Image),
// the strip with the collate icon below it (MPView_*_Icon), the halftone sample of the Quality
// page (HalftoneImage) and the collate picture of the Finishing page. The drawing follows
// CAP1UIK.DLL pixel for pixel; every control is painted into a memory bitmap first.
#include "ui.h"
#include "htpreview.h"

namespace ui {
namespace {

const wchar_t* const kImageClasses[] = { L"LBP810_SetupImage", L"LBP810_FinishImage" };
const wchar_t* const kIconClasses[] = { L"LBP810_SetupIcon", L"LBP810_FinishIcon", L"LBP810_QualityIcon" };
const wchar_t kHalftoneClass[] = L"LBP810_Halftone";

Sheet* CtlSheet(HWND h)
{
    return reinterpret_cast<Sheet*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

SIZE WindowSize(HWND h)
{
    RECT r;
    GetWindowRect(h, &r);
    return { r.right - r.left, r.bottom - r.top };
}

void Frame(HDC dc, int l, int t, int r, int b, HBRUSH br)
{
    RECT rc = { l, t, r, b };
    FrameRect(dc, &rc, br);
}

// Double buffer for WM_PAINT.
class Buffer {
public:
    Buffer(HWND h, int w, int ht) : hwnd_(h), w_(w), h_(ht)
    {
        dc_ = BeginPaint(h, &ps_);
        mem_ = CreateCompatibleDC(dc_);
        bmp_ = CreateCompatibleBitmap(dc_, w, ht);
        old_ = SelectObject(mem_, bmp_);
        SetBrushOrgEx(mem_, 0, 0, nullptr);
    }
    ~Buffer()
    {
        BitBlt(dc_, 0, 0, w_, h_, mem_, 0, 0, SRCCOPY);
        SelectObject(mem_, old_);
        DeleteObject(bmp_);
        DeleteDC(mem_);
        EndPaint(hwnd_, &ps_);
    }
    HDC dc() const { return mem_; }

private:
    HWND hwnd_;
    int w_, h_;
    PAINTSTRUCT ps_;
    HDC dc_, mem_;
    HBITMAP bmp_;
    HGDIOBJ old_;
};

// sub_6A916023 / sub_6A9148EF: white with cyan diagonals every 4 pixels.
void Hatch(HDC dc, int w, int h)
{
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    RECT rc = { 0, 0, w, h };
    FillRect(dc, &rc, white);
    DeleteObject(white);
    HPEN pen = CreatePen(PS_SOLID, 0, RGB(0, 255, 255));
    HGDIOBJ old = SelectObject(dc, pen);
    for (int i = 3; i < w + h; i += 4) {
        POINT pt[2] = { { i, 0 }, { 0, i } };
        Polyline(dc, pt, 2);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}

// sub_6A916115: edges of the page preview control.
void ImageFrame(HDC dc, int w, int h)
{
    HBRUSH hi = CreateSolidBrush(GetSysColor(COLOR_BTNHIGHLIGHT));
    HBRUSH sh = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
    HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    Frame(dc, w - 1, 0, w, h, hi);
    Frame(dc, 0, 0, 1, h, sh);
    Frame(dc, 0, 0, w - 1, 1, sh);
    Frame(dc, 1, 1, w - 1, 2, face);
    Frame(dc, 1, 1, 2, h, face);
    Frame(dc, w - 2, 1, w - 1, h, face);
    DeleteObject(hi);
    DeleteObject(sh);
    DeleteObject(face);
}

// sub_6A916702: the sheet, sized after the output (or page) size.
RECT SheetRect(const Settings& s, int w, int h)
{
    const PaperDef& p = kPapers[s.output ? s.output - 1 : s.paper];
    int a = p.width, b = p.height;
    bool swapped = false;
    if (a > b) {
        std::swap(a, b);
        swapped = true;
    }
    int ratio = 100 * a / b;
    int len = b >= 1480 ? (b - 1480) / 39 + 100 : 100;
    if (len < 100)
        len = 100;
    if (len > 154)
        len = 154;
    int sw = len * ratio / 100, sh = len;
    if (swapped)
        std::swap(sw, sh);
    bool turn = !s.landscape ? (s.layout == 1 || s.layout == 3) : (s.layout == 0 || s.layout == 2 || s.layout == 4);
    if (turn)
        std::swap(sw, sh);
    RECT rc;
    rc.left = (w - sw - 1) / 2;
    rc.top = (h - sh - 1) / 2;
    rc.right = rc.left + sw;
    rc.bottom = rc.top + sh;
    return rc;
}

// sub_6A9168F9: text block of a single page, scaled by the manual scale.
RECT TextArea(const RECT& rc, int pct)
{
    int l = rc.left + 14, t = rc.top + 14;
    unsigned hh = unsigned(rc.bottom - rc.top - 28);
    unsigned ww = unsigned(rc.right - rc.left - 28);
    unsigned ty = unsigned(pct) * hh / 100;
    unsigned tx = unsigned(pct) * ww / 100;
    if (tx >= ww + 14)
        tx = ww + 13;
    if (ty >= hh + 14)
        ty = hh + 13;
    return { l, t, l + int(tx), t + int(ty) };
}

void Grid(const Settings& s, int& cols, int& rows)
{
    switch (s.layout) {
    case 1: cols = s.landscape ? 1 : 2; rows = s.landscape ? 2 : 1; break;
    case 2: cols = rows = 2; break;
    case 3: cols = s.landscape ? 2 : 4; rows = s.landscape ? 4 : 2; break;
    case 4: cols = rows = 3; break;
    default: cols = rows = 1; break;
    }
}

// sub_6A91696C: first page of an N-up sheet.
RECT FirstCell(const RECT& rc, int cols, int rows)
{
    int mw = 10 * cols + 18, mh = 10 * rows + 18;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int cw = w > mw ? (w - mw) / cols : 0;
    int ch = h > mh ? (h - mh) / rows : 0;
    return { rc.left + 14, rc.top + 14, rc.left + 14 + cw, rc.top + 14 + ch };
}

// sub_6A9162B5: white sheet, grey/black edges and a grey shadow.
void Paper(HDC dc, RECT r)
{
    r.right++;
    r.bottom++;
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ old = SelectObject(dc, white);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(BLACK_PEN));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldPen);
    SelectObject(dc, old);
    DeleteObject(white);
    HBRUSH grey = CreateSolidBrush(RGB(128, 128, 128));
    FrameRect(dc, &r, grey);
    Frame(dc, r.right, r.top + 1, r.right + 1, r.bottom, grey);
    Frame(dc, r.left + 1, r.bottom, r.right + 1, r.bottom + 1, grey);
    DeleteObject(grey);
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    Frame(dc, r.right - 1, r.top, r.right, r.bottom, black);
    Frame(dc, r.left, r.bottom - 1, r.right, r.bottom, black);
    DeleteObject(black);
}

// sub_6A9163E1: lines of "text", 1 pixel high every 4 pixels.
void TextLines(HDC dc, const RECT& a)
{
    if (a.right == a.left || a.bottom == a.top)
        return;
    HBITMAP bmp = LoadBitmapW(g_module, MAKEINTRESOURCEW(IDB_TEXTLINE));
    HBRUSH br = CreatePatternBrush(bmp);
    for (int y = a.top; y <= a.bottom; y += 4) {
        RECT rc = { a.left, y, a.right, y + 1 };
        FillRect(dc, &rc, br);
    }
    DeleteObject(br);
    DeleteObject(bmp);
}

void PaintImage(HWND hwnd)
{
    SIZE sz = WindowSize(hwnd);
    Buffer buf(hwnd, sz.cx, sz.cy);
    HDC dc = buf.dc();
    Hatch(dc, sz.cx, sz.cy);
    ImageFrame(dc, sz.cx, sz.cy);
    Sheet* sh = CtlSheet(hwnd);
    if (!sh)
        return;
    const Settings& s = sh->cur;
    RECT rc = SheetRect(s, sz.cx, sz.cy);
    Paper(dc, rc);
    if (!s.layout) {
        TextLines(dc, TextArea(rc, PreviewScale(s)));
        return;
    }
    // sub_6A9164B1
    int cols, rows;
    Grid(s, cols, rows);
    RECT c = FirstCell(rc, cols, rows);
    int dx = c.right - c.left + 10, dy = c.bottom - c.top + 10;
    for (int r = 0; r < rows; r++)
        for (int k = 0; k < cols; k++) {
            RECT cell = { c.left + k * dx, c.top + r * dy, c.right + k * dx, c.bottom + r * dy };
            TextLines(dc, cell);
        }
}

// sub_6A915CFC + sub_6A915F24
void PaintIcon(HWND hwnd)
{
    SIZE sz = WindowSize(hwnd);
    Buffer buf(hwnd, sz.cx, sz.cy);
    HDC dc = buf.dc();
    Sheet* sh = CtlSheet(hwnd);
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    HPEN pen = CreatePen(PS_SOLID, 1, face);
    HBRUSH br = CreateSolidBrush(face);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    Rectangle(dc, 0, 0, sz.cx, sz.cy);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
    if (sh) {
        HICON icon = LoadIconW(g_module, MAKEINTRESOURCEW(sh->cur.collate ? IDI_COLLATE : IDI_NOCOLLATE));
        if (icon)
            DrawIcon(dc, 3, 0, icon);
    }
    HBRUSH hi = CreateSolidBrush(GetSysColor(COLOR_BTNHIGHLIGHT));
    Frame(dc, 0, sz.cy - 2, sz.cx, sz.cy - 1, hi);
    Frame(dc, sz.cx - 1, 0, sz.cx, sz.cy - 1, hi);
    DeleteObject(hi);
    HBRUSH shadow = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
    Frame(dc, 0, sz.cy - 1, sz.cx, sz.cy, shadow);
    Frame(dc, 0, 0, 1, sz.cy - 1, shadow);
    DeleteObject(shadow);
}

LRESULT CALLBACK ImageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBP_SETSHEET:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 1;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintImage(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK IconProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBP_SETSHEET:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 1;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PaintIcon(hwnd);
        // sub_6A916F53: the caption under the preview is refreshed with the icon.
        if (Sheet* sh = CtlSheet(hwnd)) {
            HWND cap = GetDlgItem(GetParent(hwnd), IDC_PREVIEW_TEXT);
            std::wstring t = PreviewCaption(sh->cur);
            wchar_t old[256] = {};
            GetWindowTextW(cap, old, 256);
            if (t != old)
                SetWindowTextW(cap, t.c_str());
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- halftone sample (HalftoneImage, sub_6A91511F) ------------------------------------------

struct HalftoneCtl {
    HBITMAP page = nullptr;       // 213
    HBITMAP lens[2] = {};         // 211 (600 dpi), 210 (300 dpi)
    HBITMAP mask = nullptr;       // 212
    const BITMAPINFOHEADER* sample = nullptr;   // 208
    RECT client = {};
    RECT pageRc = {}, imageRc = {}, lensRc = {};
};

SIZE BitmapSize(HBITMAP b)
{
    BITMAP bm = {};
    if (b)
        GetObjectW(b, sizeof(bm), &bm);
    return { bm.bmWidth, bm.bmHeight };
}

HalftoneCtl* CreateHalftone(HWND hwnd)
{
    auto* c = new (std::nothrow) HalftoneCtl;
    if (!c)
        return nullptr;
    GetClientRect(hwnd, &c->client);
    const RECT& r = c->client;
    c->page = LoadBitmapW(g_module, MAKEINTRESOURCEW(IDB_PAGE));
    c->lens[0] = LoadBitmapW(g_module, MAKEINTRESOURCEW(IDB_RES600));
    c->lens[1] = LoadBitmapW(g_module, MAKEINTRESOURCEW(IDB_RES300));
    c->mask = LoadBitmapW(g_module, MAKEINTRESOURCEW(IDB_RES_MASK));
    if (HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(IDB_HALFTONE_SAMPLE), RT_BITMAP))
        if (HGLOBAL g = LoadResource(g_module, res))
            c->sample = static_cast<const BITMAPINFOHEADER*>(LockResource(g));
    // sub_6A914C8C: the sheet centred, 7 pixels up; the sample 5 pixels inside it.
    SIZE p = BitmapSize(c->page);
    RECT want;
    want.left = r.left + (r.right - r.left) / 2 - p.cx / 2;
    want.right = want.left + p.cx;
    int top = r.top + (r.bottom - r.top) / 2 - p.cy / 2;
    want.top = top - 7;
    want.bottom = top + p.cy - 7;
    IntersectRect(&c->pageRc, &r, &want);
    if (c->sample) {
        c->imageRc.left = c->pageRc.left + 5;
        c->imageRc.top = c->pageRc.top + 5;
        c->imageRc.right = c->imageRc.left + c->sample->biWidth;
        c->imageRc.bottom = c->imageRc.top + c->sample->biHeight;
    }
    // sub_6A914D8F: the magnifier, 53 right and 64 down of the centre.
    SIZE l = BitmapSize(c->lens[0]);
    int lx = r.left + (r.right - r.left) / 2 - l.cx / 2;
    int ly = r.top + (r.bottom - r.top) / 2 - l.cy / 2;
    want = { lx + 53, ly + 64, lx + l.cx + 53, ly + l.cy + 64 };
    IntersectRect(&c->lensRc, &r, &want);
    return c;
}

void DestroyHalftone(HalftoneCtl* c)
{
    if (!c)
        return;
    for (HBITMAP b : { c->page, c->lens[0], c->lens[1], c->mask })
        if (b)
            DeleteObject(b);
    delete c;
}

void Blit(HDC dc, const RECT& r, HBITMAP b, DWORD rop)
{
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(m, b);
    BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, m, 0, 0, rop);
    SelectObject(m, o);
    DeleteDC(m);
}

// sub_6A9149E1: bitmap with a mask (white = transparent).
void MaskedBlit(HDC dc, const RECT& r, HBITMAP b, HBITMAP mask)
{
    if (!mask) {
        Blit(dc, r, b, SRCCOPY);
        return;
    }
    SIZE s = BitmapSize(b);
    HBITMAP tmp = CreateCompatibleBitmap(dc, s.cx, s.cy);
    HDC src = CreateCompatibleDC(dc), mk = CreateCompatibleDC(dc), t = CreateCompatibleDC(dc);
    HGDIOBJ o1 = SelectObject(src, b), o2 = SelectObject(mk, mask), o3 = SelectObject(t, tmp);
    BitBlt(t, 0, 0, s.cx, s.cy, mk, 0, 0, NOTSRCCOPY);
    BitBlt(t, 0, 0, s.cx, s.cy, src, 0, 0, SRCAND);
    BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mk, 0, 0, SRCAND);
    BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, t, 0, 0, SRCPAINT);
    SelectObject(src, o1);
    SelectObject(mk, o2);
    SelectObject(t, o3);
    DeleteDC(src);
    DeleteDC(mk);
    DeleteDC(t);
    DeleteObject(tmp);
}

void PaintHalftone(HWND hwnd, HalftoneCtl* c)
{
    const RECT& r = c->client;
    Buffer buf(hwnd, r.right, r.bottom);
    HDC dc = buf.dc();
    Hatch(dc, r.right, r.bottom);
    if (c->page)
        Blit(dc, c->pageRc, c->page, SRCCOPY);
    Sheet* sh = CtlSheet(hwnd);
    if (!sh)
        return;
    const Settings& s = sh->cur;
    if (c->sample) {
        // The original renders the sample on every paint (the "smoothing" noise moves).
        static thread_local CrtRand rng;
        std::vector<uint8_t> bits =
            HalftonePreview(c->sample, s.halftone, StoredAdjust(s.brightness), StoredAdjust(s.contrast), rng);
        HBITMAP b = CreateBitmap(c->sample->biWidth, c->sample->biHeight, 1, 1, bits.data());
        if (b) {
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkColor(dc, RGB(255, 255, 255));
            Blit(dc, c->imageRc, b, SRCCOPY);
            DeleteObject(b);
        }
    }
    if (HBITMAP lens = c->lens[s.res300 ? 1 : 0])
        MaskedBlit(dc, c->lensRc, lens, c->mask);
}

LRESULT CALLBACK HalftoneProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<HalftoneCtl*>(GetWindowLongPtrW(hwnd, 0));
    switch (msg) {
    case WM_CREATE:
        c = CreateHalftone(hwnd);
        SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(c));
        return c ? 0 : -1;
    case WM_DESTROY:
        DestroyHalftone(c);
        SetWindowLongPtrW(hwnd, 0, 0);
        return 0;
    case WM_LBP_SETSHEET:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 1;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (c) {
            PaintHalftone(hwnd, c);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// sub_6A907607 and friends: 42x35 pictures combined with raster operations.
void DrawBitmap(HDC dc, UINT id, DWORD rop)
{
    HBITMAP b = LoadBitmapW(g_module, MAKEINTRESOURCEW(id));
    if (!b)
        return;
    SIZE s = BitmapSize(b);
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(m, b);
    BitBlt(dc, 0, 0, s.cx, s.cy, m, 0, 0, rop);
    SelectObject(m, o);
    DeleteDC(m);
    DeleteObject(b);
}

} // namespace

void PaintCollate(HWND, HDC dc, bool collate)
{
    // sub_6A907812: image through its mask, then the shadow in COLOR_BTNSHADOW.
    const UINT image = collate ? 200 : 203, mask = collate ? 201 : 204, shadow = collate ? 202 : 205;
    const int w = 42, h = 35;
    DrawBitmap(dc, mask, SRCAND);
    HBITMAP tb = CreateCompatibleBitmap(dc, w, h);
    HDC t = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(t, tb);
    DrawBitmap(t, mask, NOTSRCCOPY);
    DrawBitmap(t, image, SRCAND);
    BitBlt(dc, 0, 0, w, h, t, 0, 0, SRCPAINT);
    DrawBitmap(t, shadow, NOTSRCCOPY);
    BitBlt(dc, 0, 0, w, h, t, 0, 0, SRCAND);
    LOGBRUSH lb = { BS_SOLID, GetSysColor(COLOR_BTNSHADOW), 0 };
    HBRUSH br = CreateBrushIndirect(&lb);
    HGDIOBJ ob = SelectObject(t, br);
    PatBlt(t, 0, 0, w, h, PATCOPY);
    SelectObject(t, ob);
    DeleteObject(br);
    DrawBitmap(t, shadow, SRCAND);
    BitBlt(dc, 0, 0, w, h, t, 0, 0, SRCPAINT);
    SelectObject(t, o);
    DeleteDC(t);
    DeleteObject(tb);
}

std::wstring PreviewCaption(const Settings& s)
{
    // sub_6A915DD1
    std::wstring t = LoadStr(kPapers[s.paper].nameId + 3122);
    t += LoadStr(3100);
    t += LoadStr(kPapers[s.output ? s.output - 1 : s.paper].nameId + 3122);
    t += LoadStr(3102);
    if (s.manualScale) {
        t += std::to_wstring(s.scale);
        t += LoadStr(3104);
    } else {
        t += LoadStr(3103);
    }
    t += LoadStr(3105);
    return t;
}

void UpdatePreview(HWND page)
{
    for (int id : { IDC_PREVIEW, IDC_PREVIEW_ICON, IDC_HALFTONE_IMAGE })
        if (HWND h = GetDlgItem(page, id))
            InvalidateRect(h, nullptr, FALSE);
}

bool RegisterControls(HINSTANCE inst)
{
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = inst;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
    bool ok = true;
    wc.lpfnWndProc = ImageProc;
    for (auto name : kImageClasses) {
        wc.lpszClassName = name;
        ok &= RegisterClassW(&wc) != 0;
    }
    wc.lpfnWndProc = IconProc;
    for (auto name : kIconClasses) {
        wc.lpszClassName = name;
        ok &= RegisterClassW(&wc) != 0;
    }
    wc.lpfnWndProc = HalftoneProc;
    wc.cbWndExtra = sizeof(void*);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kHalftoneClass;
    ok &= RegisterClassW(&wc) != 0;
    ok &= RegisterOverlayClass(inst);
    return ok;
}

void UnregisterControls(HINSTANCE inst)
{
    for (auto name : kImageClasses)
        UnregisterClassW(name, inst);
    for (auto name : kIconClasses)
        UnregisterClassW(name, inst);
    UnregisterClassW(kHalftoneClass, inst);
    UnregisterOverlayClass(inst);
}

} // namespace ui
