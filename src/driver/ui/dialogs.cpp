// Dialogs behind the page buttons: finishing "Settings" (sub_6A90E11F), quality "Advanced"
// (sub_6A90E55D), "About" (sub_6A908F44) and "Overlay" (DialogFunc, only its initial state
// so far: printing overlays is not implemented yet).
#include "ui.h"
#include <cstdio>

namespace ui {
namespace {

struct FinishCtx {
    Sheet* sh;
    bool mismatch, emf;   // values to restore on Cancel
};

INT_PTR CALLBACK FinishSettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* c = reinterpret_cast<FinishCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        // sub_6A90DE05
        c = reinterpret_cast<FinishCtx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        CheckDlgButton(dlg, IDC_FS_EMF, c->sh->cur.emfSpool ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_FS_MISMATCH, c->sh->cur.ignoreMismatch ? BST_CHECKED : BST_UNCHECKED);
        c->mismatch = c->sh->cur.ignoreMismatch;
        c->emf = c->sh->cur.emfSpool;
        return TRUE;
    case WM_COMMAND:
        if (!c)
            return TRUE;
        switch (LOWORD(wp)) {
        case IDC_FS_MISMATCH:
            c->sh->cur.ignoreMismatch = IsDlgButtonChecked(dlg, IDC_FS_MISMATCH) == BST_CHECKED;
            break;
        case IDC_FS_EMF:
            c->sh->cur.emfSpool = IsDlgButtonChecked(dlg, IDC_FS_EMF) == BST_CHECKED;
            break;
        case IDC_FS_OK:
            EndDialog(dlg, IDOK);
            break;
        case IDC_FS_CANCEL:
        case IDCANCEL:
            c->sh->cur.ignoreMismatch = c->mismatch;
            c->sh->cur.emfSpool = c->emf;
            EndDialog(dlg, IDCANCEL);
            break;
        case IDC_FS_RESET:
            // sub_6A90DFAD
            c->sh->cur.ignoreMismatch = false;
            CheckDlgButton(dlg, IDC_FS_MISMATCH, BST_UNCHECKED);
            c->sh->cur.emfSpool = false;
            CheckDlgButton(dlg, IDC_FS_EMF, BST_UNCHECKED);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK AdvancedProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* sh = reinterpret_cast<Sheet*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        // sub_6A90E447: density 0..4 (Canon keeps 0/15/31/47/63)
        sh = reinterpret_cast<Sheet*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        HWND tb = GetDlgItem(dlg, IDC_ADV_DENSITY);
        SendMessageW(tb, TBM_SETRANGE, FALSE, MAKELONG(0, 4));
        SendMessageW(tb, TBM_SETTICFREQ, 1, 0);
        SendMessageW(tb, TBM_SETPOS, TRUE, sh->cur.density);
        CheckDlgButton(dlg, IDC_ADV_TONERSAVE, sh->cur.tonerSave ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_ADV_REFINE, sh->cur.refinement ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (!sh)
            return FALSE;
        switch (LOWORD(wp)) {
        case IDC_ADV_OK: {
            // sub_6A90E3E3
            int pos = int(SendDlgItemMessageW(dlg, IDC_ADV_DENSITY, TBM_GETPOS, 0, 0));
            sh->cur.density = pos < 0 || pos > 4 ? 2 : pos;
            sh->cur.tonerSave = IsDlgButtonChecked(dlg, IDC_ADV_TONERSAVE) == BST_CHECKED;
            sh->cur.refinement = IsDlgButtonChecked(dlg, IDC_ADV_REFINE) == BST_CHECKED;
            EndDialog(dlg, 1);
            break;
        }
        case IDC_ADV_CANCEL:
        case IDCANCEL:
            EndDialog(dlg, 0);
            break;
        case IDC_ADV_RESET:
            // sub_6A90E35F: only the controls; the values change with OK
            SendDlgItemMessageW(dlg, IDC_ADV_DENSITY, TBM_SETPOS, TRUE, 2);
            CheckDlgButton(dlg, IDC_ADV_TONERSAVE, BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_ADV_REFINE, BST_CHECKED);
            break;
        }
        return FALSE;
    }
    return FALSE;
}

std::wstring ModuleVersion()
{
    // sub_6A908EA6: "%d.%02d.%01d.%03d" of the UI DLL's file version
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        return L"Unknown";
    DWORD h = 0, n = GetFileVersionInfoSizeW(path, &h);
    std::vector<BYTE> buf(n);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!n || !GetFileVersionInfoW(path, 0, n, buf.data()) ||
        !VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi || !len)
        return L"Unknown";
    wchar_t v[64];
    swprintf_s(v, L"%d.%02d.%01d.%03d", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
               HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return v;
}

INT_PTR CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    if (msg == WM_INITDIALOG) {
        SetDlgItemTextW(dlg, IDC_ABOUT_VERSION, ModuleVersion().c_str());
        return TRUE;
    }
    if (msg == WM_COMMAND && (LOWORD(wp) == IDC_ABOUT_OK || LOWORD(wp) == IDCANCEL)) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ---- Overlay -----------------------------------------------------------------------------------

// OverlayBitmap (sub_6A9131DA): the sheet at 90% of the control, white with a black edge.
LRESULT CALLBACK OverlayPreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        auto* sh = reinterpret_cast<Sheet*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        int pw = 4960, ph = 7016;   // A4 at 600 dpi
        if (sh) {
            const PaperDef& p = kPapers[sh->cur.paper];
            pw = p.width * 600 / 254;
            ph = p.height * 600 / 254;
        }
        bool portrait = !sh || !sh->cur.landscape;
        int w = portrait ? pw : ph, h = portrait ? ph : pw;
        int s = 1000 * rc.right / w, t = 1000 * rc.bottom / h;
        if (s > t)
            s = t;
        int sw = 9 * w * s / 10000, shh = 9 * h * s / 10000;
        int l = (rc.right - sw) / 2, tp = (rc.bottom - shh) / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(WHITE_BRUSH));
        Rectangle(dc, l, tp, l + sw, tp + shh);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

INT_PTR CALLBACK OverlayProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        auto* sh = reinterpret_cast<Sheet*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SetWindowLongPtrW(GetDlgItem(dlg, IDC_OVL_PREVIEW), GWLP_USERDATA, lp);
        HWND cb = GetDlgItem(dlg, IDC_OVL_FILE);
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStr(6004).c_str()));
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        CheckRadioButton(dlg, IDC_OVL_BACK, IDC_OVL_FRONT, sh->cur.overlayFront ? IDC_OVL_FRONT : IDC_OVL_BACK);
        CheckRadioButton(dlg, IDC_OVL_CM, IDC_OVL_INCH, sh->cur.overlayInch ? IDC_OVL_INCH : IDC_OVL_CM);
        SetDlgItemTextW(dlg, IDC_OVL_LEFT, L"0.00");
        SetDlgItemTextW(dlg, IDC_OVL_TOP, L"0.00");
        SetDlgItemTextW(dlg, IDC_OVL_WIDTH, L"1.00");
        SetDlgItemTextW(dlg, IDC_OVL_HEIGHT, L"1.00");
        // with "(Нет)" selected everything about the placement is disabled
        for (int id = 1305; id <= 1329; id++)
            EnableWindow(GetDlgItem(dlg, id), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_OVL_RESET), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_OVL_BROWSE), FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OVL_OK:
            EndDialog(dlg, IDOK);
            break;
        case IDC_OVL_CANCEL:
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}

} // namespace

bool RegisterOverlayClass(HINSTANCE inst)
{
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayPreviewProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LBP810_Overlay";
    return RegisterClassW(&wc) != 0;
}

void UnregisterOverlayClass(HINSTANCE inst)
{
    UnregisterClassW(L"LBP810_Overlay", inst);
}

INT_PTR FinishingSettingsDialog(HWND owner, Sheet* sh)
{
    FinishCtx c{ sh, false, false };
    return DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_FINISH_SETTINGS), owner, FinishSettingsProc,
                           reinterpret_cast<LPARAM>(&c));
}

INT_PTR QualityAdvancedDialog(HWND owner, Sheet* sh)
{
    return DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_ADVANCED), owner, AdvancedProc, reinterpret_cast<LPARAM>(sh));
}

void AboutDialog(HWND owner)
{
    DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProc, 0);
}

INT_PTR OverlayDialog(HWND owner, Sheet* sh)
{
    return DialogBoxParamW(g_module, MAKEINTRESOURCEW(IDD_OVERLAY), owner, OverlayProc, reinterpret_cast<LPARAM>(sh));
}

} // namespace ui
