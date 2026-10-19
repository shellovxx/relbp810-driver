// Spool stream between the rendering plug-in and the language monitor.
//
// The format is the one Canon's CAP1K.DLL writes and CAPMONK.DLL/CAP1EMN.DLL consume:
// a sequence of packets [u16 cmd][u16 size incl. header][payload]:
//   0x0001  job header, 24 bytes: magic 0xFADEDACE, copies, collate, page order mode, flags
//   0x0002  document info, 100 bytes (paper id, form name, ...)
//   0xD0A0  page parameters (34-byte payload, see capt::PageParams)
//   0xD0A1  page begin
//   0xC0A0  SCoA data chunk(s); the last one is "@B"
//   0xD0A2  page end
//   0x0004  end of page marker, 6 bytes
//   0x0003  end of job
#pragma once
#include "capt.h"
#include <cstdint>
#include <vector>

namespace spool {

enum : uint16_t {
    PKT_JOB_HEADER = 0x0001,
    PKT_DOC_INFO   = 0x0002,
    PKT_JOB_END    = 0x0003,
    PKT_PAGE_END   = 0x0004,
};

constexpr uint32_t kJobMagic = 0xFADEDACE;

// Job header flags (CAP1K sub_402C2D).
enum : uint32_t {
    JOBF_BIT0       = 0x01,
    JOBF_PAGE_FLAG  = 0x02,   // CAP1EMN sets D0A0 byte 5 bit 0
    JOBF_WAIT_PAGES = 0x04,
    JOBF_BIT3       = 0x08,
    JOBF_BIT4       = 0x10,
};

#pragma pack(push, 1)
struct JobHeader {
    uint16_t cmd = PKT_JOB_HEADER;
    uint16_t size = sizeof(JobHeader);
    uint32_t magic = kJobMagic;
    uint32_t copies = 1;
    uint32_t collate = 0;
    uint32_t pageOrder = 1;   // 2/3 = reorder pages through temp files (CAPMONK)
    uint32_t flags = JOBF_PAGE_FLAG;
};
static_assert(sizeof(JobHeader) == 24, "job header");

struct DocInfo {
    uint16_t cmd = PKT_DOC_INFO;
    uint16_t size = sizeof(DocInfo);
    int32_t paperId = 9;       // dmPaperSize (570 = index card, custom forms 580..582)
    wchar_t formName[32] = {};
    uint32_t codePage = 0;     // non-zero: formName holds ANSI text in this code page
    uint32_t unknown76 = 0;
    uint32_t unknown80 = 0;
    uint32_t copies = 1;       // rewritten by the language monitor
    uint32_t unknown88 = 0;
    uint32_t unknown92 = 0;
    uint32_t blankPage = 0;    // 1..3 = synthetic blank page (manual duplex padding)
};
static_assert(sizeof(DocInfo) == 100, "doc info");

struct PageEnd {
    uint16_t cmd = PKT_PAGE_END;
    uint16_t size = 6;
    uint8_t flag = 0;
    uint8_t pad = 0;
};
#pragma pack(pop)

// Appends a packet with the given payload.
void Put(std::vector<uint8_t>& out, uint16_t cmd, const void* payload, size_t n);

// Encodes a whole page (1bpp, 1 = black) into D0A0, D0A1, C0A0..., D0A2, 0x0004 packets.
void PutPage(std::vector<uint8_t>& out, const capt::PageParams& pp,
             const uint8_t* bits, size_t stride, size_t lines);

// Incremental parser for the spool stream.
class Parser {
public:
    // Feeds bytes; calls onPacket(ptr, size) for each complete packet (ptr includes the header).
    template <class F> void Feed(const uint8_t* p, size_t n, F&& onPacket)
    {
        while (n) {
            if (need_ == 0) {
                // collecting header
                hdr_[have_++] = *p++;
                n--;
                if (have_ == 2 && hdr_[0] == 0 && hdr_[1] == 0) {
                    have_ = 0;  // zero padding words are skipped like CAP1EMN does
                    continue;
                }
                if (have_ == 4) {
                    size_t sz = hdr_[2] | (hdr_[3] << 8);
                    if (sz < 4) {
                        bad_ = true;
                        have_ = 0;
                        continue;
                    }
                    pkt_.assign(hdr_, hdr_ + 4);
                    need_ = sz - 4;
                    have_ = 0;
                    if (need_ == 0) {
                        onPacket(pkt_.data(), pkt_.size());
                    }
                }
                continue;
            }
            size_t take = n < need_ ? n : need_;
            pkt_.insert(pkt_.end(), p, p + take);
            p += take;
            n -= take;
            need_ -= take;
            if (need_ == 0)
                onPacket(pkt_.data(), pkt_.size());
        }
    }
    bool Bad() const { return bad_; }
    void Reset() { have_ = 0; need_ = 0; bad_ = false; pkt_.clear(); }
private:
    uint8_t hdr_[4] = {};
    size_t have_ = 0;
    size_t need_ = 0;
    bool bad_ = false;
    std::vector<uint8_t> pkt_;
};

} // namespace spool
