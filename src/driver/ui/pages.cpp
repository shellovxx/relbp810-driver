// The three document property pages. Control handling follows CAP1UIK.DLL: page setup
// sub_6A907036, finishing sub_6A907B7F, quality sub_6A908CB2 and the helpers named below.
#include "ui.h"

namespace ui {
namespace {

enum { PG_SETUP, PG_FINISH, PG_QUALITY };

// Page order pictures: 2-up portrait, 2-up landscape, 4/8/9-up.
HICON g_orderIcons[8];

void LoadOrderIcons()
{
    if (g_orderIcons[0])
        return;
    for (int i = 0; i < 8; i++)
        g_orderIcons[i] = LoadIconW(g_module, MAKEINTRESOURCEW(IDI_ORDER_FIRST + i));
}

// sub_6A906042 / sub_6A906067: list position <-> stored order of the 4/8/9-up layouts.
int OrderSwap(int v)
{
    return v == 1 ? 2 : (v == 2 ? 1 : v);
}

void Enable(HWND page, int id, bool on)
{
    EnableWindow(GetDlgItem(page, id), on);
}

void Show(HWND page, int id, bool on)
{
    ShowWindow(GetDlgItem(page, id), on ? SW_SHOW : SW_HIDE);
}

LRESULT Send(HWND page, int id, UINT msg, WPARAM wp = 0, LPARAM lp = 0)
{
    return SendDlgItemMessageW(page, id, msg, wp, lp);
}

void Check(HWND page, int id, bool on)
{
    Send(page, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED);
}

bool Checked(HWND page, int id)
{
    return Send(page, id, BM_GETCHECK) == BST_CHECKED;
}

void SetInt(HWND page, int id, int v)
{
    SetDlgItemInt(page, id, UINT(v), TRUE);
}

void SelectAll(HWND page, int id, int from = 0)
{
    Send(page, id, EM_SETSEL, WPARAM(from), -1);
}

// sub_6A9050FD: only (half-width) digits.
bool OnlyDigits(HWND page, int id)
{
    wchar_t t[256] = {};
    GetDlgItemTextW(page, id, t, 256);
    for (const wchar_t* p = t; *p; p++) {
        WORD c3 = 0, c1 = 0;
        GetStringTypeW(CT_CTYPE3, p, 1, &c3);
        if (c3 & C3_FULLWIDTH)
            return false;
        GetStringTypeW(CT_CTYPE1, p, 1, &c1);
        if (!(c1 & C1_DIGIT))
            return false;
    }
    return true;
}

// sub_6A907F07: keeps a leading minus and the digits, clamps to -80..80.
// Returns 1 if the text was fine, 0 if empty, -1 if it had to be corrected (fixed in out).
int FilterSigned(const wchar_t* in, std::wstring& out)
{
    size_t n = wcslen(in);
    if (!n)
        return 0;
    std::wstring s;
    const wchar_t* p = in;
    if (*p == L'-') {
        s.push_back(L'-');
        p++;
    }
    for (; *p; p++)
        if (*p >= L'0' && *p <= L'9')
            s.push_back(*p);
    if (s.size() > 16)
        s.resize(16);
    int v = _wtoi(s.c_str());
    if (v > lbpdm::kMaxAdjust)
        s = std::to_wstring(lbpdm::kMaxAdjust);
    else if (v < lbpdm::kMinAdjust)
        s = std::to_wstring(lbpdm::kMinAdjust);
    out = s;
    return s == in ? 1 : -1;
}

void InitSpin(HWND page, int spin, int edit, int lo, int hi, int pos)
{
    Send(page, spin, UDM_SETRANGE32, WPARAM(lo), LPARAM(hi));
    Send(page, spin, UDM_SETPOS32, 0, LPARAM(pos));
    Send(page, edit, EM_LIMITTEXT, 3);
}

// The common part of all three pages: configuration combo and buttons.
bool CommonCommand(HWND page, Sheet* sh, int id, int code, int pageIndex)
{
    switch (id) {
    case IDC_PROFILE:
        if (code == CBN_SELCHANGE) {
            OnProfileCombo(page, sh);
            sh->Changed(page, false);
        }
        return true;
    case IDC_PROFILE_ADD:
        AddProfileDialog(page, sh);
        return true;
    case IDC_PROFILE_EDIT:
        EditProfilesDialog(page, sh);
        return true;
    case IDC_VIEW_SETTINGS:
        ViewSettingsDialog(page, sh, pageIndex);
        return true;
    }
    return false;
}

void InitCommon(HWND page, Sheet* sh, int index)
{
    SetWindowLongPtrW(page, DWLP_USER, reinterpret_cast<LONG_PTR>(sh));
    sh->hwndPage[index] = page;
    for (int id : { IDC_PREVIEW, IDC_PREVIEW_ICON, IDC_HALFTONE_IMAGE })
        if (HWND h = GetDlgItem(page, id))
            SendMessageW(h, WM_LBP_SETSHEET, 0, reinterpret_cast<LPARAM>(sh));
    InitProfileCombo(page, sh);
}

void Apply(HWND page, Sheet* sh, int index)
{
    // sub_6A9033A4: CPSFUNC_SET_RESULT for this page
    sh->Save();
    if (sh->pfnComPropSheet && size_t(index) < sh->pages.size())
        sh->pfnComPropSheet(sh->hComPropSheet, CPSFUNC_SET_RESULT, reinterpret_cast<LPARAM>(sh->pages[size_t(index)]),
                            CPSUI_OK);
    SetWindowLongPtrW(page, DWLP_MSGRESULT, PSNRET_NOERROR);
}

// ---- Page setup ------------------------------------------------------------------------------

void FillOrder(HWND page, const Settings& s)
{
    // sub_6A90608B
    if (!s.layout)
        return;
    HWND cb = GetDlgItem(page, IDC_ORDER);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    int first, n, sel;
    if (s.layout == 1) {
        first = s.landscape ? 2 : 0;
        n = 2;
        sel = s.order[0];
    } else {
        first = 4;
        n = 4;
        sel = OrderSwap(s.order[s.layout - 1]);
    }
    for (int i = 0; i < n; i++) {
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L" "));
        SendMessageW(cb, CB_SETITEMDATA, WPARAM(i), reinterpret_cast<LPARAM>(g_orderIcons[first + i]));
    }
    SendMessageW(cb, CB_SETCURSEL, WPARAM(sel), 0);
    SendMessageW(cb, CB_SETITEMHEIGHT, WPARAM(-1), 36);
}

void LayoutControls(HWND page, const Settings& s)
{
    // sub_6A905D4C
    bool nup = s.layout != 0;
    Enable(page, IDC_OUTPUT_SIZE, !nup);
    Enable(page, IDC_OUTPUT_LABEL, !nup);
    Enable(page, IDC_MANUAL_SCALE, !nup);
    Enable(page, IDC_ORDER_LABEL, nup);
    Show(page, IDC_ORDER, nup);
    Show(page, IDC_ORDER_LABEL, nup);
    for (int id : { IDC_MANUAL_SCALE, IDC_SCALE, IDC_SCALE_SPIN, IDC_SCALE_PERCENT })
        Show(page, id, !nup);
}

void FillPageSetup(HWND page, Sheet* sh)
{
    // sub_6A90673E
    Settings& s = sh->cur;
    sh->updating = true;
    HWND cb = GetDlgItem(page, IDC_PAPER_SIZE);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < kPaperCount; i++)
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStr(kPapers[i].nameId).c_str()));
    SendMessageW(cb, CB_SETCURSEL, WPARAM(s.paper), 0);
    cb = GetDlgItem(page, IDC_OUTPUT_SIZE);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStr(5050).c_str()));
    for (int i = 0; i < kPaperCount; i++)
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStr(kPapers[i].nameId).c_str()));
    SendMessageW(cb, CB_SETCURSEL, WPARAM(s.output), 0);
    InitSpin(page, IDC_COPIES_SPIN, IDC_COPIES, 1, 999, s.copies);
    SetInt(page, IDC_COPIES, s.copies);
    Check(page, IDC_PORTRAIT, !s.landscape);
    Check(page, IDC_LANDSCAPE, s.landscape);
    cb = GetDlgItem(page, IDC_LAYOUT);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 5; i++)
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStr(5061 + i).c_str()));
    SendMessageW(cb, CB_SETCURSEL, WPARAM(s.layout), 0);
    LayoutControls(page, s);
    FillOrder(page, s);
    Check(page, IDC_MANUAL_SCALE, s.manualScale);
    Enable(page, IDC_SCALE, s.manualScale);
    Enable(page, IDC_SCALE_SPIN, s.manualScale);
    InitSpin(page, IDC_SCALE_SPIN, IDC_SCALE, lbpdm::kMinScale, lbpdm::kMaxScale, s.scale);
    SetInt(page, IDC_SCALE, s.scale);
    sh->updating = false;
    UpdatePreview(page);
}

void DrawOrderItem(Sheet* sh, const DRAWITEMSTRUCT* di)
{
    // sub_6A906DC6
    if (di->itemID == UINT(-1))
        return;
    if (di->itemAction == ODA_FOCUS) {
        DrawFocusRect(di->hDC, &di->rcItem);
        return;
    }
    if (di->itemAction != ODA_DRAWENTIRE && di->itemAction != ODA_SELECT)
        return;
    bool sel = (di->itemState & ODS_SELECTED) != 0;
    COLORREF bg = GetSysColor(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    HPEN pen = CreatePen(PS_SOLID, 1, bg);
    HBRUSH br = CreateSolidBrush(bg);
    HGDIOBJ op = SelectObject(di->hDC, pen), ob = SelectObject(di->hDC, br);
    Rectangle(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
    SelectObject(di->hDC, ob);
    DeleteObject(br);
    SelectObject(di->hDC, op);
    DeleteObject(pen);
    auto icon = reinterpret_cast<HICON>(SendMessageW(di->hwndItem, CB_GETITEMDATA, di->itemID, 0));
    if (icon)
        DrawIcon(di->hDC, di->rcItem.left, di->rcItem.top, icon);
    const Settings& s = sh->cur;
    UINT base = 0;
    switch (s.layout) {
    case 1: base = s.landscape ? 5073 : 5071; break;
    case 2: base = 5081; break;
    case 3: base = 5091; break;
    case 4: base = 5101; break;
    }
    if (!base)
        return;
    COLORREF oldText = 0, oldBk = 0;
    if (sel) {
        oldText = SetTextColor(di->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
        oldBk = SetBkColor(di->hDC, bg);
    }
    std::wstring t = LoadStr(base + di->itemID);
    TEXTMETRICW tm;
    GetTextMetricsW(di->hDC, &tm);
    int y = di->rcItem.top + (di->rcItem.bottom - tm.tmHeight - di->rcItem.top) / 2;
    ExtTextOutW(di->hDC, 38, y, ETO_CLIPPED, &di->rcItem, t.c_str(), UINT(t.size()), nullptr);
    if (sel) {
        SetBkColor(di->hDC, oldBk);
        SetTextColor(di->hDC, oldText);
    }
}

void OnCopies(HWND page, Sheet* sh)
{
    // sub_6A905B58
    Settings& s = sh->cur;
    if (!OnlyDigits(page, IDC_COPIES)) {
        MessageBeep(MB_ICONASTERISK);
        SetDlgItemInt(page, IDC_COPIES, UINT(s.copies), FALSE);
        return;
    }
    int v = int(GetDlgItemInt(page, IDC_COPIES, nullptr, TRUE));
    if (v != s.copies)
        sh->Changed(page);
    Send(page, IDC_COPIES_SPIN, UDM_SETPOS32, 0, v);
    s.copies = v;
    if (v < 1 || v > 999) {
        s.copies = v < 1 ? 1 : 999;
        SetDlgItemInt(page, IDC_COPIES, UINT(s.copies), FALSE);
        SelectAll(page, IDC_COPIES);
        MessageBeep(MB_ICONASTERISK);
    }
    Send(page, IDC_COPIES_SPIN, UDM_SETPOS32, 0, s.copies);
}

void OnScale(HWND page, Sheet* sh)
{
    // sub_6A9065E9
    Settings& s = sh->cur;
    if (!OnlyDigits(page, IDC_SCALE)) {
        MessageBeep(MB_ICONASTERISK);
        SetDlgItemInt(page, IDC_SCALE, UINT(s.scale), FALSE);
        UpdatePreview(page);
        return;
    }
    int v = int(GetDlgItemInt(page, IDC_SCALE, nullptr, TRUE));
    if (v != s.scale)
        sh->Changed(page);
    if (v >= lbpdm::kMinScale && v <= lbpdm::kMaxScale) {
        Send(page, IDC_SCALE_SPIN, UDM_SETPOS32, 0, v);
        s.scale = v;
    }
    if (v <= 0 || v > lbpdm::kMaxScale) {
        SetDlgItemInt(page, IDC_SCALE, v <= 0 ? lbpdm::kMinScale : lbpdm::kMaxScale, FALSE);
        SelectAll(page, IDC_SCALE);
        MessageBeep(MB_ICONASTERISK);
    } else if (v < lbpdm::kMinScale) {
        // typing "1" turns into "10" with the "0" selected, so the next digit replaces it
        SetDlgItemInt(page, IDC_SCALE, UINT(10 * v), FALSE);
        SelectAll(page, IDC_SCALE, 1);
    }
    UpdatePreview(page);
}

void PageSetupCommand(HWND page, Sheet* sh, int id, int code, HWND ctl)
{
    Settings& s = sh->cur;
    if (sh->updating)
        return;
    switch (id) {
    case IDC_PAPER_SIZE:
        if (code == CBN_SELCHANGE) {
            // sub_6A905805: a new size resets the scale to the automatic one
            s.paper = int(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
            s.scale = AutoScale(s);
            SetInt(page, IDC_SCALE, s.scale);
            UpdatePreview(page);
            sh->Changed(page);
        }
        break;
    case IDC_OUTPUT_SIZE:
        if (code == CBN_SELCHANGE) {
            // sub_6A905A2B
            s.output = int(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
            s.scale = AutoScale(s);
            SetInt(page, IDC_SCALE, s.scale);
            UpdatePreview(page);
            sh->Changed(page);
        }
        break;
    case IDC_COPIES:
        if (code == EN_UPDATE)
            OnCopies(page, sh);
        break;
    case IDC_PORTRAIT:
    case IDC_LANDSCAPE: {
        // sub_6A905CD6: every click counts as a change
        bool landscape = !Checked(page, IDC_PORTRAIT);
        if (landscape != s.landscape) {
            s.landscape = landscape;
            FillOrder(page, s);
            UpdatePreview(page);
        }
        sh->Changed(page);
        break;
    }
    case IDC_LAYOUT:
        if (code == CBN_SELCHANGE) {
            // sub_6A905F72
            s.layout = int(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
            if (s.layout) {
                s.output = 0;
                Send(page, IDC_OUTPUT_SIZE, CB_SETCURSEL, 0);
                LayoutControls(page, s);
                FillOrder(page, s);
                s.scale = AutoScale(s);
                SetInt(page, IDC_SCALE, s.scale);
            } else {
                LayoutControls(page, s);
            }
            UpdatePreview(page);
            sh->Changed(page);
        }
        break;
    case IDC_ORDER:
        if (code == CBN_SELCHANGE) {
            // sub_6A90632C
            int sel = int(SendMessageW(ctl, CB_GETCURSEL, 0, 0));
            if (s.layout == 1)
                s.order[0] = sel;
            else if (s.layout > 1)
                s.order[s.layout - 1] = OrderSwap(sel);
            UpdatePreview(page);
            sh->Changed(page);
        }
        break;
    case IDC_MANUAL_SCALE:
        // sub_6A90644A
        s.manualScale = Checked(page, IDC_MANUAL_SCALE);
        if (!s.manualScale) {
            s.scale = AutoScale(s);
            SetInt(page, IDC_SCALE, s.scale);
        }
        Enable(page, IDC_SCALE, s.manualScale);
        Enable(page, IDC_SCALE_SPIN, s.manualScale);
        UpdatePreview(page);
        sh->Changed(page);
        break;
    case IDC_SCALE:
        if (code == EN_UPDATE)
            OnScale(page, sh);
        break;
    case IDC_OVERLAY:
        if (OverlayDialog(page, sh) == IDOK && sh->profiles.selected >= 0 && !sh->profiles.modified) {
            sh->profiles.modified = true;
            for (HWND h : sh->hwndPage)
                if (h)
                    FillProfileCombo(h, sh);
        }
        break;
    case IDC_PAGESETUP_RESET:
        ResetPageSetup(s);
        FillPageSetup(page, sh);
        SetFocus(GetDlgItem(page, IDC_PAGESETUP_RESET));
        sh->Changed(page);
        break;
    default:
        CommonCommand(page, sh, id, code, PG_SETUP);
        if (id == IDC_PROFILE && code == CBN_SELCHANGE) {
            Normalize(s);
            FillPageSetup(page, sh);
        }
        break;
    }
}

// ---- Finishing -------------------------------------------------------------------------------

LRESULT CALLBACK CollatePictureProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto old = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HWND page = GetParent(hwnd);
        auto br = reinterpret_cast<HBRUSH>(SendMessageW(page, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                                                        reinterpret_cast<LPARAM>(hwnd)));
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_BTNFACE));
        if (Sheet* sh = SheetOf(page))
            PaintCollate(hwnd, dc, sh->cur.collate);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

void FillFinishing(HWND page, Sheet* sh)
{
    Check(page, IDC_COLLATE, sh->cur.collate);
    InvalidateRect(GetDlgItem(page, IDC_COLLATE_ICON), nullptr, FALSE);
    UpdatePreview(page);
}

void FinishingCommand(HWND page, Sheet* sh, int id, int code)
{
    // sub_6A90741A
    Settings& s = sh->cur;
    switch (id) {
    case IDC_COLLATE:
        s.collate = Checked(page, IDC_COLLATE);
        InvalidateRect(GetDlgItem(page, IDC_COLLATE_ICON), nullptr, FALSE);
        sh->Changed(page);
        InvalidateRect(GetDlgItem(page, IDC_PREVIEW_ICON), nullptr, FALSE);
        break;
    case IDC_FINISH_SETTINGS:
        if (FinishingSettingsDialog(page, sh) == IDOK)
            sh->Changed(page);
        break;
    case IDC_FINISHING_RESET:
        ResetFinishing(s);
        Check(page, IDC_COLLATE, false);
        InvalidateRect(GetDlgItem(page, IDC_COLLATE_ICON), nullptr, FALSE);
        sh->Changed(page);
        UpdatePreview(page);
        break;
    default:
        CommonCommand(page, sh, id, code, PG_FINISH);
        if (id == IDC_PROFILE && code == CBN_SELCHANGE) {
            Normalize(s);
            FillFinishing(page, sh);
        }
        break;
    }
}

// ---- Quality ---------------------------------------------------------------------------------

const int kHalftoneButtons[] = { IDC_HT_SMOOTH, IDC_HT_PATTERN1, IDC_HT_PATTERN2 };   // nIDCheckButton

int HalftoneOf(int id)
{
    // sub_6A907EE8
    return id == IDC_HT_PATTERN1 ? lbpdm::HT_PATTERN1 : (id == IDC_HT_PATTERN2 ? lbpdm::HT_PATTERN2 : lbpdm::HT_SMOOTH);
}

// sub_6A907C75: the sliders ignore PgUp/PgDn/Up/Down and the mouse wheel.
LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto old = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && (wp == VK_PRIOR || wp == VK_NEXT || wp == VK_UP || wp == VK_DOWN))
        return 1;
    if (msg == WM_MOUSEWHEEL)
        return 1;
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

void Subclass(HWND ctl, WNDPROC proc)
{
    if (!ctl)
        return;
    LONG_PTR old = SetWindowLongPtrW(ctl, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc));
    SetWindowLongPtrW(ctl, GWLP_USERDATA, old);
}

void InitAdjust(HWND page, int slider, int spin, int edit, int value)
{
    // sub_6A9082C4 / sub_6A908329
    HWND sl = GetDlgItem(page, slider);
    SendMessageW(sl, TBM_SETRANGEMIN, FALSE, lbpdm::kMinAdjust);
    SendMessageW(sl, TBM_SETRANGEMAX, FALSE, lbpdm::kMaxAdjust);
    SendMessageW(sl, TBM_SETTICFREQ, 20, 0);
    SendMessageW(sl, TBM_SETPOS, TRUE, value);
    SendMessageW(sl, TBM_SETPAGESIZE, 0, 20);
    Send(page, spin, UDM_SETRANGE32, WPARAM(lbpdm::kMinAdjust), lbpdm::kMaxAdjust);
    Send(page, spin, UDM_SETPOS32, 0, value);
    Send(page, edit, EM_LIMITTEXT, 3);
    SetInt(page, edit, value);
}

struct QualityState {
    int lastBrightness = 0, lastContrast = 0;   // +24316 / +24320
};
QualityState* QState(HWND page)
{
    return reinterpret_cast<QualityState*>(GetPropW(page, L"LBP810Q"));
}

void FillQuality(HWND page, Sheet* sh)
{
    // sub_6A9080E4
    const Settings& s = sh->cur;
    CheckRadioButton(page, IDC_RES600, IDC_RES300, s.res300 ? IDC_RES300 : IDC_RES600);
    CheckRadioButton(page, IDC_HT_PATTERN1, IDC_HT_SMOOTH, kHalftoneButtons[s.halftone]);
    if (QualityState* q = QState(page)) {
        q->lastBrightness = s.brightness;
        q->lastContrast = s.contrast;
    }
    SetInt(page, IDC_BRIGHTNESS_EDIT, s.brightness);
    SetInt(page, IDC_CONTRAST_EDIT, s.contrast);
    UpdatePreview(page);
}

void OnAdjust(HWND page, Sheet* sh, int edit)
{
    // sub_6A908586 / sub_6A9086B6
    bool bright = edit == IDC_BRIGHTNESS_EDIT;
    HWND e = GetDlgItem(page, edit);
    wchar_t t[16] = {};
    GetWindowTextW(e, t, 16);
    std::wstring fixed;
    int r = FilterSigned(t, fixed);
    if (r < 0) {
        MessageBeep(MB_ICONEXCLAMATION);
        SetWindowTextW(e, fixed.c_str());
        SendMessageW(e, EM_SETSEL, 0, -1);
        return;
    }
    if (r == 0)
        SetInt(page, edit, 0);
    int v = int(GetDlgItemInt(page, edit, nullptr, TRUE));
    Send(page, bright ? IDC_BRIGHTNESS : IDC_CONTRAST, TBM_SETPOS, TRUE, v);
    Send(page, bright ? IDC_BRIGHTNESS_SPIN : IDC_CONTRAST_SPIN, UDM_SETPOS32, 0, v);
    int& value = bright ? sh->cur.brightness : sh->cur.contrast;
    value = 2 * (StoredAdjust(v) - 50);
    InvalidateRect(GetDlgItem(page, IDC_HALFTONE_IMAGE), nullptr, FALSE);
    if (QualityState* q = QState(page)) {
        int& last = bright ? q->lastBrightness : q->lastContrast;
        if (v != last) {
            last = v;
            sh->Changed(page);
        }
    }
}

void QualityCommand(HWND page, Sheet* sh, int id, int code)
{
    // sub_6A9089E1
    Settings& s = sh->cur;
    if (sh->updating)
        return;
    switch (id) {
    case IDC_RES600:
    case IDC_RES300:
        if (IsDlgButtonChecked(page, id) == BST_CHECKED) {
            sh->Changed(page);
            s.res300 = id == IDC_RES300;
            InvalidateRect(GetDlgItem(page, IDC_HALFTONE_IMAGE), nullptr, FALSE);
        }
        break;
    case IDC_HT_PATTERN1:
    case IDC_HT_PATTERN2:
    case IDC_HT_SMOOTH:
        if (IsDlgButtonChecked(page, id) == BST_CHECKED) {
            sh->Changed(page);
            s.halftone = HalftoneOf(id);
            InvalidateRect(GetDlgItem(page, IDC_HALFTONE_IMAGE), nullptr, FALSE);
        }
        break;
    case IDC_BRIGHTNESS_EDIT:
    case IDC_CONTRAST_EDIT:
        if (code == EN_UPDATE)
            OnAdjust(page, sh, id);
        break;
    case IDC_ABOUT:
        AboutDialog(page);
        break;
    case IDC_ADVANCED:
        if (QualityAdvancedDialog(page, sh))
            sh->Changed(page);
        break;
    case IDC_QUALITY_RESET:
        // sub_6A908498 + sub_6A90819E
        sh->Changed(page);
        ResetQuality(s);
        CheckRadioButton(page, IDC_RES600, IDC_RES300, IDC_RES600);
        CheckRadioButton(page, IDC_HT_PATTERN1, IDC_HT_SMOOTH, IDC_HT_PATTERN1);
        SetInt(page, IDC_BRIGHTNESS_EDIT, 0);
        SetInt(page, IDC_CONTRAST_EDIT, 0);
        InvalidateRect(GetDlgItem(page, IDC_HALFTONE_IMAGE), nullptr, FALSE);
        break;
    default:
        CommonCommand(page, sh, id, code, PG_QUALITY);
        if (id == IDC_PROFILE && code == CBN_SELCHANGE) {
            Normalize(s);
            FillQuality(page, sh);
        }
        break;
    }
}

void QualityScroll(HWND page, HWND ctl, WPARAM wp)
{
    // sub_6A908BA3: slider -> edit
    int id = GetDlgCtrlID(ctl);
    if (id != IDC_BRIGHTNESS && id != IDC_CONTRAST)
        return;
    int code = LOWORD(wp);
    int pos;
    if (code == TB_THUMBTRACK)
        pos = short(HIWORD(wp));
    else if (code <= TB_PAGEDOWN || code == TB_TOP || code == TB_BOTTOM)
        pos = int(SendMessageW(ctl, TBM_GETPOS, 0, 0));
    else
        return;
    SetInt(page, id == IDC_BRIGHTNESS ? IDC_BRIGHTNESS_EDIT : IDC_CONTRAST_EDIT, pos);
}

// ---- dialog procedures -----------------------------------------------------------------------

bool CommonMessage(HWND page, UINT msg, WPARAM, LPARAM lp, Sheet* sh, INT_PTR& result)
{
    switch (msg) {
    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi->CtlID == IDC_PROFILE)
            mi->itemHeight = 18;            // sub_6A90B54C
        else if (mi->CtlID == IDC_ORDER)
            mi->itemHeight = 36;            // sub_6A906D99
        result = TRUE;
        return true;
    }
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (sh && di->CtlID == IDC_PROFILE)
            DrawProfileItem(page, sh, di);
        else if (sh && di->CtlID == IDC_ORDER)
            DrawOrderItem(sh, di);
        result = TRUE;
        return true;
    }
    }
    return false;
}

} // namespace

Sheet* SheetOf(HWND page)
{
    return reinterpret_cast<Sheet*>(GetWindowLongPtrW(page, DWLP_USER));
}

INT_PTR CALLBACK PageSetupProc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    Sheet* sh = SheetOf(page);
    INT_PTR result = FALSE;
    switch (msg) {
    case WM_INITDIALOG:
        sh = reinterpret_cast<Sheet*>(reinterpret_cast<PROPSHEETPAGEW*>(lp)->lParam);
        LoadOrderIcons();
        InitCommon(page, sh, PG_SETUP);
        FillPageSetup(page, sh);
        return TRUE;
    case WM_DESTROY:
        if (sh)
            sh->hwndPage[PG_SETUP] = nullptr;
        return FALSE;
    case WM_COMMAND:
        if (sh)
            PageSetupCommand(page, sh, LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
        return TRUE;
    case WM_VSCROLL:
        // sub_6A906D5E: the spin buttons have no buddy integer
        if (sh) {
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (id == IDC_COPIES_SPIN)
                SetInt(page, IDC_COPIES, short(HIWORD(wp)));
            else if (id == IDC_SCALE_SPIN)
                SetInt(page, IDC_SCALE, short(HIWORD(wp)));
        }
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (!sh)
            break;
        if (nm->code == PSN_SETACTIVE) {
            Normalize(sh->cur);
            FillPageSetup(page, sh);
            FillProfileCombo(page, sh);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_APPLY) {
            Apply(page, sh, PG_SETUP);
            return TRUE;
        }
        break;
    }
    default:
        if (CommonMessage(page, msg, wp, lp, sh, result))
            return result;
    }
    return FALSE;
}

INT_PTR CALLBACK FinishingProc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    Sheet* sh = SheetOf(page);
    INT_PTR result = FALSE;
    switch (msg) {
    case WM_INITDIALOG:
        sh = reinterpret_cast<Sheet*>(reinterpret_cast<PROPSHEETPAGEW*>(lp)->lParam);
        InitCommon(page, sh, PG_FINISH);
        Subclass(GetDlgItem(page, IDC_COLLATE_ICON), CollatePictureProc);
        Check(page, IDC_COLLATE, sh->cur.collate);
        return TRUE;
    case WM_DESTROY:
        if (sh)
            sh->hwndPage[PG_FINISH] = nullptr;
        return FALSE;
    case WM_COMMAND:
        if (sh)
            FinishingCommand(page, sh, LOWORD(wp), HIWORD(wp));
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (!sh)
            break;
        if (nm->code == PSN_SETACTIVE) {
            Normalize(sh->cur);
            FillFinishing(page, sh);
            FillProfileCombo(page, sh);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_APPLY) {
            Apply(page, sh, PG_FINISH);
            return TRUE;
        }
        break;
    }
    default:
        if (CommonMessage(page, msg, wp, lp, sh, result))
            return result;
    }
    return FALSE;
}

INT_PTR CALLBACK QualityProc(HWND page, UINT msg, WPARAM wp, LPARAM lp)
{
    Sheet* sh = SheetOf(page);
    INT_PTR result = FALSE;
    switch (msg) {
    case WM_INITDIALOG: {
        // sub_6A9087E6
        sh = reinterpret_cast<Sheet*>(reinterpret_cast<PROPSHEETPAGEW*>(lp)->lParam);
        auto* q = new (std::nothrow) QualityState;
        SetPropW(page, L"LBP810Q", q);
        InitCommon(page, sh, PG_QUALITY);
        const Settings& s = sh->cur;
        sh->updating = true;
        CheckRadioButton(page, IDC_RES600, IDC_RES300, s.res300 ? IDC_RES300 : IDC_RES600);
        CheckRadioButton(page, IDC_HT_PATTERN1, IDC_HT_SMOOTH, kHalftoneButtons[s.halftone]);
        InitAdjust(page, IDC_BRIGHTNESS, IDC_BRIGHTNESS_SPIN, IDC_BRIGHTNESS_EDIT, s.brightness);
        InitAdjust(page, IDC_CONTRAST, IDC_CONTRAST_SPIN, IDC_CONTRAST_EDIT, s.contrast);
        sh->updating = false;
        if (q) {
            q->lastBrightness = s.brightness;
            q->lastContrast = s.contrast;
        }
        Subclass(GetDlgItem(page, IDC_BRIGHTNESS), SliderProc);
        Subclass(GetDlgItem(page, IDC_CONTRAST), SliderProc);
        return TRUE;
    }
    case WM_DESTROY:
        delete QState(page);
        RemovePropW(page, L"LBP810Q");
        if (sh)
            sh->hwndPage[PG_QUALITY] = nullptr;
        return FALSE;
    case WM_COMMAND:
        if (sh)
            QualityCommand(page, sh, LOWORD(wp), HIWORD(wp));
        return TRUE;
    case WM_HSCROLL:
        if (sh)
            QualityScroll(page, reinterpret_cast<HWND>(lp), wp);
        return TRUE;
    case WM_VSCROLL:
        // sub_6A908C30
        if (sh) {
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (id == IDC_BRIGHTNESS_SPIN)
                SetInt(page, IDC_BRIGHTNESS_EDIT, short(HIWORD(wp)));
            else if (id == IDC_CONTRAST_SPIN)
                SetInt(page, IDC_CONTRAST_EDIT, short(HIWORD(wp)));
        }
        return TRUE;
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (!sh)
            break;
        if (nm->code == PSN_SETACTIVE) {
            // sub_6A90893E
            Normalize(sh->cur);
            sh->updating = true;
            FillQuality(page, sh);
            sh->updating = false;
            // the edits were filled while updating: bring the sliders along
            for (int id : { IDC_BRIGHTNESS_EDIT, IDC_CONTRAST_EDIT }) {
                int v = int(GetDlgItemInt(page, id, nullptr, TRUE));
                bool b = id == IDC_BRIGHTNESS_EDIT;
                Send(page, b ? IDC_BRIGHTNESS : IDC_CONTRAST, TBM_SETPOS, TRUE, v);
                Send(page, b ? IDC_BRIGHTNESS_SPIN : IDC_CONTRAST_SPIN, UDM_SETPOS32, 0, v);
            }
            FillProfileCombo(page, sh);
            SetWindowLongPtrW(page, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_APPLY) {
            Apply(page, sh, PG_QUALITY);
            return TRUE;
        }
        break;
    }
    default:
        if (CommonMessage(page, msg, wp, lp, sh, result))
            return result;
    }
    return FALSE;
}

void SelectProfile(HWND, Sheet* sh, int index)
{
    // sub_6A90B100: everything but the page size comes from the configuration.
    if (index < 0 || index >= int(sh->profiles.items.size()))
        return;
    int paper = sh->cur.paper;
    sh->cur = sh->profiles.items[size_t(index)].s;
    sh->cur.paper = paper;
    Normalize(sh->cur);
    sh->profiles.selected = index;
    sh->profiles.modified = false;
    for (int i = 0; i < 3; i++) {
        HWND h = sh->hwndPage[i];
        if (!h)
            continue;
        if (i == PG_SETUP)
            FillPageSetup(h, sh);
        else if (i == PG_FINISH)
            FillFinishing(h, sh);
        else {
            sh->updating = true;
            FillQuality(h, sh);
            sh->updating = false;
            for (int id : { IDC_BRIGHTNESS_EDIT, IDC_CONTRAST_EDIT }) {
                int v = int(GetDlgItemInt(h, id, nullptr, TRUE));
                bool b = id == IDC_BRIGHTNESS_EDIT;
                SendDlgItemMessageW(h, b ? IDC_BRIGHTNESS : IDC_CONTRAST, TBM_SETPOS, TRUE, v);
                SendDlgItemMessageW(h, b ? IDC_BRIGHTNESS_SPIN : IDC_CONTRAST_SPIN, UDM_SETPOS32, 0, v);
            }
        }
        FillProfileCombo(h, sh);
    }
}

} // namespace ui
