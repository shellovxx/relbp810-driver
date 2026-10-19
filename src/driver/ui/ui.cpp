// IPrintOemUI2 plug-in object, DLL entry points and loading/saving of the settings.
#include <initguid.h>
#include "ui.h"
#include <cstdarg>
#include <cstdio>
#include <map>

#include "../../common/debuglog.h"

namespace ui {

HMODULE g_module = nullptr;
static LONG g_locks = 0;
static LONG g_objects = 0;

const PaperDef kPapers[] = {
    { "LETTER", DMPAPER_LETTER, 78, 2159, 2794 },
    { "LEGAL", DMPAPER_LEGAL, 79, 2159, 3556 },
    { "A4", DMPAPER_A4, 80, 2100, 2970 },
    { "B5", DMPAPER_B5, 81, 1820, 2570 },
    { "EXECUTIVE", DMPAPER_EXECUTIVE, 86, 1842, 2667 },
    { "ENV_DL", DMPAPER_ENV_DL, 82, 1100, 2200 },
    { "ENV_10", DMPAPER_ENV_10, 83, 1048, 2413 },
    { "ENV_C5", DMPAPER_ENV_C5, 84, 1620, 2290 },
    { "ENV_MONARCH", DMPAPER_ENV_MONARCH, 85, 984, 1905 },
    { "INDEX3X5", 0, 94, 762, 1270 },
};

static const char* const kDensityKeys[] = { "LIGHT2", "LIGHT1", "STD", "DARK1", "DARK2" };

// dword_6A91A068: automatic scaling from page size (row) to output size (column), percent.
static const unsigned char kAutoScale[kPaperCount][kPaperCount] = {
    { 100, 100, 97, 82, 84, 48, 46, 73, 43, 31 },
    { 77, 100, 83, 71, 74, 48, 46, 63, 43, 31 },
    { 93, 102, 100, 85, 86, 50, 47, 75, 44, 32 },
    { 109, 120, 116, 100, 101, 58, 55, 88, 52, 38 },
    { 104, 118, 111, 96, 100, 57, 54, 85, 51, 37 },
    { 128, 164, 136, 117, 122, 100, 94, 104, 85, 55 },
    { 116, 149, 124, 106, 110, 90, 100, 94, 78, 50 },
    { 123, 135, 131, 112, 114, 66, 62, 100, 58, 42 },
    { 149, 191, 159, 136, 142, 112, 106, 121, 100, 64 },
    { 200, 200, 200, 200, 200, 154, 145, 187, 137, 100 },
};

int DefaultPaper()
{
    // sub_6A9181CB: any LOCALE_IMEASURE other than 1 (US) or 2 counts as metric.
    wchar_t buf[8] = {};
    if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IMEASURE, buf, 8))
        return 0;
    int v = _wtoi(buf);
    return v != 1 && v != 2 ? 2 : 0;
}

int NupCount(int layout)
{
    static const int n[] = { 1, 2, 4, 8, 9 };
    return layout >= 0 && layout < 5 ? n[layout] : 1;
}

int AutoScale(const Settings& s)
{
    if (!s.output || s.paper < 0 || s.paper >= kPaperCount)
        return 100;
    return kAutoScale[s.paper][s.output - 1];
}

void Normalize(Settings& s)
{
    if (s.layout)
        s.output = 0;
    if (!s.manualScale)
        s.scale = AutoScale(s);
}

int PreviewScale(const Settings& s)
{
    if (!s.manualScale)
        return 100;
    int a = AutoScale(s);
    return s.scale == a ? 100 : 100 * s.scale / a;
}

int StoredAdjust(int value)
{
    return value / 2 + 50;
}

void ResetPageSetup(Settings& s)
{
    s.paper = DefaultPaper();
    s.landscape = false;
    s.copies = 1;
    s.scale = 100;
    s.output = 0;
    s.manualScale = false;
    s.layout = 0;
    for (int& o : s.order)
        o = 0;
}

void ResetFinishing(Settings& s)
{
    s.collate = false;
}

void ResetQuality(Settings& s)
{
    s.res300 = false;
    s.halftone = lbpdm::HT_PATTERN1;
    s.brightness = 0;
    s.contrast = 0;
}

std::wstring LoadStr(UINT id)
{
    const wchar_t* p = nullptr;
    int n = LoadStringW(g_module, id, reinterpret_cast<LPWSTR>(&p), 0);
    return n > 0 ? std::wstring(p, size_t(n)) : std::wstring();
}

void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbg::Log("%s", buf);
}

// ---- feature/option helpers (IPrintCoreHelper) ----------------------------------------------
// Windows 10's Unidrv UI no longer offers IPrintCoreUI2; IPrintCoreHelper works on the devmode
// of the property sheet session when no devmode is passed.

static std::map<std::string, std::string> GetOptions(Sheet* sh)
{
    std::map<std::string, std::string> m;
    if (!sh->core)
        return m;
    PCSTR* feats = nullptr;
    DWORD n = 0;
    if (FAILED(sh->core->EnumFeatures(&feats, &n)) || !feats)
        return m;
    for (DWORD i = 0; i < n; i++) {
        PCSTR opt = nullptr;
        if (SUCCEEDED(sh->core->GetOption(nullptr, 0, feats[i], &opt)) && opt)
            m[feats[i]] = opt;
    }
    return m;
}

static void SetOptions(Sheet* sh, const std::vector<std::pair<std::string, std::string>>& opts)
{
    if (!sh->core)
        return;
    std::vector<PRINT_FEATURE_OPTION> fo;
    for (auto& o : opts)
        fo.push_back({ o.first.c_str(), o.second.c_str() });
    DWORD written = 0, result = 0;
    HRESULT hr = sh->core->SetOptions(nullptr, 0, TRUE, fo.data(), DWORD(fo.size()), &written, &result);
    Log("SetOptions: hr %08lX written %lu result %lu", hr, written, result);
}

static void DumpFeatures(Sheet* sh)
{
    if (!sh->core)
        return;
    PCSTR* feats = nullptr;
    DWORD n = 0;
    if (FAILED(sh->core->EnumFeatures(&feats, &n)) || !feats)
        return;
    for (DWORD i = 0; i < n; i++) {
        PCSTR* list = nullptr;
        DWORD k = 0;
        std::string t;
        if (SUCCEEDED(sh->core->EnumOptions(feats[i], &list, &k)) && list)
            for (DWORD j = 0; j < k; j++)
                t += std::string(list[j]) + " ";
        Log("feature %s: %s", feats[i], t.c_str());
    }
}

void Sheet::Load()
{
    DEVMODEW* dm = param->pPublicDM;
    auto* ex = static_cast<lbpdm::Extra*>(param->pOEMDM);
    auto opts = GetOptions(this);
    for (auto& fo : opts)
        Log("option %s=%s", fo.first.c_str(), fo.second.c_str());

    Settings s;
    s.paper = -1;
    auto paper = opts.find("PaperSize");
    for (int i = 0; i < kPaperCount; i++) {
        if (paper != opts.end() ? paper->second == kPapers[i].keyword
                                : (dm && kPapers[i].dmPaper && dm->dmPaperSize == kPapers[i].dmPaper))
            s.paper = i;
    }
    if (s.paper < 0)
        s.paper = DefaultPaper();   // sub_6A90571B: unknown sizes fall back to the default
    auto orient = opts.find("Orientation");
    s.landscape = orient != opts.end() ? orient->second != "PORTRAIT" : (dm && dm->dmOrientation == DMORIENT_LANDSCAPE);
    s.res300 = opts["Resolution"] == "Option2";
    for (int i = 0; i < 5; i++)
        if (opts["TonerDensity"] == kDensityKeys[i])
            s.density = i;
    s.tonerSave = opts["TonerSave"] == "ON";
    s.refinement = opts["ImageRefinement"] != "OFF";
    if (dm) {
        s.copies = (dm->dmFields & DM_COPIES) && dm->dmCopies > 0 ? dm->dmCopies : 1;
        s.collate = (dm->dmFields & DM_COLLATE) ? dm->dmCollate == DMCOLLATE_TRUE : true;
        if (s.copies > 999)
            s.copies = 999;
    }
    if (ex && ex->dwSignature == lbpdm::kSignature) {
        lbpdm::Extra e = *ex;
        lbpdm::Validate(e);
        s.output = 0;
        for (int i = 0; i < kPaperCount; i++)
            if (e.outputPaper && kPapers[i].dmPaper == short(e.outputPaper))
                s.output = i + 1;
        s.layout = 0;
        for (int i = 0; i < 5; i++)
            if (NupCount(i) == e.nup)
                s.layout = i;
        for (int i = 0; i < 4; i++)
            s.order[i] = e.nupOrder[i];
        s.manualScale = e.manualScale != 0;
        s.scale = e.scale;
        s.ignoreMismatch = e.ignoreMismatch != 0;
        s.emfSpool = e.emfSpool != 0;
        s.halftone = e.halftone;
        s.brightness = 2 * (e.brightness / 2);
        s.contrast = 2 * (e.contrast / 2);
        s.overlayFile = e.overlayFile;
        s.overlayFront = e.overlayFront != 0;
        s.overlayInch = e.overlayInch != 0;
        s.overlayLeft = e.overlayLeft;
        s.overlayTop = e.overlayTop;
        s.overlayWidth = e.overlayWidth;
        s.overlayHeight = e.overlayHeight;
    }
    Normalize(s);
    cur = s;
    profiles.Load(printer);
}

void Sheet::Save()
{
    const Settings& s = cur;
    DEVMODEW* dm = param->pPublicDM;
    const PaperDef& p = kPapers[s.paper];
    SetOptions(this, {
                         { "PaperSize", p.keyword },
                         { "Orientation", s.landscape ? "LANDSCAPE_CC90" : "PORTRAIT" },
                         { "Resolution", s.res300 ? "Option2" : "Option1" },
                         { "TonerDensity", kDensityKeys[s.density] },
                         { "TonerSave", s.tonerSave ? "ON" : "OFF" },
                         { "ImageRefinement", s.refinement ? "ON" : "OFF" },
                     });
    if (dm) {
        dm->dmFields |= DM_COPIES | DM_COLLATE | DM_SCALE;
        dm->dmCopies = short(s.copies);
        dm->dmCollate = s.collate ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
        // Output size and N-up are applied later in the pipeline; Unidrv scales only for a
        // manual scale of the page size itself.
        dm->dmScale = short(!s.layout && !s.output && s.manualScale ? s.scale : 100);
    }
    auto* ex = static_cast<lbpdm::Extra*>(param->pOEMDM);
    if (ex) {
        lbpdm::Extra e;
        lbpdm::SetDefaults(e);
        e.outputPaper = WORD(s.output ? kPapers[s.output - 1].dmPaper : 0);
        e.nup = WORD(NupCount(s.layout));
        for (int i = 0; i < 4; i++)
            e.nupOrder[i] = WORD(s.order[i]);
        e.manualScale = s.manualScale;
        e.scale = WORD(s.scale);
        e.ignoreMismatch = s.ignoreMismatch;
        e.emfSpool = s.emfSpool;
        e.halftone = WORD(s.halftone);
        e.brightness = SHORT(s.brightness);
        e.contrast = SHORT(s.contrast);
        e.overlayFront = s.overlayFront;
        e.overlayInch = s.overlayInch;
        e.overlayLeft = s.overlayLeft;
        e.overlayTop = s.overlayTop;
        e.overlayWidth = s.overlayWidth;
        e.overlayHeight = s.overlayHeight;
        wcsncpy_s(e.overlayFile, s.overlayFile.c_str(), _TRUNCATE);
        if (profiles.selected >= 0 && profiles.selected < int(profiles.items.size()))
            wcsncpy_s(e.profile, profiles.items[size_t(profiles.selected)].name.c_str(), _TRUNCATE);
        lbpdm::Validate(e);
        *ex = e;
    }
    profiles.SaveSelection();
    saved = true;
    Log("saved: paper %s %s copies %d collate %d nup %d scale %d%s res %d ht %d br %d ct %d dens %d",
        p.keyword, s.landscape ? "landscape" : "portrait", s.copies, s.collate, NupCount(s.layout), s.scale,
        s.manualScale ? " manual" : "", s.res300 ? 300 : 600, s.halftone, s.brightness, s.contrast, s.density);
}

void Sheet::Changed(HWND page, bool profileModified)
{
    if (updating)
        return;
    if (page)
        PropSheet_Changed(GetParent(page), page);
    // sub_6A90B595: a change marks the selected configuration as modified.
    if (profileModified && profiles.selected >= 0 && !profiles.modified) {
        profiles.modified = true;
        for (HWND h : hwndPage)
            if (h)
                FillProfileCombo(h, this);
    }
}

// ---- the plug-in object ----------------------------------------------------------------------

static LONG APIENTRY DocPropSheets(PPROPSHEETUI_INFO info, LPARAM lParam, IPrintCoreHelper* core)
{
    if (!info)
        return -1;
    switch (info->Reason) {
    case PROPSHEETUI_REASON_INIT: {
        auto* sh = new (std::nothrow) Sheet;
        if (!sh)
            return -1;
        sh->param = reinterpret_cast<OEMUIPSPARAM*>(info->lParamInit);
        sh->core = core;
        sh->pfnComPropSheet = info->pfnComPropSheet;
        sh->hComPropSheet = info->hComPropSheet;
        if (sh->param && sh->param->pPrinterName)
            sh->printer = sh->param->pPrinterName;
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            DumpFeatures(sh);
        }
        sh->Load();
        struct {
            UINT id;
            DLGPROC proc;
        } const pages[] = { { IDD_PAGESETUP, PageSetupProc }, { IDD_FINISHING, FinishingProc }, { IDD_QUALITY, QualityProc } };
        for (auto& pg : pages) {
            PROPSHEETPAGEW psp = { sizeof(psp) };
            psp.dwFlags = PSP_DEFAULT;
            psp.hInstance = g_module;
            psp.pszTemplate = MAKEINTRESOURCEW(pg.id);
            psp.pfnDlgProc = pg.proc;
            psp.lParam = reinterpret_cast<LPARAM>(sh);
            LONG_PTR h = info->pfnComPropSheet(info->hComPropSheet, CPSFUNC_ADD_PROPSHEETPAGEW,
                                               reinterpret_cast<LPARAM>(&psp), 0);
            if (h <= 0) {
                Log("CPSFUNC_ADD_PROPSHEETPAGEW failed (%Id)", h);
                break;
            }
            sh->pages.push_back(reinterpret_cast<HANDLE>(h));
        }
        info->UserData = reinterpret_cast<ULONG_PTR>(sh);
        info->Result = CPSUI_CANCEL;
        return 1;
    }
    case PROPSHEETUI_REASON_GET_INFO_HEADER:
        return 1;
    case PROPSHEETUI_REASON_SET_RESULT:
        if (lParam)
            info->Result = reinterpret_cast<PSETRESULT_INFO>(lParam)->Result;
        return 1;
    case PROPSHEETUI_REASON_DESTROY:
        delete reinterpret_cast<Sheet*>(info->UserData);
        info->UserData = 0;
        return 1;
    }
    return -1;
}

class UiPlugin : public IPrintOemUI2 {
public:
    UiPlugin() { InterlockedIncrement(&g_objects); }
    virtual ~UiPlugin()
    {
        if (core_)
            core_->Release();
        InterlockedDecrement(&g_objects);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IPrintOemUI || riid == IID_IPrintOemUI2) {
            *ppv = static_cast<IPrintOemUI2*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG r = InterlockedDecrement(&refs_);
        if (!r)
            delete this;
        return r;
    }

    STDMETHODIMP GetInfo(DWORD mode, PVOID buf, DWORD cb, PDWORD needed) override
    {
        if (needed)
            *needed = sizeof(DWORD);
        if (!buf || cb < sizeof(DWORD))
            return E_FAIL;
        switch (mode) {
        case OEMGI_GETSIGNATURE: *static_cast<DWORD*>(buf) = lbpdm::kSignature; return S_OK;
        case OEMGI_GETVERSION: *static_cast<DWORD*>(buf) = lbpdm::kVersion; return S_OK;
        case OEMGI_GETREQUESTEDHELPERINTERFACES: *static_cast<DWORD*>(buf) = OEMPUBLISH_IPRINTCOREHELPER; return S_OK;
        default: return E_FAIL;
        }
    }
    STDMETHODIMP DevMode(DWORD mode, POEMDMPARAM p) override { return lbpdm::HandleDevMode(mode, p); }
    STDMETHODIMP PublishDriverInterface(IUnknown* unk) override
    {
        // First call: IPrintOemDriverUI (not needed). Second call, as requested in GetInfo:
        // IPrintCoreHelperUni, which gives access to the GPD features.
        if (!core_ && unk) {
            void* p = nullptr;
            if (SUCCEEDED(unk->QueryInterface(IID_IPrintCoreHelper, &p)))
                core_ = static_cast<IPrintCoreHelper*>(p);
        }
        Log("PublishDriverInterface: core %p", core_);
        return S_OK;
    }
    STDMETHODIMP CommonUIProp(DWORD, POEMCUIPPARAM) override { return E_NOTIMPL; }
    STDMETHODIMP DocumentPropertySheets(PPROPSHEETUI_INFO info, LPARAM lParam) override
    {
        LONG r = DocPropSheets(info, lParam, core_);
        return r > 0 ? S_OK : E_FAIL;
    }
    STDMETHODIMP DevicePropertySheets(PPROPSHEETUI_INFO, LPARAM) override { return E_NOTIMPL; }
    STDMETHODIMP DevQueryPrintEx(POEMUIOBJ, PDEVQUERYPRINT_INFO, PDEVMODE, PVOID) override { return E_NOTIMPL; }
    STDMETHODIMP DeviceCapabilities(POEMUIOBJ, HANDLE, PWSTR, WORD, PVOID, PDEVMODE, PVOID, DWORD, DWORD*) override
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP UpgradePrinter(DWORD, PBYTE) override { return E_NOTIMPL; }
    STDMETHODIMP PrinterEvent(PWSTR, INT, DWORD, LPARAM) override { return E_NOTIMPL; }
    STDMETHODIMP DriverEvent(DWORD, DWORD, LPBYTE, LPARAM) override { return E_NOTIMPL; }
    STDMETHODIMP QueryColorProfile(HANDLE, POEMUIOBJ, PDEVMODE, PVOID, ULONG, VOID*, ULONG*, FLONG*) override
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP FontInstallerDlgProc(HWND, UINT, WPARAM, LPARAM) override { return E_NOTIMPL; }
    STDMETHODIMP UpdateExternalFonts(HANDLE, HANDLE, PWSTR) override { return E_NOTIMPL; }
    STDMETHODIMP QueryJobAttributes(HANDLE, PDEVMODE, DWORD, LPBYTE) override { return E_NOTIMPL; }
    STDMETHODIMP HideStandardUI(DWORD mode) override
    {
        // Replace Unidrv's document pages; keep its device settings page.
        return mode == OEMCUIP_DOCPROP ? S_OK : E_NOTIMPL;
    }
    STDMETHODIMP DocumentEvent(HANDLE, HDC, INT, ULONG, PVOID, ULONG, PVOID, PINT) override { return E_NOTIMPL; }

private:
    LONG refs_ = 1;
    IPrintCoreHelper* core_ = nullptr;
};

class ClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG r = InterlockedDecrement(&refs_);
        if (!r)
            delete this;
        return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto* p = new (std::nothrow) UiPlugin;
        if (!p)
            return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            InterlockedIncrement(&g_locks);
        else
            InterlockedDecrement(&g_locks);
        return S_OK;
    }

private:
    LONG refs_ = 1;
};

} // namespace ui

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        ui::g_module = inst;
        DisableThreadLibraryCalls(inst);
        dbg::Init(L"ui");
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icc);
        ui::RegisterControls(inst);
    } else if (reason == DLL_PROCESS_DETACH) {
        ui::UnregisterControls(inst);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (ui::g_objects == 0 && ui::g_locks == 0) ? S_OK : S_FALSE;
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID clsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
    if (clsid != CLSID_OEMUI)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new (std::nothrow) ui::ClassFactory;
    if (!cf)
        return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}
