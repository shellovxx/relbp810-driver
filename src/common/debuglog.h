// Optional diagnostics, enabled by HKLM\SOFTWARE\LBP810x\Debug (DWORD, non-zero).
// Logs go to %ProgramData%\LBP810x\<component>.log; DumpPages=1 also writes every page as PBM.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>

namespace dbg {

struct State {
    bool enabled = false;
    bool dumpPages = false;
    wchar_t dir[MAX_PATH] = {};
    wchar_t component[32] = {};
};

inline State& St()
{
    static State s;
    return s;
}

inline DWORD ReadDword(const wchar_t* name)
{
    DWORD v = 0, cb = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\LBP810x", name, RRF_RT_REG_DWORD, nullptr, &v, &cb) != ERROR_SUCCESS)
        return 0;
    return v;
}

inline void Init(const wchar_t* component)
{
    State& s = St();
    s.enabled = ReadDword(L"Debug") != 0;
    s.dumpPages = s.enabled && ReadDword(L"DumpPages") != 0;
    wcsncpy_s(s.component, component, _TRUNCATE);
    wchar_t pd[MAX_PATH];
    if (!GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH))
        wcscpy_s(pd, L"C:\\ProgramData");
    swprintf_s(s.dir, L"%s\\LBP810x", pd);
    // Flag files work where HKLM\SOFTWARE is locked down.
    wchar_t flag[MAX_PATH];
    swprintf_s(flag, L"%s\\debug.on", s.dir);
    if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES)
        s.enabled = true;
    swprintf_s(flag, L"%s\\dumppages.on", s.dir);
    if (s.enabled && GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES)
        s.dumpPages = true;
    // Test harnesses can redirect logging with LBP810X_LOGDIR=<directory>.
    wchar_t env[MAX_PATH];
    if (GetEnvironmentVariableW(L"LBP810X_LOGDIR", env, MAX_PATH)) {
        wcscpy_s(s.dir, env);
        s.enabled = true;
        s.dumpPages = true;
    }
}

inline void Log(const char* fmt, ...)
{
    State& s = St();
    if (!s.enabled)
        return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg) - 3, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > int(sizeof(msg)) - 3)
        n = int(sizeof(msg)) - 3;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[2200];
    int m = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [%lu:%lu] %.*s\r\n", t.wHour, t.wMinute, t.wSecond,
                     t.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), n, msg);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\%s.log", s.dir, s.component);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD w;
    WriteFile(h, line, DWORD(m), &w, nullptr);
    CloseHandle(h);
}

// Writes a 1bpp page (1 = black) as a binary PBM file.
inline void DumpPage(int pageNo, const uint8_t* bits, size_t stride, int w, int h)
{
    State& s = St();
    if (!s.dumpPages)
        return;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\page_%lu_%03d.pbm", s.dir, GetCurrentProcessId(), pageNo);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") || !f)
        return;
    fprintf(f, "P4\n%d %d\n", w, h);
    size_t rowBytes = (size_t(w) + 7) / 8;
    for (int y = 0; y < h; y++)
        fwrite(bits + stride * size_t(y), 1, rowBytes, f);
    fclose(f);
}

} // namespace dbg
