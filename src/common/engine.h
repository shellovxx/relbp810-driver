// CAPT 1.x printing engine for the LBP-810 (modelled on Canon's CAP1EMN.DLL engine manager).
#pragma once
#include "capt.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace capt {

// Byte transport to the printer (USB via usbprint, or the spooler's port monitor).
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool Write(const uint8_t* data, size_t len) = 0;
    // Returns number of bytes read, 0 on timeout, < 0 on error.
    virtual int Read(uint8_t* data, size_t len, unsigned timeoutMs) = 0;
};

// One page ready to send: parameters plus SCoA chunks (each starts with 0x40, even length).
struct Page {
    PageParams params = {};
    std::vector<std::vector<uint8_t>> chunks;  // last chunk should be the "@B" end-of-page chunk
};

// High level printer condition derived from the status bytes.
enum class Condition {
    Ready,
    Busy,          // printing / warming up
    NotReady,      // initializing
    PaperOut,
    PaperJam,
    CoverOpen,
    NoCartridge,
    PaperMismatch, // loaded paper differs from the page size (A2A1 engine info)
    PrintFail,     // data arrived too late (print overrun), page must be reprinted
    ServiceError,
    Offline,       // no reply / communication error
    Error,         // unspecified error bits
};

const char* ConditionName(Condition c);

struct EngineCallbacks {
    std::function<void(const std::string&)> log;              // protocol trace (optional)
    std::function<void(Condition, const Status&)> status;      // condition changes
    std::function<bool()> cancelled;                           // polled while waiting
};

class Engine {
public:
    Engine(Transport& t, EngineCallbacks cb = {}) : t_(t), cb_(std::move(cb)) {}

    // Low level
    bool Command(uint16_t cmd, const void* payload, size_t n, std::vector<uint8_t>* reply);
    bool Send(uint16_t cmd, const void* payload, size_t n);

    // Status
    bool RefreshStatus();          // E0A0 (+A0A0) inside a session, A0A0 outside
    bool QueryTray();              // A0A1
    const Status& LastStatus() const { return status_; }
    Condition CurrentCondition() const { return cond_; }
    uint16_t ModelCode() const { return model_; }

    // Job (modelled on CAP1EMN's job loop). Pages stay queued until the printer reports them
    // completed. After any printer error (jam, cover, paper out, transfer failure...) the engine
    // waits until the printer is idle and the error is gone, sets the job up again with E0A4 and
    // E0A5(number of completed pages) and sends the pages that were not completed again.
    bool BeginJob();               // A1A1, A0A0, A2A0
    bool QueuePage(const Page& page);  // returns once the page (and all before it) has been sent
    bool EndJob();                 // waits until all pages are completed, then E0A9
    void AbortJob();               // E0A6 + E0A9

    int PagesQueued() const { return queuedTotal_; }
    int PagesCompleted() const { return completedTotal_; }

    // Packs the page chunks into <= kMaxDataPacket data packets exactly like CAP1EMN.
    static std::vector<std::vector<uint8_t>> PackData(const std::vector<std::vector<uint8_t>>& chunks);

private:
    enum class Step { Ok, Retry, Fatal };
    bool ReadReply(uint16_t cmd, std::vector<uint8_t>& reply, unsigned timeoutMs);
    Step WaitFlow(uint8_t busyMask, bool pageStart = false);
    Step SendOne(const Page& page);
    Step StartSession();           // clear errors, E0A4, E0A5(completed)
    bool Pump(bool drain);
    void QueryDetails();           // A2A1 / A2A2 when the status asks for them
    void UpdateCompleted();
    bool CanSend(bool pageStart) const;
    void FinishSession(bool cancel);
    bool Sleep(unsigned ms);
    void UpdateCondition();
    void Log(const char* fmt, ...);
    bool Cancelled() const { return cb_.cancelled && cb_.cancelled(); }

    Transport& t_;
    EngineCallbacks cb_;
    Status status_;
    Condition cond_ = Condition::Offline;
    bool session_ = false;         // A2A0 done
    bool jobActive_ = false;       // between BeginJob and EndJob/AbortJob
    bool started_ = false;         // E0A4/E0A5 accepted and not dropped by the printer (flow 0x20)
    bool restart_ = false;         // the job must be set up again before sending
    bool paperWait_ = false;       // next page waits for paper and nothing is being fed
    bool engineInfoValid_ = false; // engineInfo_ holds the A2A1 reply for the current STAT_ENGINE_INFO
    uint8_t engineInfo_[8] = {};
    uint16_t extError_ = 0;        // A2A2 bytes 6-7
    int autoResumes_ = 0;          // automatic recoveries from print failures in this job
    bool failCounted_ = false;     // the current print/paper failure has been handled
    int frontAttempts_ = 0;        // times the oldest pending page has been sent
    uint16_t model_ = 0;
    std::deque<Page> pending_;     // queued, not yet completed pages (chunks already packed)
    size_t sent_ = 0;              // pages of pending_ sent since the last E0A5
    int queuedTotal_ = 0;
    int completedTotal_ = 0;
    std::vector<uint8_t> rx_;
};

} // namespace capt
