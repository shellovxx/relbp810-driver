// Language monitor for the Canon LBP-810 ("LBP-810 CAPT Monitor").
//
// Sits between the spooler and the port monitor (usbmon for USB001, localmon for LPT).
// Jobs produced by CNLB810R.DLL start with Canon's 0x0001 job header; such jobs are parsed
// packet by packet and driven through the bidirectional CAPT protocol (capt::Engine).
// Anything else is passed to the port unchanged.
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <string>
#include <vector>

#include "../../common/capt.h"
#include "../../common/engine.h"
#include "../../common/spool.h"
#include "../../common/debuglog.h"
#include "resource.h"

namespace {

HMODULE g_module = nullptr;

std::wstring LoadStr(UINT id)
{
    const wchar_t* p = nullptr;
    int n = LoadStringW(g_module, id, reinterpret_cast<LPWSTR>(&p), 0);
    return n > 0 ? std::wstring(p, size_t(n)) : std::wstring();
}

struct Port;

// std::mutex from the current MSVC STL imports GetSystemTimePreciseAsFileTime (Windows 8+);
// the monitor has to load on Windows 7.
class SrwLock {
public:
    void Lock() { AcquireSRWLockExclusive(&lock_); }
    void Unlock() { ReleaseSRWLockExclusive(&lock_); }
private:
    SRWLOCK lock_ = SRWLOCK_INIT;
};

class LockGuard {
public:
    explicit LockGuard(SrwLock& l) : l_(l) { l_.Lock(); }
    ~LockGuard() { l_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    SrwLock& l_;
};

// capt::Transport over the port monitor's WritePort/ReadPort.
class PortTransport : public capt::Transport {
public:
    explicit PortTransport(Port& p) : port_(p) {}
    bool Write(const uint8_t* data, size_t len) override;
    int Read(uint8_t* data, size_t len, unsigned timeoutMs) override;
private:
    Port& port_;
};

enum class Mode { Detect, Capt, Raw };

struct Port {
    std::wstring portName;
    std::wstring printerName;
    MONITOR2 pm = {};          // port monitor functions
    HANDLE hPort = nullptr;     // port monitor handle
    SrwLock lock;

    // current job
    DWORD jobId = 0;
    HANDLE hPrinter = nullptr;
    Mode mode = Mode::Detect;
    std::vector<uint8_t> detectBuf;
    spool::Parser parser;
    spool::JobHeader header;
    spool::DocInfo doc;
    std::vector<uint8_t> pageParams;            // D0A0 payload
    std::vector<std::vector<uint8_t>> chunks;   // C0A0 payloads of the current page
    bool inPage = false;
    bool jobStarted = false;
    bool failed = false;
    bool cancelled = false;
    DWORD lastCancelCheck = 0;
    capt::Condition lastCond = capt::Condition::Ready;
    PortTransport transport{ *this };
    capt::Engine* engine = nullptr;
};

bool PortTransport::Write(const uint8_t* data, size_t len)
{
    while (len) {
        DWORD written = 0;
        DWORD chunk = len > 0x10000 ? 0x10000 : DWORD(len);
        if (!port_.pm.pfnWritePort(port_.hPort, const_cast<LPBYTE>(data), chunk, &written)) {
            dbg::Log("WritePort failed %lu", GetLastError());
            return false;
        }
        if (!written) {
            // usbmon reports 0 bytes on timeout; retry unless the job is gone
            if (port_.cancelled)
                return false;
            Sleep(10);
            continue;
        }
        data += written;
        len -= written;
    }
    return true;
}

int PortTransport::Read(uint8_t* data, size_t len, unsigned timeoutMs)
{
    if (!port_.pm.pfnReadPort)
        return -1;
    DWORD start = GetTickCount();
    for (;;) {
        DWORD got = 0;
        if (!port_.pm.pfnReadPort(port_.hPort, data, DWORD(len), &got)) {
            DWORD e = GetLastError();
            if (e != ERROR_TIMEOUT && e != ERROR_SEM_TIMEOUT) {
                dbg::Log("ReadPort failed %lu", e);
                return -1;
            }
        }
        if (got)
            return int(got);
        if (GetTickCount() - start >= timeoutMs)
            return 0;
        Sleep(1);  // CAP1EMN polls ReadPort with Sleep(1) until UsbReadTimeOut
    }
}

UINT MessageFor(capt::Condition c)
{
    switch (c) {
    case capt::Condition::PaperJam: return IDS_PAPER_JAM;
    case capt::Condition::CoverOpen: return IDS_COVER_OPEN;
    case capt::Condition::NoCartridge: return IDS_NO_CARTRIDGE;
    case capt::Condition::PaperOut: return IDS_ADD_PAPER;
    case capt::Condition::PaperMismatch: return IDS_CHANGE_PAPER;
    case capt::Condition::PrintFail: return IDS_PRINT_FAILED;
    case capt::Condition::ServiceError: return IDS_SERVICE_ERROR;
    case capt::Condition::Offline: return IDS_CHECK_PRINTER;
    case capt::Condition::Error: return IDS_PRINTER_ERROR;
    default: return 0;
    }
}

DWORD PrinterStatusFor(capt::Condition c)
{
    switch (c) {
    case capt::Condition::PaperJam: return PRINTER_STATUS_PAPER_JAM;
    case capt::Condition::CoverOpen: return PRINTER_STATUS_DOOR_OPEN;
    case capt::Condition::NoCartridge: return PRINTER_STATUS_NO_TONER;
    case capt::Condition::PaperOut: return PRINTER_STATUS_PAPER_OUT;
    case capt::Condition::Offline: return PRINTER_STATUS_NOT_AVAILABLE;
    case capt::Condition::PaperMismatch: return PRINTER_STATUS_PAPER_PROBLEM;
    case capt::Condition::ServiceError:
    case capt::Condition::PrintFail:
    case capt::Condition::Error: return PRINTER_STATUS_ERROR;
    default: return 0;
    }
}

void ReportCondition(Port& p, capt::Condition c)
{
    if (!p.hPrinter || c == p.lastCond)
        return;
    p.lastCond = c;
    std::wstring msg;
    if (UINT id = MessageFor(c))
        msg = LoadStr(id);
    // Job status string shown in the print queue
    DWORD need = 0;
    GetJobW(p.hPrinter, p.jobId, 1, nullptr, 0, &need);
    if (need) {
        std::vector<BYTE> buf(need);
        auto* ji = reinterpret_cast<JOB_INFO_1W*>(buf.data());
        if (GetJobW(p.hPrinter, p.jobId, 1, buf.data(), need, &need)) {
            ji->pStatus = msg.empty() ? nullptr : const_cast<LPWSTR>(msg.c_str());
            ji->Position = JOB_POSITION_UNSPECIFIED;
            SetJobW(p.hPrinter, p.jobId, 1, buf.data(), 0);
        }
    }
    // Level 0 + PRINTER_CONTROL_SET_STATUS takes the status value itself in place of the pointer.
    SetPrinterW(p.hPrinter, 0, reinterpret_cast<LPBYTE>(ULONG_PTR(PrinterStatusFor(c))),
                PRINTER_CONTROL_SET_STATUS);
    dbg::Log("condition %s", capt::ConditionName(c));
}

void ClearJobStatus(Port& p)
{
    DWORD need = 0;
    GetJobW(p.hPrinter, p.jobId, 1, nullptr, 0, &need);
    if (!need)
        return;
    std::vector<BYTE> buf(need);
    auto* ji = reinterpret_cast<JOB_INFO_1W*>(buf.data());
    if (GetJobW(p.hPrinter, p.jobId, 1, buf.data(), need, &need)) {
        ji->pStatus = nullptr;
        ji->Position = JOB_POSITION_UNSPECIFIED;
        SetJobW(p.hPrinter, p.jobId, 1, buf.data(), 0);
    }
}

bool JobCancelled(Port& p)
{
    if (p.cancelled)
        return true;
    DWORD now = GetTickCount();
    if (now - p.lastCancelCheck < 500)
        return false;
    p.lastCancelCheck = now;
    DWORD need = 0;
    GetJobW(p.hPrinter, p.jobId, 1, nullptr, 0, &need);
    if (!need)
        return false;
    std::vector<BYTE> buf(need);
    if (GetJobW(p.hPrinter, p.jobId, 1, buf.data(), need, &need)) {
        auto* ji = reinterpret_cast<JOB_INFO_1W*>(buf.data());
        if (ji->Status & (JOB_STATUS_DELETING | JOB_STATUS_DELETED | JOB_STATUS_RESTART)) {
            dbg::Log("job %lu cancelled (status %08lX)", p.jobId, ji->Status);
            p.cancelled = true;
        }
    }
    return p.cancelled;
}

void EnsureEngine(Port& p)
{
    if (p.engine)
        return;
    capt::EngineCallbacks cb;
    cb.log = [](const std::string& s) { dbg::Log("%s", s.c_str()); };
    cb.status = [&p](capt::Condition c, const capt::Status&) { ReportCondition(p, c); };
    cb.cancelled = [&p]() { return JobCancelled(p); };
    p.engine = new capt::Engine(p.transport, cb);
}

bool FlushPage(Port& p)
{
    if (p.failed || p.cancelled)
        return false;
    if (p.pageParams.size() != sizeof(capt::PageParams)) {
        dbg::Log("page without D0A0");
        return false;
    }
    EnsureEngine(p);
    if (!p.jobStarted) {
        if (!p.engine->BeginJob()) {
            p.failed = true;
            return false;
        }
        p.jobStarted = true;
    }
    capt::Page page;
    memcpy(&page.params, p.pageParams.data(), sizeof(page.params));
    if (p.header.flags & spool::JOBF_PAGE_FLAG)
        page.params.flags |= 1;
    page.chunks = std::move(p.chunks);
    p.chunks.clear();
    // Copies of a page follow each other (CAP1EMN repeats each page record "copies" times).
    DWORD copies = p.header.copies ? p.header.copies : 1;
    if (copies > 999)
        copies = 999;
    for (DWORD i = 0; i < copies; i++) {
        if (!p.engine->QueuePage(page)) {
            dbg::Log("QueuePage failed");
            p.failed = true;
            return false;
        }
    }
    return true;
}

void OnPacket(Port& p, const uint8_t* pkt, size_t n)
{
    uint16_t cmd = uint16_t(pkt[0] | (pkt[1] << 8));
    switch (cmd) {
    case spool::PKT_JOB_HEADER:
        if (n >= sizeof(spool::JobHeader))
            memcpy(&p.header, pkt, sizeof(p.header));
        dbg::Log("job header: copies %u collate %u order %u flags %X", p.header.copies, p.header.collate,
                 p.header.pageOrder, p.header.flags);
        break;
    case spool::PKT_DOC_INFO:
        if (n >= sizeof(spool::DocInfo))
            memcpy(&p.doc, pkt, sizeof(p.doc));
        break;
    case capt::CMD_PAGE_PARAMS:
        p.pageParams.assign(pkt + 4, pkt + n);
        p.chunks.clear();
        p.inPage = true;
        break;
    case capt::CMD_PAGE_DATA:
        p.chunks.emplace_back(pkt + 4, pkt + n);
        break;
    case spool::PKT_PAGE_END:
        if (p.inPage)
            FlushPage(p);
        p.inPage = false;
        break;
    default:
        break;  // D0A1/D0A2 are regenerated by the engine; 0x0003 handled at EndDocPort
    }
}

// ---- MONITOR2 entry points --------------------------------------------------------------

BOOL WINAPI LmOpenPortEx(HANDLE, HANDLE hMonitorPort, LPWSTR pPortName, LPWSTR pPrinterName, PHANDLE pHandle,
                         MONITOR2* pMonitor)
{
    if (!pMonitor || !pMonitor->pfnOpenPort || !pHandle) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    auto* p = new (std::nothrow) Port;
    if (!p) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    size_t cb = pMonitor->cbSize < sizeof(MONITOR2) ? pMonitor->cbSize : sizeof(MONITOR2);
    memcpy(&p->pm, pMonitor, cb);
    if (!p->pm.pfnOpenPort(hMonitorPort, pPortName, &p->hPort)) {
        DWORD e = GetLastError();
        delete p;
        SetLastError(e);
        return FALSE;
    }
    p->portName = pPortName ? pPortName : L"";
    p->printerName = pPrinterName ? pPrinterName : L"";
    dbg::Log("OpenPortEx %ls for '%ls'", p->portName.c_str(), p->printerName.c_str());
    *pHandle = p;
    return TRUE;
}

BOOL WINAPI LmStartDocPort(HANDLE hPort, LPWSTR pPrinterName, DWORD JobId, DWORD Level, LPBYTE pDocInfo)
{
    auto* p = static_cast<Port*>(hPort);
    LockGuard g(p->lock);
    if (!p->pm.pfnStartDocPort(p->hPort, pPrinterName, JobId, Level, pDocInfo))
        return FALSE;
    p->jobId = JobId;
    p->mode = Mode::Detect;
    p->detectBuf.clear();
    p->parser.Reset();
    p->header = spool::JobHeader();
    p->doc = spool::DocInfo();
    p->pageParams.clear();
    p->chunks.clear();
    p->inPage = false;
    p->jobStarted = false;
    p->failed = false;
    p->cancelled = false;
    p->lastCond = capt::Condition::Ready;
    PRINTER_DEFAULTSW pd = { nullptr, nullptr, PRINTER_ACCESS_ADMINISTER };
    if (!OpenPrinterW(pPrinterName, &p->hPrinter, &pd))
        OpenPrinterW(pPrinterName, &p->hPrinter, nullptr);
    if (p->pm.pfnSetPortTimeOuts) {
        COMMTIMEOUTS cto = { 0, 10, 500, 10, 10000 };
        p->pm.pfnSetPortTimeOuts(p->hPort, &cto, 0);
    }
    dbg::Log("StartDocPort job %lu printer '%ls'", JobId, pPrinterName ? pPrinterName : L"");
    return TRUE;
}

BOOL WINAPI LmWritePort(HANDLE hPort, LPBYTE pBuffer, DWORD cbBuf, LPDWORD pcbWritten)
{
    auto* p = static_cast<Port*>(hPort);
    LockGuard g(p->lock);
    *pcbWritten = 0;
    const uint8_t* data = pBuffer;
    size_t n = cbBuf;

    if (p->mode == Mode::Detect) {
        p->detectBuf.insert(p->detectBuf.end(), data, data + n);
        if (p->detectBuf.size() < 8) {
            *pcbWritten = cbBuf;
            return TRUE;
        }
        static const uint8_t sig[8] = { 0x01, 0x00, 0x18, 0x00, 0xCE, 0xDA, 0xDE, 0xFA };
        p->mode = memcmp(p->detectBuf.data(), sig, 8) == 0 ? Mode::Capt : Mode::Raw;
        dbg::Log("job %lu mode %s", p->jobId, p->mode == Mode::Capt ? "CAPT" : "raw");
        std::vector<uint8_t> pending;
        pending.swap(p->detectBuf);
        if (p->mode == Mode::Raw) {
            DWORD w = 0;
            if (!p->transport.Write(pending.data(), pending.size()))
                return FALSE;
            (void)w;
        } else {
            p->parser.Feed(pending.data(), pending.size(),
                           [p](const uint8_t* pkt, size_t sz) { OnPacket(*p, pkt, sz); });
        }
        *pcbWritten = cbBuf;
        return !p->cancelled;
    }

    if (p->mode == Mode::Raw) {
        if (!p->pm.pfnWritePort(p->hPort, pBuffer, cbBuf, pcbWritten))
            return FALSE;
        return TRUE;
    }

    p->parser.Feed(data, n, [p](const uint8_t* pkt, size_t sz) { OnPacket(*p, pkt, sz); });
    *pcbWritten = cbBuf;
    if (p->cancelled) {
        SetLastError(ERROR_PRINT_CANCELLED);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI LmReadPort(HANDLE hPort, LPBYTE pBuffer, DWORD cbBuffer, LPDWORD pcbRead)
{
    auto* p = static_cast<Port*>(hPort);
    if (!p->pm.pfnReadPort) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return p->pm.pfnReadPort(p->hPort, pBuffer, cbBuffer, pcbRead);
}

BOOL WINAPI LmEndDocPort(HANDLE hPort)
{
    auto* p = static_cast<Port*>(hPort);
    LockGuard g(p->lock);
    if (p->mode == Mode::Detect && !p->detectBuf.empty()) {
        p->transport.Write(p->detectBuf.data(), p->detectBuf.size());
        p->detectBuf.clear();
    }
    if (p->engine && p->jobStarted) {
        if (p->cancelled || p->failed || JobCancelled(*p))
            p->engine->AbortJob();
        else if (!p->engine->EndJob())
            p->failed = true;
    }
    delete p->engine;
    p->engine = nullptr;
    if (p->hPrinter) {
        SetPrinterW(p->hPrinter, 0, nullptr, PRINTER_CONTROL_SET_STATUS);  // status 0
        if (p->lastCond != capt::Condition::Ready) {
            p->lastCond = capt::Condition::Ready;
            ClearJobStatus(*p);
        }
    }
    BOOL ok = p->pm.pfnEndDocPort(p->hPort);
    if (p->hPrinter) {
        SetJobW(p->hPrinter, p->jobId, 0, nullptr, JOB_CONTROL_LAST_PAGE_EJECTED);
        ClosePrinter(p->hPrinter);
        p->hPrinter = nullptr;
    }
    dbg::Log("EndDocPort job %lu (%s)", p->jobId, p->cancelled ? "cancelled" : p->failed ? "failed" : "ok");
    return ok;
}

BOOL WINAPI LmClosePort(HANDLE hPort)
{
    auto* p = static_cast<Port*>(hPort);
    BOOL ok = p->pm.pfnClosePort(p->hPort);
    delete p->engine;
    delete p;
    return ok;
}

BOOL WINAPI LmGetPrinterDataFromPort(HANDLE hPort, DWORD ControlID, LPWSTR pValueName, LPWSTR lpInBuffer,
                                     DWORD cbInBuffer, LPWSTR lpOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbReturned)
{
    auto* p = static_cast<Port*>(hPort);
    if (!p->pm.pfnGetPrinterDataFromPort) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return p->pm.pfnGetPrinterDataFromPort(p->hPort, ControlID, pValueName, lpInBuffer, cbInBuffer, lpOutBuffer,
                                           cbOutBuffer, lpcbReturned);
}

BOOL WINAPI LmSetPortTimeOuts(HANDLE hPort, LPCOMMTIMEOUTS lpCTO, DWORD reserved)
{
    auto* p = static_cast<Port*>(hPort);
    if (!p->pm.pfnSetPortTimeOuts) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return p->pm.pfnSetPortTimeOuts(p->hPort, lpCTO, reserved);
}

VOID WINAPI LmShutdown(HANDLE) {}

MONITOR2 g_monitor = {};

} // namespace

extern "C" LPMONITOR2 WINAPI InitializePrintMonitor2(PMONITORINIT pMonitorInit, PHANDLE phMonitor)
{
    g_monitor.cbSize = sizeof(MONITOR2);
    g_monitor.pfnOpenPortEx = LmOpenPortEx;
    g_monitor.pfnStartDocPort = LmStartDocPort;
    g_monitor.pfnWritePort = LmWritePort;
    g_monitor.pfnReadPort = LmReadPort;
    g_monitor.pfnEndDocPort = LmEndDocPort;
    g_monitor.pfnClosePort = LmClosePort;
    g_monitor.pfnGetPrinterDataFromPort = LmGetPrinterDataFromPort;
    g_monitor.pfnSetPortTimeOuts = LmSetPortTimeOuts;
    g_monitor.pfnShutdown = LmShutdown;
    *phMonitor = reinterpret_cast<HANDLE>(pMonitorInit);
    dbg::Log("InitializePrintMonitor2");
    return &g_monitor;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
        dbg::Init(L"langmon");
    }
    return TRUE;
}
