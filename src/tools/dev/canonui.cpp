// Development harness: shows printing preferences or printer properties for an existing printer
// and saves a screenshot of every tab and of the dialogs behind the tab buttons.
// x86 build: the original Canon CAP1UIK.DLL (the reference screenshots).
//   usage: canonui <printer> <dir with Canon files> <out dir> [doc|dev|states]
//   The Canon files must be in <dir>\3 (CAP1UIK.DLL builds paths as <driver dir>\3\...).
// x64 build: the system UNIDRVUI.DLL with our GPD and plug-ins from a package folder, so the
// UI can be checked without installing the driver.
//   usage: canonui <printer> <out\package\amd64> <out dir> [doc|dev|states]
#include <windows.h>
#include <winspool.h>
#include <commctrl.h>
#include <prsht.h>
#include <winddiui.h>
#include <compstui.h>
#include <cstdio>
#include <string>
#include <vector>

static std::wstring g_dir;      // fake printer driver directory
static std::wstring g_out;
static std::vector<BYTE> g_di3; // packed DRIVER_INFO_3W
static decltype(&GetPrinterDriverW) g_realGetPrinterDriverW;
static decltype(&GetPrinterDriverDirectoryW) g_realGetPrinterDriverDirectoryW;

static void BuildDriverInfo()
{
#ifdef _WIN64
    // Unidrv from the installed system, everything of ours from the package folder.
    wchar_t sysdir[MAX_PATH];
    DWORD cb = 0;
    GetPrinterDriverDirectoryW(nullptr, nullptr, 1, reinterpret_cast<LPBYTE>(sysdir), sizeof(sysdir), &cb);
    std::wstring sys = std::wstring(sysdir) + L"\\3\\", pkg = g_dir + L"\\";
    std::wstring deps;
    for (auto f : { L"CNLB810.INI", L"CNLB810R.DLL", L"CNLB810U.DLL", L"LB810RES.DLL" })
        deps += pkg + f + L'\0';
    for (auto f : { L"UNIRES.DLL", L"STDNAMES.GPD", L"STDDTYPE.GDL", L"STDSCHEM.GDL", L"STDSCHMX.GDL" })
        deps += sys + f + L'\0';
    // Unidrv looks for the OEM plug-ins next to its UI DLL (pConfigFile) and for *Include files
    // next to the GPD: the staging folder gets a copy of UNIDRVUI.DLL and StdNames.gpd.
    for (auto f : { L"UNIDRVUI.DLL", L"STDNAMES.GPD" })
        CopyFileW((sys + f).c_str(), (pkg + f).c_str(), FALSE);
    std::vector<std::wstring> s = { L"Canon LBP-810", L"Windows x64", sys + L"UNIDRV.DLL", pkg + L"CNLB810.GPD",
                                    pkg + L"UNIDRVUI.DLL", sys + L"UNIDRV.HLP", deps, L"", L"" };
#else
    std::wstring d3 = g_dir + L"\\3\\";
    std::vector<std::wstring> s = { L"Canon LBP-810", L"Windows NT x86", d3 + L"CAP1K.DLL", d3 + L"CAP1K.DLL",
                                    d3 + L"CAP1UIK.DLL", d3 + L"CAP1A.HLP", L"", L"", L"" };
#endif
    // A DRIVER_INFO_8W serves every level (1..8 share the prefix); the extra strings are empty.
    std::vector<std::wstring> extra = { std::wstring(1, L'\0'), L"Canon", L"", L"", L"", L"winprint", L"",
                                        std::wstring(1, L'\0'), L"", std::wstring(1, L'\0') };
    size_t off = sizeof(DRIVER_INFO_8W);
    size_t total = off;
    for (auto& x : s)
        total += (x.size() + 2) * sizeof(wchar_t);
    for (auto& x : extra)
        total += (x.size() + 2) * sizeof(wchar_t);
    g_di3.assign(total, 0);
    auto* di = reinterpret_cast<DRIVER_INFO_8W*>(g_di3.data());
    di->cVersion = 3;
    LPWSTR* fields[] = { &di->pName, &di->pEnvironment, &di->pDriverPath, &di->pDataFile, &di->pConfigFile,
                         &di->pHelpFile, &di->pDependentFiles, &di->pMonitorName, &di->pDefaultDataType };
    LPWSTR* more[] = { &di->pszzPreviousNames, &di->pszMfgName, &di->pszOEMUrl, &di->pszHardwareID, &di->pszProvider,
                       &di->pszPrintProcessor, &di->pszVendorSetup, &di->pszzColorProfiles, &di->pszInfPath,
                       &di->pszzCoreDriverDependencies };
    auto put = [&](LPWSTR* f, const std::wstring& v) {
        auto* q = reinterpret_cast<wchar_t*>(g_di3.data() + off);
        memcpy(q, v.c_str(), v.size() * sizeof(wchar_t));
        *f = q;
        off += (v.size() + 2) * sizeof(wchar_t);
    };
    for (size_t i = 0; i < s.size(); i++)
        put(fields[i], s[i]);
    for (size_t i = 0; i < extra.size(); i++)
        put(more[i], extra[i]);
}

static BOOL WINAPI HookGetPrinterDriverW(HANDLE, LPWSTR, DWORD level, LPBYTE buf, DWORD cb, LPDWORD need)
{
    *need = DWORD(g_di3.size());
    if (!buf || cb < g_di3.size()) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buf, g_di3.data(), g_di3.size());
    // relocate the string pointers
    auto* dst = reinterpret_cast<DRIVER_INFO_8W*>(buf);
    LPWSTR* ptrs[] = { &dst->pName, &dst->pEnvironment, &dst->pDriverPath, &dst->pDataFile, &dst->pConfigFile,
                       &dst->pHelpFile, &dst->pDependentFiles, &dst->pMonitorName, &dst->pDefaultDataType,
                       &dst->pszzPreviousNames, &dst->pszMfgName, &dst->pszOEMUrl, &dst->pszHardwareID,
                       &dst->pszProvider, &dst->pszPrintProcessor, &dst->pszVendorSetup, &dst->pszzColorProfiles,
                       &dst->pszInfPath, &dst->pszzCoreDriverDependencies };
    for (LPWSTR* f : ptrs)
        if (*f)
            *f = reinterpret_cast<LPWSTR>(buf + (reinterpret_cast<BYTE*>(*f) - g_di3.data()));
    if (level == 1)
        reinterpret_cast<DRIVER_INFO_1W*>(buf)->pName = dst->pName;
    return TRUE;
}

static BOOL WINAPI HookGetPrinterDriverDirectoryW(LPWSTR, LPWSTR, DWORD, LPBYTE buf, DWORD cb, LPDWORD need)
{
    *need = DWORD((g_dir.size() + 1) * sizeof(wchar_t));
    if (!buf || cb < *need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buf, g_dir.c_str(), *need);
    return TRUE;
}

static void PatchImport(HMODULE mod, const char* dll, const char* fn, void* hook, void** orig)
{
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto& dirent = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dirent.VirtualAddress); imp->Name; imp++) {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll))
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(ibn->Name), fn))
                continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            if (orig)
                *orig = reinterpret_cast<void*>(iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            printf("patched %s!%s\n", dll, fn);
        }
    }
}

static void SaveWindow(HWND hwnd, const std::wstring& name)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    // Copy from the screen: PrintWindow returns black for these old-style property sheets.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
    Sleep(400);
    BitBlt(mem, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SelectObject(mem, old);
    DWORD stride = ((w * 3 + 3) & ~3u);
    BITMAPFILEHEADER fh = { 0x4D42, DWORD(sizeof(fh) + sizeof(bi.bmiHeader) + stride * h), 0, 0,
                            DWORD(sizeof(fh) + sizeof(bi.bmiHeader)) };
    std::wstring path = g_out + L"\\" + name + L".bmp";
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (f) {
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        fwrite(bits, stride, h, f);
        fclose(f);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    printf("saved %ls (%dx%d)\n", path.c_str(), w, h);
}

struct FindCtx {
    DWORD pid;
    HWND exclude;
    HWND found;
};

static BOOL CALLBACK FindDialog(HWND h, LPARAM lp)
{
    auto* c = reinterpret_cast<FindCtx*>(lp);
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (pid == c->pid && h != c->exclude && IsWindowVisible(h) && !wcscmp(cls, L"#32770")) {
        c->found = h;
        return FALSE;
    }
    return TRUE;
}

static HWND WaitDialog(HWND exclude, int ms)
{
    for (int t = 0; t < ms; t += 100) {
        FindCtx c = { GetCurrentProcessId(), exclude, nullptr };
        EnumWindows(FindDialog, reinterpret_cast<LPARAM>(&c));
        if (c.found)
            return c.found;
        Sleep(100);
    }
    return nullptr;
}

// Buttons opening sub-dialogs, per tab index (document properties).
struct SubDlg {
    int tab;
    int id;
    const wchar_t* name;
};
static const SubDlg kDocSub[] = {
    { 0, 1003, L"profile_add" },  { 0, 1004, L"profile_edit" }, { 0, 1010, L"view_settings" },
    { 0, 1226, L"overlay" },      { 1, 1353, L"finishing_settings" }, { 2, 1476, L"quality_advanced" },
    { 2, 1475, L"about" },
};
static bool g_doc = true;
static bool g_states = false;
static bool g_apply = false;
static bool g_hold = false;    // just show the sheet (for driving it from outside)   // change a few settings and press OK (x64: the DEVMODE is dumped)


static void DumpCombo(HWND page, int id)
{
    HWND cb = GetDlgItem(page, id);
    if (!cb)
        return;
    int n = int(SendMessageW(cb, CB_GETCOUNT, 0, 0));
    int sel = int(SendMessageW(cb, CB_GETCURSEL, 0, 0));
    printf("combo %d (%d items, sel %d, %s):", id, n, sel, IsWindowVisible(cb) ? "visible" : "hidden");
    for (int i = 0; i < n; i++) {
        wchar_t t[128] = {};
        SendMessageW(cb, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(t));
        char u[256];
        WideCharToMultiByte(CP_UTF8, 0, t, -1, u, sizeof(u), nullptr, nullptr);
        printf(" [%s]", u);
    }
    printf("\n");
}

static void SelectCombo(HWND page, int id, int idx)
{
    HWND cb = GetDlgItem(page, id);
    SendMessageW(cb, CB_SETCURSEL, idx, 0);
    SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(cb));
    Sleep(600);
}

static void Click(HWND page, int id)
{
    SendMessageW(GetDlgItem(page, id), BM_CLICK, 0, 0);
    Sleep(600);
}

static void States(HWND sheet)
{
    PropSheet_SetCurSel(sheet, nullptr, 0);
    Sleep(800);
    HWND page = PropSheet_GetCurrentPageHwnd(sheet);
    for (int id : { 1202, 1205, 1217, 1219 })
        DumpCombo(page, id);
    int layouts = int(SendDlgItemMessageW(page, 1217, CB_GETCOUNT, 0, 0));
    for (int i = 1; i < layouts; i++) {
        SelectCombo(page, 1217, i);
        DumpCombo(page, 1219);
        wchar_t nm[32];
        swprintf_s(nm, L"state_layout%d", i);
        SaveWindow(sheet, nm);
        if (i == 1) {
            SelectCombo(page, 1219, 1);
            SaveWindow(sheet, L"state_layout1_order1");
            SelectCombo(page, 1219, 0);
        }
    }
    SelectCombo(page, 1217, 0);
    Click(page, 1214);
    SaveWindow(sheet, L"state_landscape");
    Click(page, 1212);
    Click(page, 1220);
    SaveWindow(sheet, L"state_manualscale");
    Click(page, 1220);
    SelectCombo(page, 1205, 2);
    SaveWindow(sheet, L"state_outputsize");
    SelectCombo(page, 1205, 0);
    PropSheet_SetCurSel(sheet, nullptr, 2);
    Sleep(800);
    page = PropSheet_GetCurrentPageHwnd(sheet);
    Click(page, 1457);
    SaveWindow(sheet, L"state_pattern2");
    Click(page, 1458);
    SaveWindow(sheet, L"state_smoothing");
    Click(page, 1453);
    SaveWindow(sheet, L"state_300dpi");
    Click(page, 1452);
    Click(page, 1456);
    HWND tb = GetDlgItem(page, 1462);
    printf("brightness range %d..%d pos %d\n", int(SendMessageW(tb, TBM_GETRANGEMIN, 0, 0)),
           int(SendMessageW(tb, TBM_GETRANGEMAX, 0, 0)), int(SendMessageW(tb, TBM_GETPOS, 0, 0)));
    SendMessageW(tb, TBM_SETPOS, TRUE, SendMessageW(tb, TBM_GETRANGEMAX, 0, 0));
    SendMessageW(page, WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), reinterpret_cast<LPARAM>(tb));
    Sleep(600);
    SaveWindow(sheet, L"state_brightmax");
}

static void Apply(HWND sheet)
{
    PropSheet_SetCurSel(sheet, nullptr, 0);
    Sleep(800);
    HWND page = PropSheet_GetCurrentPageHwnd(sheet);
    SetDlgItemInt(page, 1207, 3, FALSE);       // copies
    Sleep(300);
    Click(page, 1214);                         // landscape
    SelectCombo(page, 1217, 2);                // 4 pages per sheet
    SelectCombo(page, 1219, 1);                // order: down from left
    PropSheet_SetCurSel(sheet, nullptr, 2);
    Sleep(800);
    page = PropSheet_GetCurrentPageHwnd(sheet);
    Click(page, 1457);                         // pattern 2
    SetDlgItemInt(page, 1464, 21, TRUE);       // brightness (kept as 20)
    Sleep(300);
    Click(page, 1453);                         // 300 dpi
    SaveWindow(sheet, L"apply_before_ok");
    PropSheet_PressButton(sheet, PSBTN_OK);
}

static DWORD WINAPI Shooter(LPVOID)
{
    HWND sheet = WaitDialog(nullptr, 30000);
    if (!sheet) {
        printf("no property sheet\n");
        return 0;
    }
    Sleep(1500);
    HWND tab = PropSheet_GetTabControl(sheet);
    int n = tab ? TabCtrl_GetItemCount(tab) : 1;
    printf("sheet %p, %d tabs\n", sheet, n);
    for (int i = 0; i < n; i++) {
        PropSheet_SetCurSel(sheet, nullptr, i);
        Sleep(1000);
        wchar_t nm[32];
        swprintf_s(nm, L"%s_tab%d", g_doc ? L"doc" : L"dev", i);
        SaveWindow(sheet, nm);
    }
    if (g_hold) {
        printf("holding sheet %p\n", sheet);
        return 0;
    }
    if (g_doc && g_apply) {
        Apply(sheet);
        return 0;
    } else if (g_doc && g_states) {
        States(sheet);
    } else if (g_doc) {
        for (auto& s : kDocSub) {
            if (s.tab >= n)
                continue;
            PropSheet_SetCurSel(sheet, nullptr, s.tab);
            Sleep(500);
            HWND page = PropSheet_GetCurrentPageHwnd(sheet);
            HWND btn = GetDlgItem(page, s.id);
            if (!btn || !IsWindowEnabled(btn)) {
                printf("button %d missing/disabled\n", s.id);
                continue;
            }
            PostMessageW(btn, BM_CLICK, 0, 0);
            HWND dlg = WaitDialog(sheet, 5000);
            if (!dlg) {
                printf("no dialog for %d\n", s.id);
                continue;
            }
            Sleep(1000);
            SaveWindow(dlg, s.name);
            PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
            Sleep(700);
            if (IsWindow(dlg) && IsWindowVisible(dlg)) {
                PostMessageW(dlg, WM_CLOSE, 0, 0);
                Sleep(700);
            }
        }
    }
    PropSheet_PressButton(sheet, PSBTN_CANCEL);
    return 0;
}

typedef LONG(WINAPI* PFN_DrvDocumentProperties)(HWND, HANDLE, LPWSTR, PDEVMODEW, PDEVMODEW, DWORD);
typedef BOOL(WINAPI* PFN_PrinterProperties)(HWND, HANDLE);

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4)
        return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Without DPI awareness window coordinates are virtualized and screen copies come out shifted.
    SetProcessDPIAware();
    g_dir = argv[2];
#ifdef _WIN64
    {
        // copy the package into <out dir>\drv, which becomes the fake driver directory
        std::wstring stage = std::wstring(argv[3]) + L"\\drv";
        CreateDirectoryW(argv[3], nullptr);
        CreateDirectoryW(stage.c_str(), nullptr);
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((g_dir + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    CopyFileW((g_dir + L"\\" + fd.cFileName).c_str(), (stage + L"\\" + fd.cFileName).c_str(), FALSE);
            while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        DeleteFileW((stage + L"\\CNLB810.BUD").c_str());
        g_dir = stage;
    }
#endif
    g_out = argv[3];
    g_doc = !(argc > 4 && !wcscmp(argv[4], L"dev"));
    g_states = argc > 4 && !wcscmp(argv[4], L"states");
    g_apply = argc > 4 && !wcscmp(argv[4], L"apply");
    g_hold = argc > 4 && !wcscmp(argv[4], L"hold");
    CreateDirectoryW(g_out.c_str(), nullptr);
    InitCommonControls();
    BuildDriverInfo();
#ifdef _WIN64
    std::wstring dll = reinterpret_cast<DRIVER_INFO_3W*>(g_di3.data())->pConfigFile;
#else
    std::wstring dll = g_dir + L"\\3\\CAP1UIK.DLL";
#endif
    HMODULE ui = LoadLibraryW(dll.c_str());
    if (!ui) {
        printf("LoadLibrary failed %lu\n", GetLastError());
        return 1;
    }
    PatchImport(ui, "WINSPOOL.DRV", "GetPrinterDriverW", reinterpret_cast<void*>(HookGetPrinterDriverW),
                reinterpret_cast<void**>(&g_realGetPrinterDriverW));
    PatchImport(ui, "WINSPOOL.DRV", "GetPrinterDriverDirectoryW",
                reinterpret_cast<void*>(HookGetPrinterDriverDirectoryW),
                reinterpret_cast<void**>(&g_realGetPrinterDriverDirectoryW));
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterW(argv[1], &hPrinter, nullptr)) {
        printf("OpenPrinter failed %lu\n", GetLastError());
        return 1;
    }
    CreateThread(nullptr, 0, Shooter, nullptr, 0, nullptr);
#ifdef _WIN64
    if (g_doc) {
        // what winspool's DocumentPropertiesW does with a v3 driver
        auto sheets = reinterpret_cast<LONG(APIENTRY*)(PPROPSHEETUI_INFO, LPARAM)>(
            GetProcAddress(ui, "DrvDocumentPropertySheets"));
        DOCUMENTPROPERTYHEADER dph = { sizeof(dph) };
        dph.hPrinter = hPrinter;
        dph.pszPrinterName = argv[1];
        LONG sz = sheets(nullptr, reinterpret_cast<LPARAM>(&dph));
        printf("size query = %ld, cbOut %lu, error %lu\n", sz, dph.cbOut, GetLastError());
        std::vector<BYTE> dm(dph.cbOut ? dph.cbOut : 4096);
        dph.pdmOut = reinterpret_cast<PDEVMODEW>(dm.data());
        dph.cbOut = DWORD(dm.size());
        dph.fMode = DM_IN_PROMPT | DM_OUT_BUFFER;
        LONG result = 0;
        LONG r = CommonPropertySheetUIW(nullptr, sheets, reinterpret_cast<LPARAM>(&dph), reinterpret_cast<LPDWORD>(&result));
        printf("CommonPropertySheetUIW = %ld, result %ld, error %lu\n", r, result, GetLastError());
        if (g_apply) {
            auto* o = dph.pdmOut;
            printf("out: paper %d orient %d copies %d collate %d scale %d res %d driverExtra %u\n", o->dmPaperSize,
                   o->dmOrientation, o->dmCopies, o->dmCollate, o->dmScale, o->dmPrintQuality, o->dmDriverExtra);
            // the OEM part of our plug-in pair: OEM_DMEXTRAHEADER {size, 'L810', version} + lbpdm::Extra
            const BYTE* priv = reinterpret_cast<const BYTE*>(o) + o->dmSize;
            for (DWORD i = 0; i + 12 <= o->dmDriverExtra; i += 4) {
                const DWORD* h = reinterpret_cast<const DWORD*>(priv + i);
                if (h[1] == 'L810') {
                    const WORD* w = reinterpret_cast<const WORD*>(h + 3);
                    const SHORT* sw = reinterpret_cast<const SHORT*>(h + 3);
                    printf("lbpdm at +%lu size %lu ver %08lX: output %u nup %u order %u %u %u %u manual %u scale %u "
                           "mismatch %u emf %u halftone %u brightness %d contrast %d\n",
                           i, h[0], h[2], w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10],
                           sw[11], sw[12]);
                    break;
                }
            }
        }
    } else {
        auto sheets = reinterpret_cast<LONG(APIENTRY*)(PPROPSHEETUI_INFO, LPARAM)>(
            GetProcAddress(ui, "DrvDevicePropertySheets"));
        DEVICEPROPERTYHEADER dph = { sizeof(dph) };
        dph.hPrinter = hPrinter;
        dph.pszPrinterName = argv[1];
        LONG result = 0;
        LONG r = CommonPropertySheetUIW(nullptr, sheets, reinterpret_cast<LPARAM>(&dph), reinterpret_cast<LPDWORD>(&result));
        printf("CommonPropertySheetUIW = %ld, result %ld\n", r, result);
    }
#else
    if (g_doc) {
        auto fn = reinterpret_cast<PFN_DrvDocumentProperties>(GetProcAddress(ui, "DrvDocumentProperties"));
        LONG size = fn(nullptr, hPrinter, argv[1], nullptr, nullptr, 0);
        printf("devmode size %ld\n", size);
        std::vector<BYTE> dm(size > 0 ? size : 4096);
        LONG r = fn(nullptr, hPrinter, argv[1], reinterpret_cast<PDEVMODEW>(dm.data()), nullptr,
                    DM_IN_PROMPT | DM_OUT_BUFFER);
        printf("DrvDocumentProperties = %ld\n", r);
    } else {
        auto fn = reinterpret_cast<PFN_PrinterProperties>(GetProcAddress(ui, "PrinterProperties"));
        BOOL r = fn(nullptr, hPrinter);
        printf("PrinterProperties = %d (err %lu)\n", r, GetLastError());
    }
#endif
    ClosePrinter(hPrinter);
    return 0;
}
