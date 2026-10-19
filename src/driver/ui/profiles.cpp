// "Configurations" (profiles) of the Canon UI: the owner-drawn combo box on every page, the
// "View settings" list and the "Add" / "Edit configuration" dialogs (CAP1UIK sub_6A90A597,
// sub_6A909EFA, sub_6A90C443, sub_6A90DB8E). The list is kept per user and printer in
// HKCU\Software\LBP810x\Profiles\<printer> (Canon: HKCU\SOFTWARE\Canon\CAPT\...\PROFILE).
#include "ui.h"
#include <commdlg.h>
#include <cstdio>

namespace ui {
namespace {

constexpr int kIcons = 36;               // 126..161
constexpr UINT IDI_MODIFIED = 168;       // marker of a changed configuration
constexpr int kNameMax = 24, kNotesMax = 160;
constexpr int IDC_VS_LIST = 1054, IDC_PA_LIST = 1113, IDC_PE_LIST_VIEW = 1172;

HICON Icon(UINT id)
{
    return LoadIconW(g_module, MAKEINTRESOURCEW(id));
}

// ---- storage ---------------------------------------------------------------------------------

#pragma pack(push, 4)
struct Rec {
    DWORD size;
    WCHAR name[kNameMax + 1];
    WCHAR notes[kNotesMax + 1];
    INT icon;
    INT paper, output, copies, landscape, layout, order[4], manualScale, scale;
    INT collate, ignoreMismatch, emfSpool;
    INT res300, halftone, brightness, contrast, density, tonerSave, refinement;
    INT overlayFront, overlayInch;
    LONG overlayLeft, overlayTop, overlayWidth, overlayHeight;
    WCHAR overlayFile[MAX_PATH];
};
#pragma pack(pop)

const char kFileMagic[8] = { 'L', 'B', 'P', '8', '1', '0', 'C', 'F' };

int Clamp(int v, int lo, int hi, int def)
{
    return v < lo || v > hi ? def : v;
}

Rec ToRec(const Profile& p)
{
    Rec r = {};
    r.size = sizeof(r);
    wcsncpy_s(r.name, p.name.c_str(), _TRUNCATE);
    wcsncpy_s(r.notes, p.notes.c_str(), _TRUNCATE);
    r.icon = p.icon;
    const Settings& s = p.s;
    r.paper = s.paper;
    r.output = s.output;
    r.copies = s.copies;
    r.landscape = s.landscape;
    r.layout = s.layout;
    for (int i = 0; i < 4; i++)
        r.order[i] = s.order[i];
    r.manualScale = s.manualScale;
    r.scale = s.scale;
    r.collate = s.collate;
    r.ignoreMismatch = s.ignoreMismatch;
    r.emfSpool = s.emfSpool;
    r.res300 = s.res300;
    r.halftone = s.halftone;
    r.brightness = s.brightness;
    r.contrast = s.contrast;
    r.density = s.density;
    r.tonerSave = s.tonerSave;
    r.refinement = s.refinement;
    r.overlayFront = s.overlayFront;
    r.overlayInch = s.overlayInch;
    r.overlayLeft = s.overlayLeft;
    r.overlayTop = s.overlayTop;
    r.overlayWidth = s.overlayWidth;
    r.overlayHeight = s.overlayHeight;
    wcsncpy_s(r.overlayFile, s.overlayFile.c_str(), _TRUNCATE);
    return r;
}

bool FromRec(const Rec& r, Profile& p)
{
    if (r.size != sizeof(Rec))
        return false;
    Rec c = r;
    c.name[kNameMax] = 0;
    c.notes[kNotesMax] = 0;
    c.overlayFile[MAX_PATH - 1] = 0;
    p.name = c.name;
    p.notes = c.notes;
    p.icon = Clamp(c.icon, 0, kIcons - 1, 0);
    Settings& s = p.s;
    s.paper = Clamp(c.paper, 0, kPaperCount - 1, DefaultPaper());
    s.output = Clamp(c.output, 0, kPaperCount, 0);
    s.copies = Clamp(c.copies, 1, 999, 1);
    s.landscape = c.landscape != 0;
    s.layout = Clamp(c.layout, 0, 4, 0);
    s.order[0] = Clamp(c.order[0], 0, 1, 0);
    for (int i = 1; i < 4; i++)
        s.order[i] = Clamp(c.order[i], 0, 3, 0);
    s.manualScale = c.manualScale != 0;
    s.scale = Clamp(c.scale, lbpdm::kMinScale, lbpdm::kMaxScale, 100);
    s.collate = c.collate != 0;
    s.ignoreMismatch = c.ignoreMismatch != 0;
    s.emfSpool = c.emfSpool != 0;
    s.res300 = c.res300 != 0;
    s.halftone = Clamp(c.halftone, 0, 2, lbpdm::HT_PATTERN1);
    s.brightness = 2 * (Clamp(c.brightness, lbpdm::kMinAdjust, lbpdm::kMaxAdjust, 0) / 2);
    s.contrast = 2 * (Clamp(c.contrast, lbpdm::kMinAdjust, lbpdm::kMaxAdjust, 0) / 2);
    s.density = Clamp(c.density, 0, 4, 2);
    s.tonerSave = c.tonerSave != 0;
    s.refinement = c.refinement != 0;
    s.overlayFront = c.overlayFront != 0;
    s.overlayInch = c.overlayInch != 0;
    s.overlayLeft = c.overlayLeft;
    s.overlayTop = c.overlayTop;
    s.overlayWidth = c.overlayWidth;
    s.overlayHeight = c.overlayHeight;
    s.overlayFile = c.overlayFile;
    Normalize(s);
    return true;
}

std::wstring KeyFor(const std::wstring& printer)
{
    // sub_6A90A96C: backslashes of a "\\server\printer" name become commas
    std::wstring p = printer;
    for (auto& ch : p)
        if (ch == L'\\')
            ch = L',';
    return L"Software\\LBP810x\\Profiles\\" + p;
}

// sub_6A90A7EA: Canon's message boxes about configurations.
int Message(HWND owner, UINT caption, UINT text, UINT type)
{
    std::wstring c = LoadStr(caption), t = LoadStr(text);
    return MessageBoxW(owner, t.c_str(), c.c_str(), type | MB_ICONEXCLAMATION);
}

// ---- the settings list (sub_6A909BA9) ------------------------------------------------------

using Rows = std::vector<std::pair<std::wstring, std::wstring>>;

std::wstring YesNo(bool on)
{
    return LoadStr(on ? 3000 : 3001);
}

Rows SettingsRows(const Settings& s, int page)
{
    Rows r;
    wchar_t buf[64];
    if (page == 0) {
        r.push_back({ LoadStr(3016), LoadStr(kPapers[s.paper].nameId) });
        r.push_back({ LoadStr(3017), s.output ? LoadStr(kPapers[s.output - 1].nameId) : LoadStr(5050) });
        swprintf_s(buf, L"%d", s.copies);
        r.push_back({ LoadStr(3018), buf });
        r.push_back({ LoadStr(3019), LoadStr(s.landscape ? 3003 : 3002) });
        r.push_back({ LoadStr(3020), LoadStr(5061 + s.layout) });
        std::wstring v;
        if (s.layout == 1) {
            v = LoadStr(5071 + s.order[0] + (s.landscape ? 2 : 0));
        } else if (s.layout > 1) {
            static const UINT base[] = { 0, 0, 5081, 5091, 5101 };
            int o = s.order[s.layout - 1];
            v = LoadStr(base[s.layout] + (o == 1 ? 2 : (o == 2 ? 1 : o)));
        } else if (s.manualScale) {
            swprintf_s(buf, L"%s [%d%%]", LoadStr(3000).c_str(), s.scale);
            v = buf;
        } else {
            v = LoadStr(3022);
        }
        r.push_back({ LoadStr(s.layout ? 3015 : 3014), v });
        r.push_back({ LoadStr(3021), YesNo(!s.overlayFile.empty()) });
    } else if (page == 1) {
        r.push_back({ LoadStr(3023), YesNo(s.collate) });
        r.push_back({ LoadStr(3024), YesNo(s.ignoreMismatch) });
        r.push_back({ LoadStr(3025), YesNo(s.emfSpool) });
    } else {
        r.push_back({ LoadStr(3026), LoadStr(s.res300 ? 3005 : 3004) });
        static const UINT ht[] = { 3006, 3007, 3008 };
        r.push_back({ LoadStr(3027), LoadStr(ht[s.halftone]) });
        swprintf_s(buf, L"%d%%", s.brightness);
        r.push_back({ LoadStr(3028), buf });
        swprintf_s(buf, L"%d%%", s.contrast);
        r.push_back({ LoadStr(3029), buf });
        r.push_back({ LoadStr(3030), LoadStr(3013 - s.density) });
        r.push_back({ LoadStr(3031), YesNo(s.tonerSave) });
        r.push_back({ LoadStr(3032), YesNo(s.refinement) });
    }
    return r;
}

// sub_6A909A72 + sub_6A9099CC + sub_6A909A2B + sub_6A909B3A + sub_6A909D23
HWND CreateSettingsList(HWND dlg, int tabId, int listId, int page)
{
    HWND tab = GetDlgItem(dlg, tabId);
    for (int i = 0; i < 3; i++) {
        std::wstring t = LoadStr(3033 + i);
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        ti.pszText = &t[0];
        SendMessageW(tab, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&ti));
    }
    RECT item = {}, rc;
    SendMessageW(tab, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
    GetClientRect(tab, &rc);
    int x = 12, y = item.bottom + 8, w = rc.right - 24, h = rc.bottom - item.bottom - 16;
    HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER, x, y, w, h, tab, nullptr,
                                g_module, nullptr);
    SetWindowLongPtrW(list, GWLP_ID, listId);
    LVCOLUMNW col = {};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = (w - 5) / 2;
    for (int i = 0; i < 2; i++) {
        col.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&col));
    }
    COLORREF bg = GetSysColor(COLOR_BTNFACE);
    SendMessageW(list, LVM_SETTEXTBKCOLOR, 0, bg);
    SendMessageW(list, LVM_SETBKCOLOR, 0, bg);
    SendMessageW(tab, TCM_SETCURSEL, page, 0);
    return list;
}

// blank: rows without text, what CAP1UIK shows before a configuration is chosen (its
// buffers are zeroed when the list is created).
void FillSettingsList(HWND tab, HWND list, const Settings* s, bool blank = false)
{
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    if (!s && !blank)
        return;
    int page = int(SendMessageW(tab, TCM_GETCURSEL, 0, 0));
    Rows rows = SettingsRows(s ? *s : Settings(), page < 0 ? 0 : page);
    if (!s)
        for (auto& r : rows)
            r.first.clear(), r.second.clear();
    for (size_t i = 0; i < rows.size(); i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = int(i);
        it.pszText = &rows[i].first[0];
        SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));
        it.iSubItem = 1;
        it.pszText = &rows[i].second[0];
        SendMessageW(list, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&it));
    }
}

void ListColors(HWND list)
{
    COLORREF bg = GetSysColor(COLOR_BTNFACE);
    SendMessageW(list, LVM_SETTEXTBKCOLOR, 0, bg);
    SendMessageW(list, LVM_SETBKCOLOR, 0, bg);
}

// ---- owner drawing (sub_6A90A2AA, sub_6A90A343, sub_6A90A3C7) ------------------------------

void ItemBackground(const DRAWITEMSTRUCT* di)
{
    COLORREF c = GetSysColor((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ op = SelectObject(di->hDC, pen), ob = SelectObject(di->hDC, br);
    Rectangle(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
    SelectObject(di->hDC, ob);
    DeleteObject(br);
    SelectObject(di->hDC, op);
    DeleteObject(pen);
}

void ItemText(const DRAWITEMSTRUCT* di, int x, const std::wstring& t)
{
    TEXTMETRICW tm;
    GetTextMetricsW(di->hDC, &tm);
    int y = di->rcItem.top + (di->rcItem.bottom - tm.tmHeight - di->rcItem.top) / 2;
    bool sel = (di->itemState & ODS_SELECTED) != 0;
    COLORREF oldText = 0, oldBk = 0;
    if (sel) {
        oldText = SetTextColor(di->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
        oldBk = SetBkColor(di->hDC, GetSysColor(COLOR_HIGHLIGHT));
    }
    ExtTextOutW(di->hDC, x, y, ETO_CLIPPED, &di->rcItem, t.c_str(), UINT(t.size()), nullptr);
    if (sel) {
        SetBkColor(di->hDC, oldBk);
        SetTextColor(di->hDC, oldText);
    }
}

// Profile entry: list (Edit dialog, a6 = 1) or page combo (with the "modified" marker).
void DrawProfile(const DRAWITEMSTRUCT* di, const Profile& p, bool marker, bool inList)
{
    if (di->itemAction == ODA_FOCUS) {
        DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }
    if (di->itemAction != ODA_DRAWENTIRE && di->itemAction != ODA_SELECT)
        return;
    ItemBackground(di);
    HICON icon = Icon(UINT(IDI_PROFILE_FIRST + p.icon));
    if (inList) {
        DrawIcon(di->hDC, di->rcItem.left, di->rcItem.top, icon);
        ItemText(di, 20, p.name);
    } else {
        if (marker)
            DrawIcon(di->hDC, di->rcItem.left, di->rcItem.top, Icon(IDI_MODIFIED));
        DrawIcon(di->hDC, di->rcItem.left + 13, di->rcItem.top, icon);
        ItemText(di, 33, p.name);
    }
}

// sub_6A90A48C: icon choice list, icons centred in 40x50 cells.
void DrawIconCell(const DRAWITEMSTRUCT* di)
{
    if (di->itemID == UINT(-1))
        return;
    if (di->itemAction == ODA_FOCUS) {
        DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }
    if (di->itemAction != ODA_DRAWENTIRE && di->itemAction != ODA_SELECT)
        return;
    ItemBackground(di);
    auto icon = reinterpret_cast<HICON>(SendMessageW(di->hwndItem, LB_GETITEMDATA, di->itemID, 0));
    int x = di->rcItem.left + (di->rcItem.right - di->rcItem.left - 16) / 2;
    int y = di->rcItem.top + (di->rcItem.bottom - GetSystemMetrics(SM_CYHSCROLL) - di->rcItem.top - 16) / 2;
    DrawIcon(di->hDC, x, y, icon);
}

void FillIconList(HWND dlg, int id)
{
    // sub_6A90A4D4
    HWND lb = GetDlgItem(dlg, id);
    for (int i = 0; i < kIcons; i++) {
        LRESULT n = SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" "));
        SendMessageW(lb, LB_SETITEMDATA, WPARAM(n), reinterpret_cast<LPARAM>(Icon(UINT(IDI_PROFILE_FIRST + i))));
    }
}

void ClampIconSelection(HWND dlg, int id)
{
    HWND lb = GetDlgItem(dlg, id);
    LRESULT n = SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (n < 0)
        SendMessageW(lb, LB_SETCURSEL, 0, 0);
    else if (n > kIcons)
        SendMessageW(lb, LB_SETCURSEL, kIcons, 0);
}

std::wstring Text(HWND dlg, int id, int max)
{
    std::vector<wchar_t> b(size_t(max) + 2, 0);
    GetDlgItemTextW(dlg, id, b.data(), max + 1);
    return b.data();
}

int Find(const std::vector<Profile>& v, const std::wstring& name)
{
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].name == name)
            return int(i);
    return -1;
}

void RemoveAt(std::vector<Profile>& v, int i, int& selected)
{
    v.erase(v.begin() + i);
    if (selected == i)
        selected = -1;
    else if (selected > i)
        selected--;
}

// sub_6A90AE46 + sub_6A90ADF5: the name must not be empty; a configuration with the same name
// may be replaced. Returns false to stay in the dialog. *removed: index taken out, or -1.
bool CheckName(HWND dlg, int editId, std::vector<Profile>& v, int self, int& selected, UINT caption, int* removed)
{
    if (removed)
        *removed = -1;
    HWND e = GetDlgItem(dlg, editId);
    if (!GetWindowTextLengthW(e)) {
        MessageBeep(MB_ICONASTERISK);
        SetFocus(e);
        return false;
    }
    std::wstring name = Text(dlg, editId, 31);
    int other = Find(v, name);
    if (other < 0 || other == self)
        return true;
    if (Message(dlg, caption, 7201, MB_OKCANCEL) != IDOK) {
        SetFocus(e);
        return false;
    }
    RemoveAt(v, other, selected);
    if (removed)
        *removed = other;
    return true;
}

// ---- "View settings" -------------------------------------------------------------------------

struct ViewCtx {
    Sheet* sh;
    int page;
    HWND list = nullptr;
};

INT_PTR CALLBACK ViewProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<ViewCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        c = reinterpret_cast<ViewCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        c->list = CreateSettingsList(dlg, IDC_VS_TAB, IDC_VS_LIST, c->page);
        FillSettingsList(GetDlgItem(dlg, IDC_VS_TAB), c->list, &c->sh->cur);
        return TRUE;
    case WM_SYSCOLORCHANGE:
        if (c)
            ListColors(c->list);
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (c && nm->code == TCN_SELCHANGE)
            FillSettingsList(GetDlgItem(dlg, IDC_VS_TAB), c->list, &c->sh->cur);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_VS_OK || LOWORD(wp) == IDCANCEL) {
            if (c)
                DestroyWindow(c->list);
            EndDialog(dlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

// ---- "Add configuration" ---------------------------------------------------------------------

struct AddCtx {
    Sheet* sh;
    int page;
    HWND list = nullptr;
    bool expanded = false;
    SIZE fullSize = {}, smallSize = {};           // window sizes
    int buttonTop[2] = {};                        // expanded / collapsed
};

const int kAddHidden[] = { IDC_PA_TAB, IDC_PA_LIST, IDC_PA_SEP2 };   // dword_6A91A258
const int kAddButtons[] = { IDC_PA_OK, IDC_PA_CANCEL, IDC_PA_HELP };

RECT ChildRect(HWND dlg, HWND ctl)
{
    RECT r;
    GetWindowRect(ctl, &r);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

void Layout(HWND dlg, AddCtx* c)
{
    // sub_6A90BCCF; like the original, both sizes come out 2*SM_CXFRAME wider than the template
    RECT w;
    GetWindowRect(dlg, &w);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&w), 2);   // window rect, client coordinates
    int fx = GetSystemMetrics(SM_CXFRAME), fy = GetSystemMetrics(SM_CYFRAME), cap = GetSystemMetrics(SM_CYCAPTION);
    int width = w.right + 2 * fx - w.left;
    c->fullSize = { width, cap + w.bottom + 2 * fy };
    RECT ok = ChildRect(dlg, GetDlgItem(dlg, IDC_PA_OK));
    RECT help = ChildRect(dlg, GetDlgItem(dlg, IDC_PA_HELP));
    RECT last = ChildRect(dlg, GetDlgItem(dlg, IDC_PA_SEP2));
    RECT sep = ChildRect(dlg, GetDlgItem(dlg, IDC_PA_SEP));
    int margin = cap + w.bottom + 2 * fy - help.bottom;
    c->buttonTop[0] = ok.top;
    c->buttonTop[1] = ok.top - last.bottom + sep.bottom;
    c->smallSize = { width, c->buttonTop[1] + margin + (ok.bottom - ok.top) };
}

void Expand(HWND dlg, AddCtx* c, bool on)
{
    // sub_6A90BE62
    SetDlgItemTextW(dlg, IDC_PA_DETAILS, LoadStr(on ? 7203 : 7202).c_str());
    for (int id : kAddHidden) {
        HWND h = id == IDC_PA_LIST ? c->list : GetDlgItem(dlg, id);
        if (h)
            ShowWindow(h, on ? SW_SHOW : SW_HIDE);
    }
    if (c->list)
        ShowWindow(GetDlgItem(dlg, IDC_PA_TAB), on ? SW_SHOW : SW_HIDE);
    for (int id : kAddButtons) {
        HWND h = GetDlgItem(dlg, id);
        RECT r = ChildRect(dlg, h);
        MoveWindow(h, r.left, c->buttonTop[on ? 0 : 1], r.right - r.left, r.bottom - r.top, TRUE);
    }
    SIZE s = on ? c->fullSize : c->smallSize;
    SetWindowPos(dlg, nullptr, 0, 0, s.cx, s.cy, SWP_NOMOVE | SWP_NOZORDER);
    c->expanded = on;
}

INT_PTR CALLBACK AddProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<AddCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        // sub_6A90C14B
        c = reinterpret_cast<AddCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        c->list = CreateSettingsList(dlg, IDC_PA_TAB, IDC_PA_LIST, c->page);
        FillSettingsList(GetDlgItem(dlg, IDC_PA_TAB), c->list, &c->sh->cur);
        Layout(dlg, c);
        Expand(dlg, c, false);
        FillIconList(dlg, IDC_PA_ICON);
        SendDlgItemMessageW(dlg, IDC_PA_NAME, EM_LIMITTEXT, kNameMax, 0);
        SendDlgItemMessageW(dlg, IDC_PA_NOTES, EM_LIMITTEXT, kNotesMax, 0);
        SendDlgItemMessageW(dlg, IDC_PA_ICON, LB_SETCURSEL, 0, 0);
        return TRUE;
    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        mi->itemWidth = 40;
        mi->itemHeight = 50;
        return TRUE;
    }
    case WM_DRAWITEM:
        DrawIconCell(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_CHARTOITEM:
        return reinterpret_cast<HWND>(lp) == GetDlgItem(dlg, IDC_PA_ICON) ? -2 : 0;
    case WM_SYSCOLORCHANGE:
        if (c)
            ListColors(c->list);
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (c && nm->code == TCN_SELCHANGE && nm->idFrom == IDC_PA_TAB)
            FillSettingsList(GetDlgItem(dlg, IDC_PA_TAB), c->list, &c->sh->cur);
        return TRUE;
    }
    case WM_COMMAND:
        if (!c)
            return TRUE;
        switch (LOWORD(wp)) {
        case IDC_PA_ICON:
            ClampIconSelection(dlg, IDC_PA_ICON);
            break;
        case IDC_PA_DETAILS:
            Expand(dlg, c, !c->expanded);
            break;
        case IDC_PA_OK: {
            if (HIWORD(wp) != BN_CLICKED)
                break;
            // sub_6A90C206 / sub_6A90BF6D
            ProfileList& pl = c->sh->profiles;
            std::vector<Profile> items = pl.items;
            int selected = pl.selected;
            if (!CheckName(dlg, IDC_PA_NAME, items, -1, selected, 7200, nullptr))
                break;
            Profile p;
            p.name = Text(dlg, IDC_PA_NAME, 31);
            p.notes = Text(dlg, IDC_PA_NOTES, 191);
            LRESULT icon = SendDlgItemMessageW(dlg, IDC_PA_ICON, LB_GETCURSEL, 0, 0);
            p.icon = icon == LB_ERR ? 0 : int(icon);
            p.s = c->sh->cur;
            items.push_back(p);
            pl.items = items;
            pl.selected = selected;
            if (!pl.Save())
                Message(dlg, 7304, 7308, MB_OK);
            pl.SaveSelection();
            DestroyWindow(c->list);
            EndDialog(dlg, IDOK);
            break;
        }
        case IDC_PA_CANCEL:
        case IDCANCEL:
            DestroyWindow(c->list);
            EndDialog(dlg, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

// ---- "Edit configuration" --------------------------------------------------------------------

struct EditCtx {
    Sheet* sh;
    int page;
    HWND list = nullptr;
    std::vector<Profile> items;
    int cur = -1;            // entry shown in the fields
    int selected = -1;       // configuration of the property sheet, followed through moves
    bool busy = false;       // a message box is up (sub_6A90CEFC re-entrancy guard)
    std::wstring dir;        // last import/export folder (PRESET_DIR)
    DWORD filter = 1;        // PRESET_INDEX
};

void FillList(HWND dlg, EditCtx* c)
{
    // sub_6A90C628 / sub_6A90D675
    HWND lb = GetDlgItem(dlg, IDC_PE_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < c->items.size(); i++)
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" "));
    if (c->cur >= 0)
        SendMessageW(lb, LB_SETCURSEL, WPARAM(c->cur), 0);
}

void EnableEdit(HWND dlg, EditCtx* c)
{
    // sub_6A90C8B9: everything but "Import" needs at least one configuration
    bool any = !c->items.empty();
    for (int id : { IDC_PE_LIST, IDC_PE_ICON, IDC_PE_NAME, IDC_PE_NOTES, IDC_PE_EXPORT, IDC_PE_DELETE, IDC_PE_UP,
                    IDC_PE_DOWN, 1150, 1156, 1158, 1160 })
        EnableWindow(GetDlgItem(dlg, id), any);
}

void ShowFields(HWND dlg, EditCtx* c)
{
    // sub_6A90C6EE
    const Profile* p = c->cur >= 0 ? &c->items[size_t(c->cur)] : nullptr;
    SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_SETCURSEL, WPARAM(c->cur), 0);
    SendDlgItemMessageW(dlg, IDC_PE_ICON, LB_SETCURSEL, WPARAM(p ? p->icon : -1), 0);
    SetDlgItemTextW(dlg, IDC_PE_NAME, p ? p->name.c_str() : L"");
    SetDlgItemTextW(dlg, IDC_PE_NOTES, p ? p->notes.c_str() : L"");
}

void ShowSettings(HWND dlg, EditCtx* c)
{
    // sub_6A90CB19
    FillSettingsList(GetDlgItem(dlg, IDC_PE_TAB), c->list, c->cur >= 0 ? &c->items[size_t(c->cur)].s : nullptr);
}

void StoreFields(HWND dlg, EditCtx* c)
{
    // sub_6A90C814
    if (c->cur < 0)
        return;
    LRESULT icon = SendDlgItemMessageW(dlg, IDC_PE_ICON, LB_GETCURSEL, 0, 0);
    if (icon < 0 || icon > kIcons)
        return;
    Profile& p = c->items[size_t(c->cur)];
    p.name = Text(dlg, IDC_PE_NAME, 31);
    p.icon = int(icon) < kIcons ? int(icon) : kIcons - 1;
    p.notes = Text(dlg, IDC_PE_NOTES, 191);
    HWND lb = GetDlgItem(dlg, IDC_PE_LIST);
    RECT r;
    SendMessageW(lb, LB_GETITEMRECT, WPARAM(c->cur), reinterpret_cast<LPARAM>(&r));
    InvalidateRect(lb, &r, TRUE);
}

// sub_6A90CEFC: 1 = the shown entry is fine, 2 = stay on it.
int Validate(HWND dlg, EditCtx* c)
{
    if (c->busy || c->cur < 0)
        return 1;
    c->busy = true;
    int removed;
    bool ok = CheckName(dlg, IDC_PE_NAME, c->items, c->cur, c->selected, 7300, &removed);
    c->busy = false;
    if (ok && removed >= 0) {
        // sub_6A90CC07
        if (c->items.empty())
            c->cur = -1;
        else if (removed <= c->cur && c->cur)
            c->cur--;
        FillList(dlg, c);
        EnableEdit(dlg, c);
    }
    return ok ? 1 : 2;
}

void Swap(HWND dlg, EditCtx* c, int a, int b)
{
    // sub_6A90CA08
    if (c->items.size() < 2 || b < 0 || b >= int(c->items.size()))
        return;
    std::swap(c->items[size_t(a)], c->items[size_t(b)]);
    if (c->selected == a)
        c->selected = b;
    else if (c->selected == b)
        c->selected = a;
    HWND lb = GetDlgItem(dlg, IDC_PE_LIST);
    InvalidateRect(lb, nullptr, TRUE);
    c->cur = b;
    ShowFields(dlg, c);
}

void Delete(HWND dlg, EditCtx* c)
{
    // sub_6A90CC38: no confirmation
    int i = c->cur;
    if (i < 0)
        return;
    RemoveAt(c->items, i, c->selected);
    SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_DELETESTRING, WPARAM(i), 0);
    if (c->items.empty())
        c->cur = -1;
    else if (i <= c->cur && c->cur)
        c->cur--;
    ShowFields(dlg, c);
    if (c->cur == -1) {
        SetFocus(GetDlgItem(dlg, IDC_PE_IMPORT));
        SendDlgItemMessageW(dlg, IDC_PE_IMPORT, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        SendDlgItemMessageW(dlg, IDC_PE_DELETE, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
    } else {
        SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_SETCURSEL, WPARAM(c->cur), 0);
    }
    ShowSettings(dlg, c);
    EnableEdit(dlg, c);
}

// sub_6A90D38D: common file dialog setup. filter is Canon's "text|pattern|...|" string.
bool FileDialog(HWND dlg, EditCtx* c, bool save, std::wstring& path)
{
    std::wstring filter = LoadStr(7302);
    if (filter.empty())
        return false;
    wchar_t sep = filter.back();
    size_t first = filter.find(sep);
    for (auto& ch : filter)
        if (ch == sep)
            ch = 0;
    filter.push_back(0);
    std::wstring ext = first != std::wstring::npos ? filter.substr(first + 3, 3) : L"cfg";
    wchar_t file[520] = {}, title[260] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = c->filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 518;
    ofn.lpstrFileTitle = title;
    ofn.nMaxFileTitle = 258;
    ofn.lpstrInitialDir = c->dir.c_str();
    ofn.lpstrDefExt = ext.c_str();
    ofn.Flags = OFN_LONGNAMES | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)))
        return false;
    path = file;
    // sub_6A90D480: remember the folder and the filter
    c->dir = path.substr(0, ofn.nFileOffset);
    c->filter = ofn.nFilterIndex;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, c->sh->profiles.key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                        nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"Dir", 0, REG_SZ, reinterpret_cast<const BYTE*>(c->dir.c_str()),
                       DWORD((c->dir.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(k, L"FilterIndex", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&c->filter), sizeof(DWORD));
        RegCloseKey(k);
    }
    return true;
}

void Import(HWND dlg, EditCtx* c)
{
    // sub_6A90D6E9 + sub_6A90D570
    std::wstring path;
    if (!FileDialog(dlg, c, false, path))
        return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") || !f) {
        Message(dlg, 7300, 7305, MB_OK);
        SetFocus(GetDlgItem(dlg, IDC_PE_LIST));
        return;
    }
    char magic[8] = {};
    Rec r = {};
    size_t n1 = fread(magic, sizeof(magic), 1, f);
    size_t n2 = fread(&r, sizeof(r), 1, f);
    bool err = ferror(f) != 0;
    fclose(f);
    Profile p;
    if (err) {
        Message(dlg, 7300, 7305, MB_OK);
        return;
    }
    if (n1 != 1 || n2 != 1 || memcmp(magic, kFileMagic, sizeof(magic)) || !FromRec(r, p)) {
        Message(dlg, 7300, 7303, MB_OK);
        return;
    }
    int other = Find(c->items, p.name);
    if (other >= 0) {
        if (Message(dlg, 7300, 7201, MB_OKCANCEL) != IDOK)
            return;
        RemoveAt(c->items, other, c->selected);
        if (c->items.empty())
            c->cur = -1;
        else if (other <= c->cur && c->cur)
            c->cur--;
    }
    if (int(c->items.size()) >= ProfileList::kMax) {
        Message(dlg, 7300, 7204, MB_OK);
        return;
    }
    c->items.push_back(p);
    c->cur = int(c->items.size()) - 1;
    FillList(dlg, c);
    EnableEdit(dlg, c);
    ShowFields(dlg, c);
    ShowSettings(dlg, c);
}

void Export(HWND dlg, EditCtx* c)
{
    // sub_6A90D7F8 + sub_6A90D79C
    if (c->cur < 0)
        return;
    std::wstring path;
    if (!FileDialog(dlg, c, true, path))
        return;
    Rec r = ToRec(c->items[size_t(c->cur)]);
    FILE* f = nullptr;
    bool ok = !_wfopen_s(&f, path.c_str(), L"wb") && f;
    if (ok) {
        ok = fwrite(kFileMagic, sizeof(kFileMagic), 1, f) == 1 && fwrite(&r, sizeof(r), 1, f) == 1;
        ok = (fclose(f) == 0) && ok;
    }
    if (!ok)
        Message(dlg, 7300, 7306, MB_OK);
}

void DrawMoveButton(const DRAWITEMSTRUCT* di)
{
    // sub_6A90D8CD: icons 162/164/163 (up) and 165/167/166 (down)
    if (di->itemAction == ODA_FOCUS) {
        DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }
    UINT base = di->CtlID == IDC_PE_UP ? 162 : 165;
    UINT id = (di->itemState & ODS_SELECTED) ? base + 1 : ((di->itemState & ODS_DISABLED) ? base + 2 : base);
    int saved = SaveDC(di->hDC);
    RECT r;
    GetWindowRect(di->hwndItem, &r);
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    HPEN pen = CreatePen(PS_SOLID, 1, face);
    HBRUSH br = CreateSolidBrush(face);
    SelectObject(di->hDC, pen);
    SelectObject(di->hDC, br);
    Rectangle(di->hDC, 0, 0, r.right - r.left, r.bottom - r.top);
    DrawIcon(di->hDC, 0, 0, Icon(id));
    RestoreDC(di->hDC, saved);
    DeleteObject(br);
    DeleteObject(pen);
}

INT_PTR CALLBACK EditProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<EditCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        // sub_6A90CCE3
        c = reinterpret_cast<EditCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        FillIconList(dlg, IDC_PE_ICON);
        SendDlgItemMessageW(dlg, IDC_PE_NAME, EM_LIMITTEXT, kNameMax, 0);
        SendDlgItemMessageW(dlg, IDC_PE_NOTES, EM_LIMITTEXT, kNotesMax, 0);
        c->list = CreateSettingsList(dlg, IDC_PE_TAB, IDC_PE_LIST_VIEW, c->page);
        // the move buttons are 24x24, the lower one aligned to the bottom of its template rect
        HWND up = GetDlgItem(dlg, IDC_PE_UP), down = GetDlgItem(dlg, IDC_PE_DOWN);
        RECT r = ChildRect(dlg, down);
        MoveWindow(down, r.left, r.bottom - 24, 24, 24, TRUE);
        MoveWindow(up, r.left, r.bottom - 24 - 25, 24, 24, TRUE);
        wchar_t dir[MAX_PATH] = {};
        DWORD cb = sizeof(dir);
        if (RegGetValueW(HKEY_CURRENT_USER, c->sh->profiles.key.c_str(), L"Dir", RRF_RT_REG_SZ, nullptr, dir, &cb))
            GetSystemDirectoryW(dir, MAX_PATH);
        c->dir = dir;
        cb = sizeof(DWORD);
        if (RegGetValueW(HKEY_CURRENT_USER, c->sh->profiles.key.c_str(), L"FilterIndex", RRF_RT_REG_DWORD, nullptr,
                         &c->filter, &cb))
            c->filter = 1;
        if (!c->items.empty()) {
            c->cur = 0;
            FillList(dlg, c);
            EnableEdit(dlg, c);
            ShowFields(dlg, c);
            ShowSettings(dlg, c);
        } else {
            EnableEdit(dlg, c);
            c->cur = -1;
            FillSettingsList(GetDlgItem(dlg, IDC_PE_TAB), c->list, nullptr, true);
        }
        return TRUE;
    }
    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi->CtlID == IDC_PE_ICON) {
            mi->itemWidth = 40;
            mi->itemHeight = 50;
        } else if (mi->CtlID == IDC_PE_LIST) {
            mi->itemHeight = 18;
        }
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!c || di->itemID == UINT(-1) && di->CtlType != ODT_BUTTON)
            return TRUE;
        if (di->CtlID == IDC_PE_ICON)
            DrawIconCell(di);
        else if (di->CtlID == IDC_PE_LIST && di->itemID < c->items.size())
            DrawProfile(di, c->items[di->itemID], false, true);
        else if (di->CtlID == IDC_PE_UP || di->CtlID == IDC_PE_DOWN)
            DrawMoveButton(di);
        return TRUE;
    }
    case WM_CHARTOITEM: {
        HWND h = reinterpret_cast<HWND>(lp);
        return h == GetDlgItem(dlg, IDC_PE_ICON) || h == GetDlgItem(dlg, IDC_PE_LIST) ? -2 : 0;
    }
    case WM_SYSCOLORCHANGE:
        if (c)
            ListColors(c->list);
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (c && nm->code == TCN_SELCHANGE && nm->idFrom == IDC_PE_TAB)
            ShowSettings(dlg, c);
        return TRUE;
    }
    case WM_COMMAND: {
        if (!c)
            return TRUE;
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDC_PE_LIST:
            if (code == LBN_SELCHANGE) {
                if (Validate(dlg, c) == 1) {
                    StoreFields(dlg, c);
                    LRESULT n = SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_GETCURSEL, 0, 0);
                    c->cur = n == LB_ERR ? 0 : int(n);
                    ShowFields(dlg, c);
                    ShowSettings(dlg, c);
                } else {
                    // sub_6A90DD9C
                    SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_SETCURSEL, WPARAM(c->cur), 0);
                    SendDlgItemMessageW(dlg, IDC_PE_ICON, LB_SETCURSEL,
                                        WPARAM(c->cur >= 0 ? c->items[size_t(c->cur)].icon : -1), 0);
                }
            }
            break;
        case IDC_PE_ICON:
            ClampIconSelection(dlg, IDC_PE_ICON);
            break;
        case IDC_PE_DELETE:
            if (code == BN_CLICKED)
                Delete(dlg, c);
            break;
        case IDC_PE_UP:
        case IDC_PE_DOWN:
            if (code == BN_CLICKED && c->cur >= 0) {
                StoreFields(dlg, c);
                Swap(dlg, c, c->cur, id == IDC_PE_UP ? c->cur - 1 : c->cur + 1);
                ShowSettings(dlg, c);
            }
            break;
        case IDC_PE_IMPORT:
        case IDC_PE_EXPORT:
            if (code != BN_CLICKED)
                break;
            if (!c->items.empty()) {
                if (Validate(dlg, c) != 1) {
                    SendDlgItemMessageW(dlg, IDC_PE_LIST, LB_SETCURSEL, WPARAM(c->cur), 0);
                    break;
                }
                StoreFields(dlg, c);
            }
            if (id == IDC_PE_IMPORT)
                Import(dlg, c);
            else
                Export(dlg, c);
            break;
        case IDC_PE_OK:
            if (code != BN_CLICKED)
                break;
            if (!c->items.empty()) {
                if (Validate(dlg, c) != 1)
                    break;
                StoreFields(dlg, c);
            }
            DestroyWindow(c->list);
            EndDialog(dlg, IDOK);
            break;
        case IDC_PE_CANCEL:
        case IDCANCEL:
            DestroyWindow(c->list);
            EndDialog(dlg, IDCANCEL);
            break;
        }
        return TRUE;
    }
    }
    return FALSE;
}

} // namespace

// ---- storage ---------------------------------------------------------------------------------

void ProfileList::Load(const std::wstring& printer)
{
    key = KeyFor(printer);
    items.clear();
    selected = -1;
    modified = false;
    DWORD perm = 0, cb = sizeof(perm);
    enabled = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Permission", RRF_RT_REG_DWORD, nullptr, &perm, &cb) ||
              perm != 1;
    cb = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"List", RRF_RT_REG_BINARY, nullptr, nullptr, &cb) == ERROR_SUCCESS &&
        cb >= sizeof(Rec)) {
        std::vector<Rec> recs(cb / sizeof(Rec));
        cb = DWORD(recs.size() * sizeof(Rec));
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"List", RRF_RT_REG_BINARY, nullptr, recs.data(), &cb) ==
            ERROR_SUCCESS)
            for (const Rec& r : recs) {
                Profile p;
                if (int(items.size()) < kMax && FromRec(r, p))
                    items.push_back(p);
            }
    }
    // sub_6A90B628: PRESET_SELECT keeps the configuration last used and whether it was changed.
    DWORD sel[2] = {};
    cb = sizeof(sel);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"Select", RRF_RT_REG_BINARY, nullptr, sel, &cb) == ERROR_SUCCESS &&
        cb == sizeof(sel) && int(sel[0]) >= 0 && int(sel[0]) < int(items.size())) {
        selected = int(sel[0]);
        modified = sel[1] != 0;
    }
}

bool ProfileList::Save() const
{
    std::vector<Rec> recs;
    for (const Profile& p : items)
        recs.push_back(ToRec(p));
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
        return false;
    LSTATUS st = recs.empty() ? RegDeleteValueW(k, L"List")
                              : RegSetValueExW(k, L"List", 0, REG_BINARY, reinterpret_cast<const BYTE*>(recs.data()),
                                               DWORD(recs.size() * sizeof(Rec)));
    RegCloseKey(k);
    return st == ERROR_SUCCESS || (recs.empty() && st == ERROR_FILE_NOT_FOUND);
}

void ProfileList::SaveSelection() const
{
    if (key.empty())
        return;
    DWORD sel[2] = { DWORD(selected), modified ? 1u : 0u };
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
        return;
    RegSetValueExW(k, L"Select", 0, REG_BINARY, reinterpret_cast<const BYTE*>(sel), sizeof(sel));
    RegCloseKey(k);
}

// ---- page combo ------------------------------------------------------------------------------

void InitProfileCombo(HWND page, Sheet* sh)
{
    // sub_6A90B6E3
    if (!sh->profiles.enabled) {
        for (int id : { IDC_PROFILE, IDC_PROFILE_ADD, IDC_PROFILE_EDIT, 1001 }) {
            EnableWindow(GetDlgItem(page, id), FALSE);
            ShowWindow(GetDlgItem(page, id), SW_HIDE);
        }
        return;
    }
    HWND cb = GetDlgItem(page, IDC_PROFILE);
    SendMessageW(cb, CB_SETITEMHEIGHT, WPARAM(-1), 18);
    FillProfileCombo(page, sh);
}

void FillProfileCombo(HWND page, Sheet* sh)
{
    // sub_6A90A597: one blank item per configuration, drawn by DrawProfileItem
    HWND cb = GetDlgItem(page, IDC_PROFILE);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < sh->profiles.items.size(); i++)
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" "));
    SendMessageW(cb, CB_SETCURSEL, WPARAM(sh->profiles.selected), 0);
}

bool DrawProfileItem(HWND, Sheet* sh, const DRAWITEMSTRUCT* di)
{
    if (di->itemID == UINT(-1) || di->itemID >= sh->profiles.items.size())
        return false;
    bool marker = int(di->itemID) == sh->profiles.selected && sh->profiles.modified;
    DrawProfile(di, sh->profiles.items[di->itemID], marker, false);
    return true;
}

void OnProfileCombo(HWND page, Sheet* sh)
{
    // sub_6A90B45F
    LRESULT n = SendDlgItemMessageW(page, IDC_PROFILE, CB_GETCURSEL, 0, 0);
    if (n == CB_ERR)
        return;
    SelectProfile(page, sh, int(n));
}

// ---- dialogs ---------------------------------------------------------------------------------

void ViewSettingsDialog(HWND owner, Sheet* sh, int page)
{
    ViewCtx c{ sh, page };
    DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_VIEW_SETTINGS), owner, ViewProc, reinterpret_cast<LPARAM>(&c));
}

void AddProfileDialog(HWND owner, Sheet* sh)
{
    // sub_6A90C5D0 + sub_6A90AEEF: at most 50 configurations
    if (int(sh->profiles.items.size()) >= ProfileList::kMax) {
        Message(owner, 7200, 7204, MB_OK);
        return;
    }
    int page = 0;
    for (int i = 0; i < 3; i++)
        if (sh->hwndPage[i] == owner)
            page = i;
    AddCtx c{ sh, page };
    if (DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_PROFILE_ADD), owner, AddProc, reinterpret_cast<LPARAM>(&c)) ==
        IDOK)
        FillProfileCombo(owner, sh);
}

void EditProfilesDialog(HWND owner, Sheet* sh)
{
    // sub_6A90DD2E
    int page = 0;
    for (int i = 0; i < 3; i++)
        if (sh->hwndPage[i] == owner)
            page = i;
    EditCtx c{ sh, page };
    c.items = sh->profiles.items;
    c.selected = sh->profiles.selected;
    if (DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_PROFILE_EDIT), owner, EditProc, reinterpret_cast<LPARAM>(&c)) !=
        IDOK)
        return;
    ProfileList& pl = sh->profiles;
    pl.items = c.items;
    pl.selected = c.selected;
    if (pl.selected < 0)
        pl.modified = false;
    if (!pl.Save())
        Message(owner, 7304, 7308, MB_OK);
    pl.SaveSelection();
    FillProfileCombo(owner, sh);
}

} // namespace ui
