// Windows Installer custom actions of the LBP-810 MSI (packaged by scripts/make-msi.py).
//
// The MSI is a 32-bit package that carries this DLL built for x86 and x64 and runs the one matching
// the OS: SetupAPI refuses device installation under WOW64. Both actions are deferred, run as
// LocalSystem and get the installed driver package folder (CNLB810.INF, cnlb810.cat, cnlb810.cer,
// amd64\, x86\) as CustomActionData. They do what scripts/install.ps1, scripts/update.ps1 and
// scripts/uninstall.ps1 do during development, without PowerShell (Windows 7 has no print cmdlets
// and its pnputil has another syntax).
//
// InstallDriver    trusts the signing certificate, stages the package and installs the printer
//                  driver, binds a connected printer to it (PnP then creates the queue) or creates
//                  the queue on its port, and replaces a language monitor DLL the spooler keeps
//                  loaded (upgrade).
// UninstallDriver  removes the queues, driver, monitor, device bindings, driver store copies and
//                  the certificate. It is also the rollback of a fresh install and never fails.
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <winspool.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <wincrypt.h>
#include <VersionHelpers.h>
#include <initguid.h>
#include <devguid.h>
#include <string>
#include <vector>
#include <cstdarg>
#include <cwchar>

namespace {

const wchar_t kDriverName[] = L"Canon LBP-810";
const wchar_t kMonitorName[] = L"LBP-810 CAPT Monitor";
const wchar_t kMonitorDll[] = L"CNLB810L.DLL";
const wchar_t kInfName[] = L"CNLB810.INF";
const wchar_t kCertFile[] = L"cnlb810.cer";
const wchar_t kPortHint[] = L"LBP-810";  // usbmon describes the port by the IEEE 1284 model: "CanonLBP-810"
const wchar_t* const kHardwareIds[] = { L"USBPRINT\\CanonLBP-810AC2D", L"LPTENUM\\CanonLBP-810AC2D" };
// LocalMachine Root makes the self-signed certificate valid, TrustedPublisher lets Windows install
// the package without asking.
const wchar_t* const kCertStores[] = { L"Root", L"TrustedPublisher" };
#ifdef _WIN64
const wchar_t kEnvironment[] = L"Windows x64";
const wchar_t kArchDir[] = L"amd64";
#else
const wchar_t kEnvironment[] = L"Windows NT x86";
const wchar_t kArchDir[] = L"x86";
#endif

// Everything goes to the MSI log; Error and Notice are also shown to the user (unless silent).
class Session {
public:
    explicit Session(MSIHANDLE h) : h_(h) {}

    void Log(const wchar_t* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        Send(INSTALLMESSAGE_INFO, L"LBP810: [1]", Format(fmt, ap));
        va_end(ap);
    }

    void Error(const wchar_t* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        Send(INSTALLMESSAGE(INSTALLMESSAGE_ERROR | MB_OK | MB_ICONERROR), L"[1]", Format(fmt, ap));
        va_end(ap);
    }

    void Notice(const wchar_t* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        Send(INSTALLMESSAGE(INSTALLMESSAGE_USER | MB_OK | MB_ICONINFORMATION), L"[1]", Format(fmt, ap));
        va_end(ap);
    }

    // CustomActionData without the trailing backslash of a [DIRECTORY] value.
    std::wstring Data() const
    {
        wchar_t empty[1] = L"";
        DWORD cch = 0;
        if (MsiGetPropertyW(h_, L"CustomActionData", empty, &cch) != ERROR_MORE_DATA)
            return std::wstring();
        std::wstring v(++cch, L'\0');
        if (MsiGetPropertyW(h_, L"CustomActionData", &v[0], &cch) != ERROR_SUCCESS)
            return std::wstring();
        v.resize(cch);
        while (!v.empty() && v.back() == L'\\')
            v.pop_back();
        return v;
    }

    void RebootAtEnd() { MsiSetMode(h_, MSIRUNMODE_REBOOTATEND, TRUE); }

private:
    static std::wstring Format(const wchar_t* fmt, va_list ap)
    {
        wchar_t buf[2048];
        _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
        return buf;
    }

    void Send(INSTALLMESSAGE kind, const wchar_t* tmpl, const std::wstring& text)
    {
        PMSIHANDLE rec = MsiCreateRecord(1);
        MsiRecordSetStringW(rec, 0, tmpl);
        MsiRecordSetStringW(rec, 1, text.c_str());
        MsiProcessMessage(h_, kind, rec);
    }

    MSIHANDLE h_;
};

std::wstring ErrText(DWORD e)
{
    wchar_t msg[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, msg, 512, nullptr);
    size_t n = wcslen(msg);
    while (n && (msg[n - 1] == L'\r' || msg[n - 1] == L'\n' || msg[n - 1] == L' ' || msg[n - 1] == L'.'))
        msg[--n] = 0;
    wchar_t out[600];
    swprintf_s(out, n ? L"0x%08lX, %s" : L"0x%08lX", e, msg);
    return out;
}

std::wstring Str(const wchar_t* s) { return s ? s : L""; }

bool Same(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

std::wstring SystemDir()
{
    wchar_t b[MAX_PATH] = L"";
    GetSystemDirectoryW(b, MAX_PATH);
    return b;
}

std::wstring InfDir()
{
    wchar_t b[MAX_PATH] = L"";
    GetSystemWindowsDirectoryW(b, MAX_PATH);
    return std::wstring(b) + L"\\INF";
}

bool ReadFileBytes(const std::wstring& path, std::vector<BYTE>& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (64 << 20);
    if (ok) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}

// Deletes a file; one that is still mapped (a loaded DLL) is renamed and deleted at reboot, so a
// file installed at the same path before the reboot survives.
void RemoveFile(Session& s, const std::wstring& path)
{
    if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
        return;
    wchar_t old[MAX_PATH];
    swprintf_s(old, L"%s.%08lX.old", path.c_str(), GetTickCount());
    if (MoveFileExW(path.c_str(), old, MOVEFILE_REPLACE_EXISTING))
        MoveFileExW(old, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    else
        MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    s.Log(L"%s is in use, deleted at reboot", path.c_str());
}

// Sources of the renames queued for reboot (MoveFileEx DELAY_UNTIL_REBOOT) that put a file at
// `target`.
std::vector<std::wstring> PendingRenamesTo(const std::wstring& target)
{
    std::vector<std::wstring> sources;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return sources;
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(key, L"PendingFileRenameOperations", nullptr, &type, nullptr, &cb) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
        std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, 0);
        if (RegQueryValueExW(key, L"PendingFileRenameOperations", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &cb) == ERROR_SUCCESS) {
            // Source/destination pairs; a deletion has an empty destination. Entries look like
            // "\??\C:\x", "!\??\C:\x" (replace existing) or "*1\??\C:\x".
            auto path = [](const wchar_t* e) {
                if (*e == L'*')
                    for (++e; iswdigit(*e); ++e) {}
                if (*e == L'!')
                    ++e;
                if (wcsncmp(e, L"\\??\\", 4) == 0)
                    e += 4;
                return std::wstring(e);
            };
            const wchar_t* p = buf.data();
            const wchar_t* end = p + cb / sizeof(wchar_t);
            while (p < end && *p) {
                const wchar_t* src = p;
                p += wcslen(p) + 1;
                if (p >= end)
                    break;
                const wchar_t* dst = p;
                p += wcslen(p) + 1;
                if (*dst && _wcsicmp(path(dst).c_str(), target.c_str()) == 0)
                    sources.push_back(path(src));
            }
        }
    }
    RegCloseKey(key);
    return sources;
}

bool IsArm64()
{
    using Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto f = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    USHORT process = 0, native = 0;
    return f && f(GetCurrentProcess(), &process, &native) && native == IMAGE_FILE_MACHINE_ARM64;
}

// --- spooler service --------------------------------------------------------------------------

bool WaitServiceState(SC_HANDLE h, DWORD state, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    for (;;) {
        SERVICE_STATUS_PROCESS st = {};
        DWORD cb = 0;
        if (!QueryServiceStatusEx(h, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&st), sizeof(st), &cb))
            return false;
        if (st.dwCurrentState == state)
            return true;
        if (GetTickCount() - start > timeoutMs)
            return false;
        Sleep(250);
    }
}

bool StopService(SC_HANDLE h)
{
    SERVICE_STATUS st = {};
    if (!ControlService(h, SERVICE_CONTROL_STOP, &st) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
        return false;
    return WaitServiceState(h, SERVICE_STOPPED, 30000);
}

// Running dependents (Fax, PrintNotify, ...) block the stop: they are stopped first and started
// again afterwards.
bool RestartSpooler(Session& s)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        s.Log(L"OpenSCManager: %s", ErrText(GetLastError()).c_str());
        return false;
    }
    bool ok = false;
    SC_HANDLE spooler = OpenServiceW(scm, L"Spooler",
        SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (spooler) {
        std::vector<std::wstring> deps;
        DWORD need = 0, count = 0;
        if (!EnumDependentServicesW(spooler, SERVICE_ACTIVE, nullptr, 0, &need, &count) && GetLastError() == ERROR_MORE_DATA) {
            std::vector<BYTE> buf(need);
            auto* st = reinterpret_cast<ENUM_SERVICE_STATUSW*>(buf.data());
            if (EnumDependentServicesW(spooler, SERVICE_ACTIVE, st, need, &need, &count))
                for (DWORD i = 0; i < count; ++i)
                    deps.push_back(st[i].lpServiceName);
        }
        for (const std::wstring& d : deps) {  // already in stop order
            if (SC_HANDLE h = OpenServiceW(scm, d.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS)) {
                StopService(h);
                CloseServiceHandle(h);
            }
        }
        ok = StopService(spooler) && StartServiceW(spooler, 0, nullptr) && WaitServiceState(spooler, SERVICE_RUNNING, 30000);
        DWORD e = GetLastError();
        for (auto it = deps.rbegin(); it != deps.rend(); ++it) {
            if (SC_HANDLE h = OpenServiceW(scm, it->c_str(), SERVICE_START)) {
                StartServiceW(h, 0, nullptr);
                CloseServiceHandle(h);
            }
        }
        s.Log(ok ? L"spooler restarted" : L"spooler restart failed: %s", ErrText(e).c_str());
        CloseServiceHandle(spooler);
    } else {
        s.Log(L"OpenService(Spooler): %s", ErrText(GetLastError()).c_str());
    }
    CloseServiceHandle(scm);
    return ok;
}

// --- certificate ------------------------------------------------------------------------------

PCCERT_CONTEXT LoadCertificate(const std::wstring& path)
{
    std::vector<BYTE> der;
    if (!ReadFileBytes(path, der) || der.empty())
        return nullptr;
    return CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), DWORD(der.size()));
}

bool TrustCertificate(Session& s, PCCERT_CONTEXT cert)
{
    for (const wchar_t* name : kCertStores) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, name);
        BOOL ok = store && CertAddCertificateContextToStore(store, cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
        DWORD e = GetLastError();
        if (store)
            CertCloseStore(store, 0);
        if (!ok) {
            s.Error(L"Не удалось добавить сертификат драйвера в хранилище LocalMachine\\%s: %s.", name, ErrText(e).c_str());
            return false;
        }
    }
    s.Log(L"certificate trusted");
    return true;
}

void UntrustCertificate(Session& s, const std::wstring& path)
{
    PCCERT_CONTEXT cert = LoadCertificate(path);
    if (!cert) {
        s.Log(L"cannot read %s, the certificate stays trusted", path.c_str());
        return;
    }
    for (const wchar_t* name : kCertStores) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, name);
        if (!store)
            continue;
        if (PCCERT_CONTEXT found = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                CERT_FIND_EXISTING, cert, nullptr)) {
            BOOL ok = CertDeleteCertificateFromStore(found);
            s.Log(ok ? L"certificate removed from %s" : L"cannot remove the certificate from %s", name);
        }
        CertCloseStore(store, 0);
    }
    CertFreeCertificateContext(cert);
}

// --- devices and driver store -----------------------------------------------------------------

template <class F>
void ForEachDevice(const GUID* cls, DWORD flags, F f)
{
    HDEVINFO set = SetupDiGetClassDevsW(cls, nullptr, nullptr, flags | (cls ? 0 : DIGCF_ALLCLASSES));
    if (set == INVALID_HANDLE_VALUE)
        return;
    SP_DEVINFO_DATA d = { sizeof(d) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i)
        f(set, d);
    SetupDiDestroyDeviceInfoList(set);
}

std::wstring DeviceValue(HDEVINFO set, SP_DEVINFO_DATA& d, DWORD which, const wchar_t* name)
{
    HKEY key = SetupDiOpenDevRegKey(set, &d, DICS_FLAG_GLOBAL, 0, which, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return std::wstring();
    wchar_t v[MAX_PATH] = L"";
    DWORD cb = sizeof(v);
    LSTATUS r = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, v, &cb);
    RegCloseKey(key);
    return r == ERROR_SUCCESS ? v : L"";
}

bool HasOurHardwareId(HDEVINFO set, SP_DEVINFO_DATA& d)
{
    wchar_t ids[2048] = L"";
    if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
            sizeof(ids) - 2 * sizeof(wchar_t), nullptr))
        return false;
    for (const wchar_t* id = ids; *id; id += wcslen(id) + 1)
        for (const wchar_t* ours : kHardwareIds)
            if (_wcsicmp(id, ours) == 0)
                return true;
    return false;
}

// An %windir%\INF\oemNN.inf staged from this package (any version).
bool IsOurInf(const std::wstring& path)
{
    DWORD need = 0;
    if (!SetupGetInfInformationW(path.c_str(), INFINFO_INF_NAME_IS_ABSOLUTE, nullptr, 0, &need) || !need)
        return false;
    std::vector<BYTE> buf(need);
    auto* info = reinterpret_cast<PSP_INF_INFORMATION>(buf.data());
    if (!SetupGetInfInformationW(path.c_str(), INFINFO_INF_NAME_IS_ABSOLUTE, info, need, nullptr))
        return false;
    SP_ORIGINAL_FILE_INFO_W orig = { sizeof(orig) };
    return SetupQueryInfOriginalFileInformationW(info, 0, nullptr, &orig) && _wcsicmp(orig.OriginalInfName, kInfName) == 0;
}

// Installs the driver on connected printers; PnP then creates their queues. True if one is present.
bool BindDevices(Session& s, const std::wstring& inf)
{
    bool present = false;
    for (const wchar_t* id : kHardwareIds) {
        BOOL reboot = FALSE;
        if (UpdateDriverForPlugAndPlayDevicesW(nullptr, id, inf.c_str(), 0, &reboot)) {
            s.Log(L"driver installed on %s", id);
            present = true;
            if (reboot)
                s.RebootAtEnd();
            continue;
        }
        DWORD e = GetLastError();
        if (e == ERROR_NO_SUCH_DEVINST)
            continue;  // not connected
        if (e == ERROR_NO_MORE_ITEMS) {
            s.Log(L"%s already has this or a newer driver", id);
            present = true;
            continue;
        }
        s.Log(L"UpdateDriverForPlugAndPlayDevices(%s): %s", id, ErrText(e).c_str());
    }
    return present;
}

// Device Manager's "Uninstall device" for devices bound to this driver: on the next enumeration
// they come back without a driver instead of pointing at a deleted INF.
void RemoveDevices(Session& s)
{
    const std::wstring infDir = InfDir();
    std::vector<std::wstring> ids;
    ForEachDevice(&GUID_DEVCLASS_PRINTER, 0, [&](HDEVINFO set, SP_DEVINFO_DATA& d) {
        std::wstring inf = DeviceValue(set, d, DIREG_DRV, L"InfPath");
        wchar_t id[MAX_DEVICE_ID_LEN] = L"";
        if (!inf.empty() && IsOurInf(infDir + L"\\" + inf) && SetupDiGetDeviceInstanceIdW(set, &d, id, MAX_DEVICE_ID_LEN, nullptr))
            ids.push_back(id);
    });
    for (const std::wstring& id : ids) {
        HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        SP_DEVINFO_DATA d = { sizeof(d) };
        BOOL reboot = FALSE;
        BOOL ok = set != INVALID_HANDLE_VALUE && SetupDiOpenDeviceInfoW(set, id.c_str(), nullptr, 0, &d)
            && DiUninstallDevice(nullptr, set, &d, 0, &reboot);
        s.Log(ok ? L"device %s uninstalled" : L"cannot uninstall device %s: %s", id.c_str(), ErrText(GetLastError()).c_str());
        if (set != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(set);
    }
}

void RemoveDriverPackages(Session& s)
{
    const std::wstring infDir = InfDir();
    std::vector<std::wstring> ours;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((infDir + L"\\oem*.inf").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (IsOurInf(infDir + L"\\" + fd.cFileName))
                ours.push_back(fd.cFileName);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    for (const std::wstring& name : ours) {
        const std::wstring path = infDir + L"\\" + name;
        wchar_t store[MAX_PATH] = L"";
        if (SetupGetInfDriverStoreLocationW(path.c_str(), nullptr, nullptr, store, MAX_PATH, nullptr)) {
            HRESULT hr = DeletePrinterDriverPackageW(nullptr, store, kEnvironment);
            s.Log(SUCCEEDED(hr) ? L"printer driver package %s deleted" : L"DeletePrinterDriverPackage(%s): %s",
                store, ErrText(DWORD(hr)).c_str());
        }
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        BOOL ok = SetupUninstallOEMInfW(name.c_str(), SUOI_FORCEDELETE, nullptr);
        s.Log(ok ? L"%s removed from the driver store" : L"cannot remove %s: %s", name.c_str(), ErrText(GetLastError()).c_str());
    }
}

// --- spooler objects --------------------------------------------------------------------------

struct Queue {
    std::wstring name, driver;
};

std::vector<Queue> LocalQueues()
{
    std::vector<Queue> out;
    DWORD need = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &need, &count);
    if (!need)
        return out;
    std::vector<BYTE> buf(need);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, buf.data(), need, &need, &count))
        return out;
    auto* pi = reinterpret_cast<const PRINTER_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < count; ++i)
        if (pi[i].pPrinterName && wcsncmp(pi[i].pPrinterName, L"\\\\", 2) != 0)  // not a connection
            out.push_back({ pi[i].pPrinterName, Str(pi[i].pDriverName) });
    return out;
}

bool HasOurQueue()
{
    for (const Queue& q : LocalQueues())
        if (Same(q.driver, kDriverName))
            return true;
    return false;
}

bool InstallPrinterDriver(Session& s, const std::wstring& inf)
{
    wchar_t storeInf[MAX_PATH] = L"";
    ULONG cch = MAX_PATH;
    HRESULT hr = UploadPrinterDriverPackageW(nullptr, inf.c_str(), kEnvironment, UPDP_SILENT_UPLOAD, nullptr, storeInf, &cch);
    if (FAILED(hr)) {
        s.Error(L"Не удалось добавить пакет драйвера в хранилище драйверов Windows: %s.%s", ErrText(DWORD(hr)).c_str(),
            IsWindows8OrGreater() ? L"" : L"\n\nWindows 7 проверяет подпись SHA-2 только после обновления KB4474419.");
        return false;
    }
    s.Log(L"package staged as %s", storeInf);
    hr = InstallPrinterDriverFromPackageW(nullptr, storeInf, kDriverName, kEnvironment, 0);
    if (FAILED(hr)) {
        s.Error(L"Не удалось установить драйвер принтера %s: %s.", kDriverName, ErrText(DWORD(hr)).c_str());
        return false;
    }
    s.Log(L"printer driver installed (%s)", kEnvironment);
    return true;
}

// Deletes the monitor images RemoveFile renamed to <dll>.<tick>.old; true if one is still loaded.
bool DeleteOldImages(const std::wstring& dll)
{
    bool loaded = false;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dll + L".*.old").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    const std::wstring dir = dll.substr(0, dll.rfind(L'\\') + 1);
    do {
        if (!DeleteFileW((dir + fd.cFileName).c_str()))
            loaded = true;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return loaded;
}

// The spooler keeps the language monitor loaded, so a driver update leaves the old DLL in
// System32. A mapped image can still be renamed: move it aside, copy the new one and restart the
// spooler; if that fails, the new DLL replaces the old one at reboot.
void UpdateMonitorDll(Session& s, const std::wstring& pkg)
{
    const std::wstring src = pkg + L"\\" + kArchDir + L"\\" + kMonitorDll;
    const std::wstring dst = SystemDir() + L"\\" + kMonitorDll;
    std::vector<BYTE> a, b;
    if (!ReadFileBytes(src, a)) {
        s.Log(L"cannot read %s", src.c_str());
        return;
    }
    // The class installer could not replace the loaded DLL either and queued its copy for the
    // reboot, which would undo this update when that copy is another build. Entries whose file is
    // gone are left alone: their rename fails harmlessly.
    for (const std::wstring& queued : PendingRenamesTo(dst)) {
        if (GetFileAttributesW(queued.c_str()) != INVALID_FILE_ATTRIBUTES && (!ReadFileBytes(queued, b) || a != b))
            s.Log(CopyFileW(src.c_str(), queued.c_str(), FALSE) ? L"queued replacement %s updated"
                                                               : L"cannot update queued replacement %s", queued.c_str());
    }
    if ((ReadFileBytes(dst, b) && a == b) || CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        // The file was not loaded, but the spooler may run an image an earlier update or
        // uninstall renamed.
        if (DeleteOldImages(dst)) {
            s.Log(L"an earlier language monitor is still loaded, restarting the spooler");
            if (!RestartSpooler(s))
                s.RebootAtEnd();
            DeleteOldImages(dst);  // what is left goes at reboot (RemoveFile queued it)
        } else {
            s.Log(L"language monitor is up to date");
        }
        return;
    }
    wchar_t old[MAX_PATH];
    swprintf_s(old, L"%s.%08lX.old", dst.c_str(), GetTickCount());
    bool moved = MoveFileExW(dst.c_str(), old, 0) != FALSE;
    if (moved && CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        s.Log(L"language monitor replaced, restarting the spooler");
        if (!RestartSpooler(s))
            s.RebootAtEnd();
        RemoveFile(s, old);
        return;
    }
    DWORD e = GetLastError();
    if (moved)
        MoveFileExW(old, dst.c_str(), 0);
    const std::wstring pending = dst + L".new";
    if (CopyFileW(src.c_str(), pending.c_str(), FALSE)
        && MoveFileExW(pending.c_str(), dst.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING)) {
        s.Log(L"cannot replace the language monitor (%s), replacing it at reboot", ErrText(e).c_str());
        s.RebootAtEnd();
    } else {
        s.Log(L"cannot replace the language monitor: %s", ErrText(GetLastError()).c_str());
    }
}

// Port of a connected printer: its device's PortName (usbprint, parport) or a port whose
// description names the model.
std::wstring PrinterPort()
{
    std::wstring port;
    ForEachDevice(nullptr, DIGCF_PRESENT, [&](HDEVINFO set, SP_DEVINFO_DATA& d) {
        if (port.empty() && HasOurHardwareId(set, d))
            port = DeviceValue(set, d, DIREG_DEV, L"PortName");
    });
    if (!port.empty())
        return port;
    DWORD need = 0, count = 0;
    EnumPortsW(nullptr, 2, nullptr, 0, &need, &count);
    std::vector<BYTE> buf(need);
    if (need && EnumPortsW(nullptr, 2, buf.data(), need, &need, &count)) {
        auto* pi = reinterpret_cast<const PORT_INFO_2W*>(buf.data());
        for (DWORD i = 0; i < count; ++i)
            if (pi[i].pDescription && wcsstr(pi[i].pDescription, kPortHint))
                return Str(pi[i].pPortName);
    }
    return std::wstring();
}

bool CreateQueue(Session& s, const std::wstring& port)
{
    const std::vector<Queue> queues = LocalQueues();
    auto taken = [&](const std::wstring& n) {
        for (const Queue& q : queues)
            if (Same(q.name, n.c_str()))
                return true;
        return false;
    };
    std::wstring name = kDriverName;
    for (int n = 2; taken(name); ++n)
        name = std::wstring(kDriverName) + L" (" + std::to_wstring(n) + L")";
    PRINTER_INFO_2W pi = {};
    pi.pPrinterName = &name[0];
    pi.pPortName = const_cast<LPWSTR>(port.c_str());
    pi.pDriverName = const_cast<LPWSTR>(kDriverName);
    pi.pPrintProcessor = const_cast<LPWSTR>(L"winprint");
    pi.pDatatype = const_cast<LPWSTR>(L"RAW");
    pi.Attributes = PRINTER_ATTRIBUTE_LOCAL;
    HANDLE h = AddPrinterW(nullptr, 2, reinterpret_cast<LPBYTE>(&pi));
    if (!h) {
        s.Log(L"AddPrinter(%s, %s): %s", name.c_str(), port.c_str(), ErrText(GetLastError()).c_str());
        return false;
    }
    ClosePrinter(h);
    s.Log(L"printer '%s' created on %s", name.c_str(), port.c_str());
    return true;
}

// A printer connected later gets its queue from PnP, which finds the staged package. A connected
// one normally gets it from BindDevices; the queue is created here only if PnP does not (the device
// was already bound, e.g. after its queue was deleted by hand).
void EnsureQueue(Session& s, bool devicePresent)
{
    if (!devicePresent) {
        if (!HasOurQueue())
            s.Notice(L"Драйвер принтера Canon LBP-810 установлен.\n\n"
                     L"Подключите принтер к компьютеру кабелем USB и включите его: Windows добавит его автоматически.");
        return;
    }
    for (int i = 0; i < 30; ++i) {  // PnP creates the queue asynchronously
        if (HasOurQueue()) {
            s.Log(L"printer queue present");
            return;
        }
        Sleep(500);
    }
    std::wstring port = PrinterPort();
    if (port.empty() || !CreateQueue(s, port))
        s.Log(L"no printer queue created");
}

void DeleteQueues(Session& s)
{
    for (const Queue& q : LocalQueues()) {
        if (!Same(q.driver, kDriverName))
            continue;
        PRINTER_DEFAULTSW pd = { nullptr, nullptr, PRINTER_ALL_ACCESS };
        HANDLE h = nullptr;
        if (!OpenPrinterW(const_cast<LPWSTR>(q.name.c_str()), &h, &pd)) {
            s.Log(L"OpenPrinter(%s): %s", q.name.c_str(), ErrText(GetLastError()).c_str());
            continue;
        }
        SetPrinterW(h, 0, nullptr, PRINTER_CONTROL_PURGE);
        BOOL ok = DeletePrinter(h);
        DWORD e = GetLastError();
        ClosePrinter(h);
        s.Log(ok ? L"printer '%s' deleted" : L"DeletePrinter(%s): %s", q.name.c_str(), ErrText(e).c_str());
    }
}

void DeleteDriver(Session& s)
{
    const wchar_t* const envs[] = {
        kEnvironment,
#ifdef _WIN64
        L"Windows NT x86",  // additional driver for 32-bit clients of a shared queue
#endif
    };
    for (const wchar_t* env : envs) {
        for (int attempt = 0;; ++attempt) {
            if (DeletePrinterDriverExW(nullptr, const_cast<LPWSTR>(env), const_cast<LPWSTR>(kDriverName), DPD_DELETE_UNUSED_FILES, 0)) {
                s.Log(L"printer driver deleted (%s)", env);
                break;
            }
            DWORD e = GetLastError();
            if (e == ERROR_UNKNOWN_PRINTER_DRIVER)
                break;
            if (e != ERROR_PRINTER_DRIVER_IN_USE || attempt == 10) {
                s.Log(L"DeletePrinterDriverEx(%s): %s", env, ErrText(e).c_str());
                break;
            }
            // Deleted queues release the driver a moment later; a spooler restart forces it.
            if (attempt == 5)
                RestartSpooler(s);
            Sleep(1000);
        }
    }
}

void RemoveMonitor(Session& s)
{
    for (int attempt = 0;; ++attempt) {
        if (DeleteMonitorW(nullptr, nullptr, const_cast<LPWSTR>(kMonitorName))) {
            s.Log(L"language monitor deleted");
            break;
        }
        DWORD e = GetLastError();
        if (e == ERROR_UNKNOWN_PRINT_MONITOR)
            break;
        if (e != ERROR_PRINT_MONITOR_IN_USE || attempt == 8) {
            s.Log(L"DeleteMonitor: %s", ErrText(e).c_str());
            break;
        }
        // The port of a deleted queue keeps the monitor for a while; a spooler restart frees it.
        if (attempt == 3)
            RestartSpooler(s);
        Sleep(1000);
    }
    const std::wstring dll = SystemDir() + L"\\" + kMonitorDll;
    // Replacements the class installer queued for the reboot would bring the DLL back.
    for (const std::wstring& queued : PendingRenamesTo(dll))
        s.Log(DeleteFileW(queued.c_str()) ? L"queued replacement %s deleted" : L"cannot delete queued replacement %s",
            queued.c_str());
    RemoveFile(s, dll);
}

}  // namespace

extern "C" UINT __stdcall InstallDriver(MSIHANDLE h)
{
    Session s(h);
    const std::wstring pkg = s.Data();
    s.Log(L"InstallDriver (%s) from %s", kEnvironment, pkg.c_str());
    if (IsArm64()) {
        s.Error(L"Драйвер Canon LBP-810 собран для x86 и x64; Windows на ARM64 не поддерживается.");
        return ERROR_INSTALL_FAILURE;
    }
    SetupSetNonInteractiveMode(TRUE);
    PCCERT_CONTEXT cert = LoadCertificate(pkg + L"\\" + kCertFile);
    if (!cert) {
        s.Error(L"Не удалось прочитать сертификат драйвера %s\\%s.", pkg.c_str(), kCertFile);
        return ERROR_INSTALL_FAILURE;
    }
    bool ok = TrustCertificate(s, cert);
    CertFreeCertificateContext(cert);
    const std::wstring inf = pkg + L"\\" + kInfName;
    if (!ok || !InstallPrinterDriver(s, inf))
        return ERROR_INSTALL_FAILURE;
    UpdateMonitorDll(s, pkg);
    EnsureQueue(s, BindDevices(s, inf));
    return ERROR_SUCCESS;
}

extern "C" UINT __stdcall UninstallDriver(MSIHANDLE h)
{
    Session s(h);
    const std::wstring pkg = s.Data();
    s.Log(L"UninstallDriver (%s), package %s", kEnvironment, pkg.c_str());
    SetupSetNonInteractiveMode(TRUE);
    DeleteQueues(s);
    DeleteDriver(s);
    RemoveMonitor(s);
    RemoveDevices(s);
    RemoveDriverPackages(s);
    UntrustCertificate(s, pkg + L"\\" + kCertFile);
    // Removed devices come back without a driver.
    DEVINST root = 0;
    if (CM_Locate_DevNodeW(&root, nullptr, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
        CM_Reenumerate_DevNode(root, 0);
    return ERROR_SUCCESS;
}
