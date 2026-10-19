// Development harness: runs the language monitor (CNLB810L.DLL) outside the spooler.
// A fake port monitor forwards WritePort/ReadPort to the USB printer, so a spool file
// produced by the rendering plug-in (or `captcli spool`) can be printed end to end.
//
// usage: lmtest <path to CNLB810L.DLL> <spool file>
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <cstdio>
#include <vector>
#include "../usbdev.h"

static UsbPrinter g_usb;
static DWORD g_readTimeout = 500;

static BOOL WINAPI FakeOpenPort(HANDLE, LPWSTR name, PHANDLE h)
{
    for (auto& i : EnumUsbPrinters()) {
        if (i.deviceId.find("CAPT") != std::string::npos && g_usb.Open(i.path)) {
            printf("[port] opened %ls for %ls\n", i.path.c_str(), name);
            *h = &g_usb;
            return TRUE;
        }
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
static BOOL WINAPI FakeStartDocPort(HANDLE, LPWSTR, DWORD, DWORD, LPBYTE) { return TRUE; }
static BOOL WINAPI FakeWritePort(HANDLE, LPBYTE p, DWORD n, LPDWORD w)
{
    *w = 0;
    if (!g_usb.Write(p, n))
        return FALSE;
    *w = n;
    return TRUE;
}
static BOOL WINAPI FakeReadPort(HANDLE, LPBYTE p, DWORD n, LPDWORD r)
{
    int got = g_usb.Read(p, n, g_readTimeout);
    if (got < 0)
        return FALSE;
    *r = DWORD(got);
    return TRUE;
}
static BOOL WINAPI FakeEndDocPort(HANDLE) { return TRUE; }
static BOOL WINAPI FakeClosePort(HANDLE) { g_usb.Close(); return TRUE; }
static BOOL WINAPI FakeSetPortTimeOuts(HANDLE, LPCOMMTIMEOUTS cto, DWORD)
{
    g_readTimeout = cto->ReadTotalTimeoutConstant ? cto->ReadTotalTimeoutConstant : 500;
    return TRUE;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        printf("usage: lmtest <CNLB810L.DLL> <spool file>\n");
        return 2;
    }
    HMODULE lm = LoadLibraryW(argv[1]);
    if (!lm) {
        printf("LoadLibrary failed %lu\n", GetLastError());
        return 1;
    }
    auto init = reinterpret_cast<LPMONITOR2(WINAPI*)(PMONITORINIT, PHANDLE)>(GetProcAddress(lm, "InitializePrintMonitor2"));
    MONITORINIT mi = { sizeof(mi) };
    HANDLE hMon = nullptr;
    LPMONITOR2 m = init(&mi, &hMon);

    MONITOR2 port = {};
    port.cbSize = sizeof(port);
    port.pfnOpenPort = FakeOpenPort;
    port.pfnStartDocPort = FakeStartDocPort;
    port.pfnWritePort = FakeWritePort;
    port.pfnReadPort = FakeReadPort;
    port.pfnEndDocPort = FakeEndDocPort;
    port.pfnClosePort = FakeClosePort;
    port.pfnSetPortTimeOuts = FakeSetPortTimeOuts;

    FILE* f = nullptr;
    if (_wfopen_s(&f, argv[2], L"rb") || !f) {
        printf("cannot open spool file\n");
        return 1;
    }
    std::vector<BYTE> data;
    BYTE buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(f);
    printf("spool file: %zu bytes\n", data.size());

    HANDLE h = nullptr;
    wchar_t portName[] = L"USB001", printerName[] = L"lmtest";
    if (!m->pfnOpenPortEx(hMon, nullptr, portName, printerName, &h, &port)) {
        printf("OpenPortEx failed %lu\n", GetLastError());
        return 1;
    }
    DOC_INFO_1W di = { const_cast<LPWSTR>(L"lmtest"), nullptr, const_cast<LPWSTR>(L"RAW") };
    if (!m->pfnStartDocPort(h, printerName, 1, 1, reinterpret_cast<LPBYTE>(&di))) {
        printf("StartDocPort failed %lu\n", GetLastError());
        return 1;
    }
    DWORD t0 = GetTickCount();
    for (size_t off = 0; off < data.size(); off += 4096) {
        DWORD chunk = DWORD(data.size() - off < 4096 ? data.size() - off : 4096), w = 0;
        if (!m->pfnWritePort(h, &data[off], chunk, &w)) {
            printf("WritePort failed at %zu: %lu\n", off, GetLastError());
            break;
        }
    }
    BOOL ok = m->pfnEndDocPort(h);
    printf("EndDocPort %s after %.1f s\n", ok ? "ok" : "failed", (GetTickCount() - t0) / 1000.0);
    m->pfnClosePort(h);
    return ok ? 0 : 1;
}
