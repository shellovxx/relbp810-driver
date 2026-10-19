#include "engine.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace capt {

namespace {
constexpr unsigned kReplyTimeoutMs = 10000;  // CAPPTMN sets 10 s port timeouts
constexpr unsigned kBusyPollMs = 200;        // CAP1EMN default "busy" interval
constexpr unsigned kIdlePollMs = 1000;       // CAP1EMN default "idle" interval
constexpr int kMaxPageAttempts = 4;          // sends of the oldest pending page before giving up

std::string HexDump(const uint8_t* p, size_t n, size_t max = 48)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < n && i < max; i++) {
        snprintf(b, sizeof(b), "%02X ", p[i]);
        s += b;
    }
    if (n > max)
        s += "...";
    return s;
}
} // namespace

const char* ConditionName(Condition c)
{
    switch (c) {
    case Condition::Ready: return "ready";
    case Condition::Busy: return "busy";
    case Condition::NotReady: return "not ready";
    case Condition::PaperOut: return "paper out";
    case Condition::PaperJam: return "paper jam";
    case Condition::CoverOpen: return "cover open";
    case Condition::NoCartridge: return "no cartridge";
    case Condition::PaperMismatch: return "paper size mismatch";
    case Condition::PrintFail: return "print failed (data late)";
    case Condition::ServiceError: return "service error";
    case Condition::Offline: return "offline";
    default: return "error";
    }
}

void Engine::Log(const char* fmt, ...)
{
    if (!cb_.log)
        return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    cb_.log(buf);
}

bool Engine::Sleep(unsigned ms)
{
    while (ms) {
        if (Cancelled())
            return false;
        unsigned step = ms > 50 ? 50 : ms;
        ::Sleep(step);
        ms -= step;
    }
    return !Cancelled();
}

bool Engine::Send(uint16_t cmd, const void* payload, size_t n)
{
    std::vector<uint8_t> pkt(4 + n);
    pkt[0] = uint8_t(cmd);
    pkt[1] = uint8_t(cmd >> 8);
    pkt[2] = uint8_t(pkt.size());
    pkt[3] = uint8_t(pkt.size() >> 8);
    if (n)
        memcpy(&pkt[4], payload, n);
    if (cmd != CMD_PAGE_DATA)
        Log("W %s", HexDump(pkt.data(), pkt.size()).c_str());
    if (!t_.Write(pkt.data(), pkt.size())) {
        Log("write failed (cmd %04X)", cmd);
        cond_ = Condition::Offline;
        return false;
    }
    return true;
}

bool Engine::ReadReply(uint16_t cmd, std::vector<uint8_t>& reply, unsigned timeoutMs)
{
    DWORD start = GetTickCount();
    uint8_t buf[512];
    for (;;) {
        if (rx_.size() >= 4) {
            size_t sz = rx_[2] | (rx_[3] << 8);
            if (sz < 6 || sz > 4096) {
                Log("bad reply header %s", HexDump(rx_.data(), rx_.size()).c_str());
                rx_.clear();
                return false;
            }
            if (rx_.size() >= sz) {
                reply.assign(rx_.begin(), rx_.begin() + sz);
                rx_.erase(rx_.begin(), rx_.begin() + sz);
                Log("R %s", HexDump(reply.data(), reply.size()).c_str());
                uint16_t got = uint16_t(reply[0] | (reply[1] << 8));
                if (got != cmd) {
                    Log("reply for %04X while waiting for %04X", got, cmd);
                    continue;
                }
                return true;
            }
        }
        unsigned elapsed = GetTickCount() - start;
        if (elapsed >= timeoutMs) {
            Log("reply timeout (cmd %04X)", cmd);
            return false;
        }
        int n = t_.Read(buf, sizeof(buf), timeoutMs - elapsed);
        if (n < 0) {
            Log("read failed (cmd %04X)", cmd);
            return false;
        }
        rx_.insert(rx_.end(), buf, buf + n);
    }
}

bool Engine::Command(uint16_t cmd, const void* payload, size_t n, std::vector<uint8_t>* reply)
{
    std::vector<uint8_t> tmp;
    std::vector<uint8_t>& r = reply ? *reply : tmp;
    if (!Send(cmd, payload, n))
        return false;
    if (!ReadReply(cmd, r, kReplyTimeoutMs)) {
        cond_ = Condition::Offline;
        return false;
    }
    if (r.size() >= 5 && (r[4] & FLOW_ERROR)) {
        Log("command %04X returned error %02X", cmd, r[4]);
        return false;
    }
    return true;
}

void Engine::UpdateCondition()
{
    // Bit meanings follow CAP1EMN sub_10004130/sub_10005B60 and the screen selection in CAP1PMN.
    const Status& s = status_;
    Condition c;
    uint8_t flow = s.Flow();
    uint16_t w = s.Word10();
    bool engineInfo = engineInfoValid_ && (w & STAT_ENGINE_INFO);
    if (flow & FLOW_ERROR)
        c = Condition::Error;
    else if (w & STAT_PAPER_JAM)
        c = Condition::PaperJam;
    else if (w & STAT_COVER_OPEN)
        c = Condition::CoverOpen;
    else if (w & STAT_NO_CARTRIDGE)
        c = Condition::NoCartridge;
    else if ((s.Byte7() & 0x10) || (w & STAT_EXT_ERROR) || (engineInfo && (engineInfo_[7] & 0x18)))
        c = Condition::ServiceError;
    else if (engineInfo && (engineInfo_[7] & 0x04) && jobActive_ && started_)
        c = Condition::PaperMismatch;
    else if (s.Byte7() & 0x0F)
        c = Condition::PrintFail;
    else if (jobActive_ ? paperWait_ : (s.TrayMask() == 0 || (w & STAT_TRAY_DISABLED)))
        c = Condition::PaperOut;  // in a job only once a page waits for paper (CAP1EMN 0x10000)
    else if (session_ && (flow & FLOW_NOT_READY))
        c = Condition::NotReady;
    else if (s.Byte6() & 0x84)
        c = Condition::Busy;
    else
        c = Condition::Ready;
    if (c != Condition::PrintFail && c != Condition::PaperMismatch)
        failCounted_ = false;
    if (c != cond_) {
        cond_ = c;
        Log("condition: %s", ConditionName(c));
        if (cb_.status)
            cb_.status(c, status_);
    }
}

void Engine::QueryDetails()
{
    std::vector<uint8_t> r;
    uint16_t w = status_.Word10();
    // A2A1 engine info: CAP1EMN asks once per occurrence of word10 & 0xC0 and keeps the reply.
    if (w & STAT_ENGINE_INFO) {
        if (!engineInfoValid_ && Command(CMD_ENGINE_INFO, nullptr, 0, &r) && r.size() >= 8) {
            memcpy(engineInfo_, r.data(), 8);
            engineInfoValid_ = true;
        }
    } else {
        engineInfoValid_ = false;
    }
    // A2A2 extended error: queried on every status while word10 & 2 is set.
    if (w & STAT_EXT_ERROR) {
        if (Command(CMD_EXT_ERROR, nullptr, 0, &r) && r.size() >= 8)
            extError_ = uint16_t(r[6] | (r[7] << 8));
    } else {
        extError_ = 0;
    }
}

void Engine::UpdateCompleted()
{
    // CAP1EMN counts pages only after E0A5 and drops the job set-up when the printer reports
    // flow 0x20; the printer restarts its counters at the page number given to E0A5.
    if (!started_)
        return;
    if (status_.Flow() & 0x20) {
        Log("printer dropped the job set-up (flow %02X)", status_.Flow());
        started_ = false;
        return;
    }
    uint16_t done = uint16_t(status_.PagesCompleted() - uint16_t(completedTotal_));
    if (!done || done > sent_)
        return;
    for (uint16_t i = 0; i < done; i++) {
        pending_.pop_front();
        sent_--;
        completedTotal_++;
        frontAttempts_ = 0;
        Log("page %d completed (%d/%d)", completedTotal_, completedTotal_, queuedTotal_);
    }
}

bool Engine::RefreshStatus()
{
    std::vector<uint8_t> r;
    bool full = true;
    if (session_) {
        if (!Command(CMD_FLOW_STATUS, nullptr, 0, &r))
            return false;
        full = r.size() >= 6 && (r[5] & 1);
        if (!full) {
            memcpy(status_.raw, r.data(), 6);
        }
    }
    if (full) {
        if (!Command(CMD_STATUS, nullptr, 0, &r))
            return false;
        memset(status_.raw, 0, sizeof(status_.raw));
        memcpy(status_.raw, r.data(), r.size() < sizeof(status_.raw) ? r.size() : sizeof(status_.raw));
        QueryDetails();
        UpdateCompleted();
    }
    UpdateCondition();
    return true;
}

bool Engine::QueryTray()
{
    std::vector<uint8_t> r;
    return Command(CMD_TRAY_INFO, nullptr, 0, &r);
}

bool Engine::CanSend(bool pageStart) const
{
    // CAP1EMN sub_10004A40 (inside a page): no error, engine ready and the job set up;
    // sub_10004AD0 (before a page) also requires the transfer error bits 0x4F to be clear.
    switch (cond_) {
    case Condition::Ready:
    case Condition::Busy:
    case Condition::NotReady:
        break;
    default:
        return false;
    }
    if (pageStart && (status_.Byte7() & 0x4F))
        return false;
    return started_ && !restart_ && !(status_.Flow() & FLOW_NOT_READY);
}

Engine::Step Engine::WaitFlow(uint8_t busyMask, bool pageStart)
{
    for (;;) {
        if (!RefreshStatus())
            return Step::Fatal;
        if (!CanSend(pageStart))
            return Step::Retry;
        if (!(status_.Flow() & busyMask))
            return Step::Ok;
        if (!Sleep(kBusyPollMs))
            return Step::Fatal;
    }
}

bool Engine::BeginJob()
{
    std::vector<uint8_t> r;
    pending_.clear();
    sent_ = 0;
    queuedTotal_ = 0;
    completedTotal_ = 0;
    autoResumes_ = 0;
    failCounted_ = false;
    frontAttempts_ = 0;
    started_ = false;
    restart_ = false;
    paperWait_ = false;
    engineInfoValid_ = false;
    if (!Command(CMD_CAPABILITIES, nullptr, 0, &r) || r.size() < 8)
        return false;
    model_ = uint16_t(r[6] | (r[7] << 8));
    Log("model code %u", model_);

    // Full status first: inside the session E0A0 only refreshes bytes 0..5 (CAP1EMN sub_10003C50).
    session_ = false;
    if (!RefreshStatus())
        return false;

    if (!Command(CMD_OPEN_SESSION, nullptr, 0, &r))
        return false;
    session_ = true;
    jobActive_ = true;
    return RefreshStatus() && QueryTray();
}

Engine::Step Engine::StartSession()
{
    // CAP1EMN sub_10003B20 + sub_10005350: clear what blocks the engine, then E0A4 and E0A5 with
    // the number of pages already completed, so the printer continues the job from there.
    std::vector<uint8_t> r;
    if (status_.Word10() & STAT_ENGINE_INFO) {
        if (status_.Byte6() & 4)
            return Step::Retry;
        if (!Command(CMD_CLEAR_ERROR2, nullptr, 0, &r))
            return cond_ == Condition::Offline ? Step::Fatal : Step::Retry;
        engineInfoValid_ = false;
    }
    if (status_.Byte7() & 0x4F) {
        if (!Command(CMD_CLEAR_ERROR, nullptr, 0, &r))
            return cond_ == Condition::Offline ? Step::Fatal : Step::Retry;
    }
    if (!Command(CMD_JOB_START, nullptr, 0, &r))
        return cond_ == Condition::Offline ? Step::Fatal : Step::Retry;
    uint16_t first = uint16_t(completedTotal_);
    uint8_t setup[8] = { 0xEE, 0xDB, 0xEA, 0xAD, uint8_t(first), uint8_t(first >> 8), 0, 0 };
    if (!Command(CMD_JOB_SETUP, setup, sizeof(setup), &r))
        return cond_ == Condition::Offline ? Step::Fatal : Step::Retry;
    started_ = true;
    restart_ = false;
    paperWait_ = false;
    sent_ = 0;
    Log("job set up from page %d (%zu pending)", completedTotal_ + 1, pending_.size());
    return Step::Ok;
}

std::vector<std::vector<uint8_t>> Engine::PackData(const std::vector<std::vector<uint8_t>>& chunks)
{
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> cur;
    for (auto& c : chunks) {
        if (!cur.empty() && 4 + cur.size() + 4 + c.size() > kMaxDataPacket) {
            packets.push_back(std::move(cur));
            cur.clear();
        }
        cur.insert(cur.end(), c.begin(), c.end());
    }
    if (!cur.empty())
        packets.push_back(std::move(cur));
    return packets;
}

Engine::Step Engine::SendOne(const Page& page)
{
    // page.chunks already holds the packed C0A0 payloads (QueuePage)
    Log("sending page %d (%zu packets, %u x 32K)", completedTotal_ + int(sent_) + 1, page.chunks.size(),
        unsigned(page.params.dataSize32k));
    Step s;
    if ((s = WaitFlow(FLOW_CTRL_BUSY, true)) != Step::Ok)
        return s;
    if (!Send(CMD_PAGE_PARAMS, &page.params, sizeof(page.params)))
        return Step::Fatal;
    if ((s = WaitFlow(FLOW_CTRL_BUSY)) != Step::Ok)
        return s;
    if (!Send(CMD_PAGE_BEGIN, nullptr, 0))
        return Step::Fatal;
    for (auto& p : page.chunks) {
        if ((s = WaitFlow(FLOW_DATA_BUSY)) != Step::Ok)
            return s;
        if (!Send(CMD_PAGE_DATA, p.data(), p.size()))
            return Step::Fatal;
    }
    if ((s = WaitFlow(FLOW_CTRL_BUSY)) != Step::Ok)
        return s;
    if (!Send(CMD_PAGE_END, nullptr, 0))
        return Step::Fatal;
    return Step::Ok;
}

bool Engine::Pump(bool drain)
{
    DWORD lastProgress = GetTickCount();
    int lastCompleted = completedTotal_;
    for (;;) {
        if (Cancelled())
            return false;
        if (!RefreshStatus())
            return false;
        if (completedTotal_ != lastCompleted) {
            lastCompleted = completedTotal_;
            lastProgress = GetTickCount();
        }
        if (drain && pending_.empty())
            return true;

        switch (cond_) {
        case Condition::Ready:
        case Condition::Busy:
        case Condition::NotReady:
        case Condition::PaperOut:
            break;
        case Condition::PrintFail:
            // The failed page has already gone through the printer (Canon's status window says
            // "discard the last printed page, then continue"). Reprinting it automatically made a
            // real LBP-810 print the same page over and over, so pages that were imaged count as
            // printed and the job continues with the next page once the engine has stopped.
            restart_ = true;
            lastProgress = GetTickCount();
            if (!(status_.Byte6() & 0x84)) {
                if (!failCounted_) {
                    failCounted_ = true;
                    uint16_t imaged = uint16_t(status_.PagesPushed() - uint16_t(completedTotal_));
                    if (imaged > sent_)
                        imaged = uint16_t(sent_);
                    for (uint16_t i = 0; i < imaged; i++) {
                        pending_.pop_front();
                        sent_--;
                        completedTotal_++;
                        frontAttempts_ = 0;
                        Log("page %d printed with a print failure, not repeated", completedTotal_);
                    }
                }
                if (StartSession() == Step::Fatal)  // E0A2 clears the failure
                    return false;
            }
            if (!Sleep(kIdlePollMs))
                return false;
            continue;
        case Condition::PaperMismatch:
            // Canon's status window offers "force print" here; without it the job continues
            // automatically once the engine has stopped (a few times per job).
            restart_ = true;
            if (!failCounted_) {
                failCounted_ = true;
                autoResumes_++;
                Log("%s, automatic resume %d", ConditionName(cond_), autoResumes_);
            }
            if (autoResumes_ <= 3 && !(status_.Byte6() & 0x84) && StartSession() == Step::Fatal)
                return false;
            lastProgress = GetTickCount();
            if (!Sleep(kIdlePollMs))
                return false;
            continue;
        default:
            // jam, cover open, no cartridge, service error...: wait until the user fixes it
            restart_ = true;
            lastProgress = GetTickCount();
            if (!Sleep(kIdlePollMs))
                return false;
            continue;
        }

        // Paper for the next page first: with an empty tray the engine also reports "not ready",
        // which must not be taken for a reason to set the job up again (CAP1EMN sub_10005350
        // checks the tray before the flow bits).
        bool setUp = started_ && !restart_;
        size_t next = setUp ? sent_ : 0;
        if (next < pending_.size()) {
            const Page& page = pending_[next];
            bool paper = (status_.TrayMask() & (0x80 >> (page.params.source & 7))) != 0 &&
                         !(status_.Word10() & STAT_TRAY_DISABLED);
            if (!paper) {
                // "add paper" only once nothing is being fed any more
                bool feeding = status_.PagesPushed() != status_.PagesPrinted() && (status_.Byte6() & 0x84);
                if (!feeding && !paperWait_) {
                    paperWait_ = true;
                    restart_ = true;
                    UpdateCondition();
                }
                lastProgress = GetTickCount();
                if (!Sleep(kIdlePollMs))
                    return false;
                continue;
            }
            if (paperWait_) {
                paperWait_ = false;
                UpdateCondition();
                continue;  // restart_ is set: E0A4/E0A5 before sending
            }
        }

        bool notReady = (status_.Flow() & FLOW_NOT_READY) != 0;
        if (!setUp || (notReady && !pending_.empty())) {
            // CAP1EMN waits for the engine to stop, then sets the job up again; it keeps doing so
            // while the engine reports "not ready" (e.g. after a paper jam was cleared).
            if (status_.Byte6() & 0x84) {
                if (!Sleep(kBusyPollMs))
                    return false;
                continue;
            }
            Step st = StartSession();
            if (st == Step::Fatal)
                return false;
            if (st == Step::Retry || notReady) {
                if (!Sleep(kIdlePollMs))
                    return false;
            }
            continue;
        }

        if (sent_ < pending_.size()) {
            // Safety net: a page that keeps failing (jams, transfer errors...) ends the job
            // instead of being fed again and again.
            if (sent_ == 0 && frontAttempts_ >= kMaxPageAttempts) {
                Log("page %d sent %d times without being printed, giving up", completedTotal_ + 1,
                    frontAttempts_);
                return false;
            }
            Step st = SendOne(pending_[sent_]);
            if (st == Step::Fatal)
                return false;
            if (st == Step::Retry) {
                Log("page %d interrupted (%s)", completedTotal_ + int(sent_) + 1, ConditionName(cond_));
                restart_ = true;
                continue;
            }
            if (sent_ == 0)
                frontAttempts_++;
            sent_++;
            lastProgress = GetTickCount();
            continue;
        }

        if (!drain)
            return true;
        // Everything sent: wait for the printer to complete the pages.
        if (GetTickCount() - lastProgress > 180000) {
            Log("no progress for 3 minutes, giving up (%d/%d completed)", completedTotal_, queuedTotal_);
            return false;
        }
        if (!Sleep(kBusyPollMs))
            return false;
    }
}

bool Engine::QueuePage(const Page& page)
{
    Page q;
    q.params = page.params;
    q.chunks = PackData(page.chunks);
    size_t total = 0;
    for (auto& p : q.chunks)
        total += p.size();
    q.params.dataSize32k = uint16_t(total >> 15);
    pending_.push_back(std::move(q));
    queuedTotal_++;
    Log("page %d queued: %zu bytes", queuedTotal_, total);
    return Pump(false);
}

void Engine::FinishSession(bool cancel)
{
    std::vector<uint8_t> r;
    if (!session_)
        return;
    if (cancel)
        Command(CMD_CANCEL, nullptr, 0, &r);
    // CAP1EMN sub_10006280: wait (up to 30 s) for the engine to stop before closing the session.
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 30000) {
        if (!RefreshStatus() || !(status_.Byte6() & 0x84))
            break;
        ::Sleep(kBusyPollMs);
    }
    if (cond_ != Condition::Offline)
        Command(CMD_CLOSE_SESSION, nullptr, 0, &r);
    session_ = false;
    started_ = false;
    jobActive_ = false;
    paperWait_ = false;
}

bool Engine::EndJob()
{
    bool ok = Pump(true);
    if (!ok)
        Log("job not finished: %d of %d pages completed", completedTotal_, queuedTotal_);
    FinishSession(!ok);
    return ok;
}

void Engine::AbortJob()
{
    Log("job aborted: %d of %d pages completed", completedTotal_, queuedTotal_);
    FinishSession(true);
}

} // namespace capt
