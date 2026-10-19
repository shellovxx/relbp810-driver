// Unidrv rendering plug-in for the Canon LBP-810.
//
// Unidrv rasterizes the page; ImageProcessing() hands us every band. We collect the page as a
// 1bpp bitmap and write Canon's CAPT spool stream (job header, page parameters, SCoA data)
// straight to the spooler. Everything Unidrv itself would send is swallowed by the
// IPrintOemUni2::WritePrinter hook, so the spool file contains only our packets.
// Unidrv rendering plug-ins are built as "kernel mode" modules that run in user mode.
#define KERNEL_MODE
#define USERMODE_DRIVER
#include <windows.h>
#include <objbase.h>
#include <winddi.h>
#include <winspool.h>
#include <printoem.h>
#include <initguid.h>
#include <prcomoem.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../common/capt.h"
#include "../../common/spool.h"
#include "../../common/paper.h"
#include "../../common/debuglog.h"
#include "../../common/lbpdm.h"

namespace {

// The private DEVMODE part is shared with the UI plug-in CNLB810U.DLL (see lbpdm.h).
constexpr DWORD kSignature = lbpdm::kSignature;
constexpr DWORD kVersion = lbpdm::kVersion;

// Callback IDs used in CNLB810.GPD.
enum : DWORD {
    CB_START_DOC = 1,
    CB_START_PAGE = 2,
    CB_END_PAGE = 3,
    CB_END_JOB = 4,
    CB_COPIES = 5,
};

LONG g_locks = 0;
LONG g_objects = 0;
HMODULE g_module = nullptr;

struct OemPdev {
    IPrintOemDriverUni* helper = nullptr;
    // Geometry from GDIINFO (device pixels at the current resolution)
    int dpi = 600;
    int printW = 0, printH = 0;
    int physW = 0, physH = 0;
    int offX = 0, offY = 0;
    // Page buffer (1bpp, 1 = black)
    std::vector<uint8_t> page;
    size_t stride = 0;
    bool pageHasInk = false;
    int pageNo = 0;
    bool jobHeaderWritten = false;
    bool firstBandLogged = false;
    int bands = 0;
    DWORD copies = 1;
};

// Unidrv/GDI swallow exceptions thrown by plug-ins and silently abort the job; log them instead.
int LogException(const char* what, EXCEPTION_POINTERS* ep)
{
    dbg::Log("EXCEPTION %08lX at %p in %s", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
             what);
    return EXCEPTION_EXECUTE_HANDLER;
}

template <class F> bool Guard(const char* what, F&& f)
{
    __try {
        f();
        return true;
    } __except (LogException(what, GetExceptionInformation())) {
        return false;
    }
}

std::string GetSetting(IPrintOemDriverUni* helper, PDEVOBJ pdevobj, const char* feature)
{
    if (!helper)
        return {};
    char buf[128] = {};
    DWORD need = 0, n = 0;
    if (FAILED(helper->DrvGetDriverSetting(pdevobj, feature, buf, sizeof(buf) - 1, &need, &n)))
        return {};
    return buf;
}

bool WriteSpool(PDEVOBJ pdevobj, const std::vector<uint8_t>& data)
{
    const uint8_t* p = data.data();
    size_t left = data.size();
    while (left) {
        DWORD chunk = left > 0x10000 ? 0x10000 : DWORD(left);
        DWORD written = 0;
        if (!::WritePrinter(pdevobj->hPrinter, const_cast<uint8_t*>(p), chunk, &written) || !written) {
            dbg::Log("WritePrinter failed, error %lu", GetLastError());
            return false;
        }
        p += written;
        left -= written;
    }
    return true;
}

void WriteJobHeader(PDEVOBJ pdevobj, OemPdev* pdev)
{
    std::vector<uint8_t> out;
    spool::JobHeader jh;
    jh.copies = pdev->copies ? pdev->copies : 1;
    jh.collate = 0;
    jh.pageOrder = 1;
    jh.flags = spool::JOBF_PAGE_FLAG;
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&jh), reinterpret_cast<uint8_t*>(&jh) + sizeof(jh));

    spool::DocInfo di;
    const DEVMODEW* dm = pdevobj->pPublicDM;
    di.paperId = dm ? dm->dmPaperSize : DMPAPER_A4;
    if (dm)
        wcsncpy_s(di.formName, dm->dmFormName, _TRUNCATE);
    di.copies = jh.copies;
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&di), reinterpret_cast<uint8_t*>(&di) + sizeof(di));
    WriteSpool(pdevobj, out);
    pdev->jobHeaderWritten = true;
    dbg::Log("job header written: copies %lu paper %d", jh.copies, di.paperId);
}

void StartPage(OemPdev* pdev)
{
    pdev->stride = ((size_t(pdev->printW) + 31) / 32) * 4;
    pdev->page.assign(pdev->stride * size_t(pdev->printH), 0);
    pdev->pageHasInk = false;
}

void EndPage(PDEVOBJ pdevobj, OemPdev* pdev)
{
    if (pdev->page.empty())
        StartPage(pdev);
    pdev->pageNo++;

    const std::string paperKey = GetSetting(pdev->helper, pdevobj, "PaperSize");
    const std::string bin = GetSetting(pdev->helper, pdevobj, "InputBin");
    const std::string density = GetSetting(pdev->helper, pdevobj, "TonerDensity");
    const std::string tonerSave = GetSetting(pdev->helper, pdevobj, "TonerSave");
    const std::string refine = GetSetting(pdev->helper, pdevobj, "ImageRefinement");

    const paper::PaperInfo* pi = paper::FindByKeyword(paperKey.c_str());
    capt::PageParams pp = {};
    pp.modelCode = capt::kModelCodeLbp810;
    pp.paperCode = pi ? pi->code : capt::kPaperCodeCustom;
    pp.flags = 0x01;   // job header flag JOBF_PAGE_FLAG (CAP1EMN sub_10005350)
    pp.source = (bin == "MANUAL") ? 1 : 0;
    pp.density = paper::DensityFromKeyword(density.c_str());
    pp.density2[0] = pp.density2[1] = pp.density2[2] = 0x1F;
    pp.mediaType = 0;
    pp.resolution = pdev->dpi == 600 ? 0x11 : 0x00;
    pp.const14[0] = 3;
    pp.const14[1] = 1;
    pp.const14[2] = 1;
    pp.const14[3] = 1;
    pp.refinement = (refine == "OFF") ? 0 : 2;
    pp.tonerSave = (tonerSave == "ON") ? 1 : 0;
    pp.marginLeft = uint16_t(pdev->offX);
    pp.marginTop = uint16_t(pdev->offY);
    pp.lineBytes = uint16_t(((pdev->printW - 1 + 31) >> 3) & ~3);
    pp.lines = uint16_t(pdev->printH + 2);
    if (pi) {
        pp.paperWidth = uint16_t(paper::ToDots(pi->width, pi->units, pdev->dpi));
        pp.paperHeight = uint16_t(paper::ToDots(pi->height, pi->units, pdev->dpi));
    } else {
        pp.paperWidth = uint16_t(pdev->physW);
        pp.paperHeight = uint16_t(pdev->physH);
    }

    std::vector<uint8_t> out;
    out.reserve(256 * 1024);
    spool::PutPage(out, pp, pdev->page.data(), pdev->stride, size_t(pdev->printH));
    dbg::Log("page %d: %dx%d @%d paper=%s(0x%02X) %ux%u off %d,%d bin=%s dens=%u ts=%u ref=%u -> %zu bytes",
             pdev->pageNo, pdev->printW, pdev->printH, pdev->dpi, paperKey.c_str(), pp.paperCode,
             pp.paperWidth, pp.paperHeight, pdev->offX, pdev->offY, bin.c_str(), pp.density, pp.tonerSave,
             pp.refinement, out.size());
    dbg::DumpPage(pdev->pageNo, pdev->page.data(), pdev->stride, pdev->printW, pdev->printH);
    WriteSpool(pdevobj, out);
    pdev->page.clear();
}

// Copies one band into the page buffer.
void PutBand(OemPdev* pdev, const BYTE* src, const BITMAPINFOHEADER* bih, const BYTE* colorTable,
             const IPPARAMS* ip)
{
    if (pdev->page.empty())
        StartPage(pdev);
    const int w = bih->biWidth;
    const int h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
    // Unidrv hands bands over top-down even though biHeight is positive
    // (pSetupOEMImageProcessing passes pvBits + stride * firstLine).
    const int bpp = bih->biBitCount;
    const size_t srcStride = ((size_t(w) * bpp + 31) / 32) * 4;

    if (!pdev->firstBandLogged) {
        pdev->firstBandLogged = true;
        const RGBQUAD* ct = reinterpret_cast<const RGBQUAD*>(colorTable);
        dbg::Log("band: %dx%d bpp %d biHeight %ld offset %ld,%ld blank %d banding %d ct0=%02X%02X%02X ct1=%02X%02X%02X",
                 w, h, bpp, bih->biHeight, ip->ptOffset.x, ip->ptOffset.y, ip->bBlankBand, ip->bBanding,
                 ct ? ct[0].rgbRed : 0, ct ? ct[0].rgbGreen : 0, ct ? ct[0].rgbBlue : 0,
                 ct ? ct[1].rgbRed : 0, ct ? ct[1].rgbGreen : 0, ct ? ct[1].rgbBlue : 0);
    }
    if (ip->bBlankBand)
        return;

    for (int y = 0; y < h; y++) {
        const int dy = ip->ptOffset.y + y;
        if (dy < 0 || dy >= pdev->printH)
            continue;
        const BYTE* s = src + srcStride * size_t(y);
        uint8_t* d = &pdev->page[pdev->stride * size_t(dy)];
        if (bpp == 1) {
            // Color table decides which index is black.
            bool invert = true;
            if (colorTable) {
                const RGBQUAD* ct = reinterpret_cast<const RGBQUAD*>(colorTable);
                int l0 = ct[0].rgbRed + ct[0].rgbGreen + ct[0].rgbBlue;
                int l1 = ct[1].rgbRed + ct[1].rgbGreen + ct[1].rgbBlue;
                invert = l0 < l1;   // index 0 darker -> bit 0 means black
            }
            if ((ip->ptOffset.x & 7) == 0 && ip->ptOffset.x >= 0) {
                // Fast path: byte aligned copy.
                const int dx0 = ip->ptOffset.x >> 3;
                int n = (w + 7) >> 3;
                if (dx0 + n > int(pdev->stride))
                    n = int(pdev->stride) - dx0;
                const uint8_t mask = invert ? 0xFF : 0x00;
                uint8_t any = 0;
                for (int i = 0; i < n; i++) {
                    uint8_t v = uint8_t(s[i] ^ mask);
                    if (i == n - 1 && (w & 7))
                        v &= uint8_t(0xFF << (8 - (w & 7)));
                    d[dx0 + i] |= v;
                    any |= v;
                }
                if (any)
                    pdev->pageHasInk = true;
                continue;
            }
            for (int x = 0; x < w; x++) {
                int dx = ip->ptOffset.x + x;
                if (dx < 0 || dx >= pdev->printW)
                    continue;
                bool bit = (s[x >> 3] >> (7 - (x & 7))) & 1;
                if (bit != invert) {
                    d[dx >> 3] |= uint8_t(0x80 >> (dx & 7));
                    pdev->pageHasInk = true;
                }
            }
        } else if (bpp == 8) {
            for (int x = 0; x < w; x++) {
                int dx = ip->ptOffset.x + x;
                if (dx < 0 || dx >= pdev->printW)
                    continue;
                const RGBQUAD* ct = reinterpret_cast<const RGBQUAD*>(colorTable);
                int v = ct ? (ct[s[x]].rgbRed + ct[s[x]].rgbGreen + ct[s[x]].rgbBlue) / 3 : s[x];
                if (v < 128) {
                    d[dx >> 3] |= uint8_t(0x80 >> (dx & 7));
                    pdev->pageHasInk = true;
                }
            }
        }
    }
}

class RenderPlugin : public IPrintOemUni2 {
public:
    RenderPlugin() { InterlockedIncrement(&g_objects); }
    virtual ~RenderPlugin()
    {
        if (helper_)
            helper_->Release();
        InterlockedDecrement(&g_objects);
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IPrintOemUni || riid == IID_IPrintOemUni2) {
            *ppv = static_cast<IPrintOemUni2*>(this);
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

    // IPrintOemCommon
    STDMETHODIMP GetInfo(DWORD mode, PVOID buf, DWORD cb, PDWORD needed) override
    {
        if (needed)
            *needed = sizeof(DWORD);
        if (!buf || cb < sizeof(DWORD))
            return E_FAIL;
        switch (mode) {
        case OEMGI_GETSIGNATURE: *static_cast<DWORD*>(buf) = kSignature; return S_OK;
        case OEMGI_GETVERSION: *static_cast<DWORD*>(buf) = kVersion; return S_OK;
        default: return E_FAIL;
        }
    }
    STDMETHODIMP DevMode(DWORD mode, POEMDMPARAM p) override { return lbpdm::HandleDevMode(mode, p); }

    // IPrintOemEngine
    STDMETHODIMP EnableDriver(DWORD, DWORD, PDRVENABLEDATA pded) override
    {
        if (pded) {
            pded->iDriverVersion = PRINTER_OEMINTF_VERSION;
            pded->c = 0;
            pded->pdrvfn = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP DisableDriver() override { return S_OK; }
    STDMETHODIMP EnablePDEV(PDEVOBJ pdevobj, PWSTR printerName, ULONG, HSURF*, ULONG cjGdiInfo,
                            GDIINFO* gi, ULONG, DEVINFO*, DRVENABLEDATA*, PDEVOEM* pDevOem) override
    {
        auto* pdev = new (std::nothrow) OemPdev;
        if (!pdev)
            return E_OUTOFMEMORY;
        pdev->helper = helper_;
        if (helper_)
            helper_->AddRef();
        if (gi && cjGdiInfo >= sizeof(GDIINFO)) {
            pdev->dpi = int(gi->ulLogPixelsX);
            pdev->printW = int(gi->ulHorzRes);
            pdev->printH = int(gi->ulVertRes);
            pdev->physW = gi->szlPhysSize.cx;
            pdev->physH = gi->szlPhysSize.cy;
            pdev->offX = gi->ptlPhysOffset.x;
            pdev->offY = gi->ptlPhysOffset.y;
        }
        dbg::Log("EnablePDEV '%ls': dpi %d printable %dx%d phys %dx%d offset %d,%d", printerName ? printerName : L"",
                 pdev->dpi, pdev->printW, pdev->printH, pdev->physW, pdev->physH, pdev->offX, pdev->offY);
        *pDevOem = pdev;
        return S_OK;
    }
    STDMETHODIMP DisablePDEV(PDEVOBJ pdevobj) override
    {
        auto* pdev = static_cast<OemPdev*>(pdevobj->pdevOEM);
        dbg::Log("DisablePDEV (pages %d, bands %d, pending page %d)", pdev ? pdev->pageNo : -1, pdev ? pdev->bands : -1,
                 pdev ? int(!pdev->page.empty()) : -1);
        if (pdev) {
            if (pdev->helper)
                pdev->helper->Release();
            delete pdev;
            pdevobj->pdevOEM = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP ResetPDEV(PDEVOBJ oldObj, PDEVOBJ newObj) override
    {
        auto* o = static_cast<OemPdev*>(oldObj->pdevOEM);
        auto* n = static_cast<OemPdev*>(newObj->pdevOEM);
        if (o && n) {
            n->jobHeaderWritten = o->jobHeaderWritten;
            n->pageNo = o->pageNo;
            n->copies = o->copies;
        }
        return S_OK;
    }

    // IPrintOemUni
    STDMETHODIMP PublishDriverInterface(IUnknown* unk) override
    {
        if (!helper_ && unk) {
            void* p = nullptr;
            if (SUCCEEDED(unk->QueryInterface(IID_IPrintOemDriverUni, &p)))
                helper_ = static_cast<IPrintOemDriverUni*>(p);
        }
        return helper_ ? S_OK : E_FAIL;
    }
    STDMETHODIMP GetImplementedMethod(PSTR name) override
    {
        if (!name)
            return S_FALSE;
        if (!strcmp(name, "CommandCallback") || !strcmp(name, "ImageProcessing") ||
            !strcmp(name, "WritePrinter"))
            return S_OK;
        return S_FALSE;
    }
    STDMETHODIMP DriverDMS(PVOID, PVOID, DWORD, PDWORD) override { return E_NOTIMPL; }
    STDMETHODIMP CommandCallback(PDEVOBJ pdevobj, DWORD id, DWORD count, PDWORD params, INT* result) override
    {
        auto* pdev = static_cast<OemPdev*>(pdevobj->pdevOEM);
        if (result)
            *result = 0;
        if (!pdev)
            return E_FAIL;
        dbg::Log("CommandCallback %lu (count %lu, param %lu)", id, count, (count && params) ? params[0] : 0);
        bool ok = Guard("CommandCallback", [&]() { HandleCommand(pdevobj, pdev, id, count, params); });
        return ok ? S_OK : E_FAIL;
    }
    void HandleCommand(PDEVOBJ pdevobj, OemPdev* pdev, DWORD id, DWORD count, PDWORD params)
    {
        switch (id) {
        case CB_COPIES:
            if (count >= 1 && params)
                pdev->copies = params[0];
            break;
        case CB_START_DOC:
            // The job header is written with the first page: CmdCopies comes after CmdStartDoc.
            break;
        case CB_START_PAGE:
            if (!pdev->jobHeaderWritten)
                WriteJobHeader(pdevobj, pdev);
            StartPage(pdev);
            break;
        case CB_END_PAGE:
            EndPage(pdevobj, pdev);
            break;
        case CB_END_JOB: {
            std::vector<uint8_t> out;
            spool::Put(out, spool::PKT_JOB_END, nullptr, 0);
            WriteSpool(pdevobj, out);
            dbg::Log("end of job after %d pages", pdev->pageNo);
            break;
        }
        }
    }
    STDMETHODIMP ImageProcessing(PDEVOBJ pdevobj, PBYTE src, PBITMAPINFOHEADER bih, PBYTE colorTable,
                                 DWORD, PIPPARAMS ip, PBYTE* result) override
    {
        auto* pdev = static_cast<OemPdev*>(pdevobj->pdevOEM);
        bool ok = true;
        if (pdev && src && bih && ip) {
            pdev->bands++;
            if (pdev->bands <= 3 || !ip->bBlankBand)
                dbg::Log("IP #%d: %ldx%ld at %ld,%ld blank %d", pdev->bands, bih->biWidth, bih->biHeight,
                         ip->ptOffset.x, ip->ptOffset.y, ip->bBlankBand);
            ok = Guard("ImageProcessing", [&]() { PutBand(pdev, src, bih, colorTable, ip); });
        }
        if (result)
            *result = src;
        return ok ? S_OK : E_FAIL;
    }
    STDMETHODIMP FilterGraphics(PDEVOBJ, PBYTE, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP Compression(PDEVOBJ, PBYTE, PBYTE, DWORD, DWORD, INT*) override { return E_NOTIMPL; }
    STDMETHODIMP HalftonePattern(PDEVOBJ, PBYTE, DWORD, DWORD, DWORD, DWORD, PBYTE, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP MemoryUsage(PDEVOBJ, POEMMEMORYUSAGE) override { return E_NOTIMPL; }
    STDMETHODIMP TTYGetInfo(PDEVOBJ, DWORD, PVOID, DWORD, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP DownloadFontHeader(PDEVOBJ, PUNIFONTOBJ, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP DownloadCharGlyph(PDEVOBJ, PUNIFONTOBJ, HGLYPH, PDWORD, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP TTDownloadMethod(PDEVOBJ, PUNIFONTOBJ, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP OutputCharStr(PDEVOBJ, PUNIFONTOBJ, DWORD, DWORD, PVOID) override { return E_NOTIMPL; }
    STDMETHODIMP SendFontCmd(PDEVOBJ, PUNIFONTOBJ, PFINVOCATION) override { return E_NOTIMPL; }
    STDMETHODIMP TextOutAsBitmap(SURFOBJ*, STROBJ*, FONTOBJ*, CLIPOBJ*, RECTL*, RECTL*, BRUSHOBJ*, BRUSHOBJ*,
                                 POINTL*, MIX) override { return E_NOTIMPL; }

    // IPrintOemUni2: swallow everything Unidrv writes; our own packets go straight to WritePrinter.
    STDMETHODIMP WritePrinter(PDEVOBJ, PVOID, DWORD cb, PDWORD written) override
    {
        if (written)
            *written = cb;
        return S_OK;
    }

private:
    LONG refs_ = 1;
    IPrintOemDriverUni* helper_ = nullptr;
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
        auto* p = new (std::nothrow) RenderPlugin;
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

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
        dbg::Init(L"render");
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID clsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
    if (clsid != CLSID_OEMRENDER)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new (std::nothrow) ClassFactory;
    if (!cf)
        return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}
