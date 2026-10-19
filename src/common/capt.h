// Canon CAPT 1.x protocol definitions for the LBP-810 (reconstructed from CAP1EMN.DLL / CAP1K.DLL).
//
// Every packet is: u16 command (LE), u16 total size incl. header (LE), payload.
// Replies echo the command; a reply of more than 6 bytes arrives as 6 bytes + rest.
#pragma once
#include <cstdint>
#include <cstddef>

namespace capt {

// Host <-> printer commands (word values; on the wire the low byte goes first).
enum : uint16_t {
    CMD_STATUS        = 0xA0A0,  // basic status, 22-byte reply
    CMD_TRAY_INFO     = 0xA0A1,  // paper source info, 16 bytes per source
    CMD_DEVICE_ID     = 0xA1A0,  // IEEE-1284 id (LPT only)
    CMD_CAPABILITIES  = 0xA1A1,  // 20-byte reply, bytes 6-7 = model code (420 for LBP-810)
    CMD_OPEN_SESSION  = 0xA2A0,  // 6-byte reply
    CMD_ENGINE_INFO   = 0xA2A1,  // 8-byte reply
    CMD_EXT_ERROR     = 0xA2A2,  // 8-byte reply, bytes 6-7 = extended error bits
    CMD_JOB_INFO_NET  = 0xA3A0,  // network only
    CMD_FLOW_STATUS   = 0xE0A0,  // 6-byte reply, byte 4 = flow bits, byte 5 bit0 = query CMD_STATUS
    CMD_CLEAR_ERROR   = 0xE0A2,
    CMD_CLEAR_ERROR2  = 0xE0A3,
    CMD_JOB_START     = 0xE0A4,
    CMD_JOB_SETUP     = 0xE0A5,  // payload: EE DB EA AD, u16 first page number, 00 00
    CMD_CANCEL        = 0xE0A6,
    CMD_PAGE_SYNC     = 0xE0A7,
    CMD_CLOSE_SESSION = 0xE0A9,
    CMD_CLEANING      = 0xE0AD,  // payload: EE DB EA AD, 3 bytes
    CMD_JOB_INFO      = 0xE1A0,  // user/computer/document names (network only)
    CMD_PAGE_DATA     = 0xC0A0,  // compressed raster (SCoA)
    CMD_PAGE_PARAMS   = 0xD0A0,  // 34-byte payload
    CMD_PAGE_BEGIN    = 0xD0A1,
    CMD_PAGE_END      = 0xD0A2,
};

constexpr uint32_t kMagic = 0xADEADBEE;          // EE DB EA AD
constexpr uint16_t kModelCodeLbp810 = 420;      // D0A0 bytes 2-3, also A1A1 reply bytes 6-7
constexpr size_t kMaxDataPacket = 0x7F00;       // CAP1EMN repacks C0A0 data up to this size

// Bits of the "flow" byte (status byte 4: E0A0 reply byte 4 / A0A0 reply byte 4).
enum : uint8_t {
    FLOW_CTRL_BUSY  = 0x04,   // do not send D0xx packets
    FLOW_DATA_BUSY  = 0x08,   // printer buffer full, do not send C0A0
    FLOW_NOT_READY  = 0x30,   // engine initializing / not ready
    FLOW_ONLINE     = 0x40,
    FLOW_ERROR      = 0x80,   // reply byte 4 is an error code
};

// Bits of status word 10-11 (A0A0 reply). Mapping taken from CAP1EMN sub_10004130.
enum : uint16_t {
    STAT_CLEANING      = 0x0004,  // -> idle screen 206 "Cleaning"
    STAT_ENGINE_INFO   = 0x00C0,  // query A2A1 for details
    STAT_EXT_ERROR     = 0x0002,  // query A2A2 for details
    STAT_PAPER_JAM     = 0x0100,  // -> screen 402 "Clear the paper jam"
    STAT_TRAY_DISABLED = 0x0200,  // treated as "no paper"
    STAT_NO_CARTRIDGE  = 0x2000,  // -> screen 414/508 "Check the toner cartridge"
    STAT_COVER_OPEN    = 0x4000,  // -> screen 405/503 "Close the printer cover"
};

// Paper size codes used in D0A0 byte 4 (CAP1EMN table at 0x1000E4C8, keyed by DEVMODE dmPaperSize).
struct PaperCode { int16_t dmPaper; uint8_t code; };
constexpr PaperCode kPaperCodes[] = {
    { 1, 0x0D },   // Letter
    { 5, 0x0C },   // Legal
    { 7, 0x0A },   // Executive
    { 9, 0x02 },   // A4
    { 11, 0x03 },  // A5
    { 13, 0x07 },  // B5 (JIS)
    { 20, 0x16 },  // Envelope #10
    { 27, 0x18 },  // Envelope DL
    { 28, 0x15 },  // Envelope C5
    { 37, 0x17 },  // Envelope Monarch
    { 43, 0x0E },  // Postcard
    { 570, 0x40 }, // Index card 3x5
    { 91, 0x1A },  // Youkei 4
    { 31, 0x1C },  // Youkei 2
};
constexpr uint8_t kPaperCodeCustom = 0x13;

inline uint8_t PaperCodeFor(int dmPaper)
{
    for (auto& p : kPaperCodes)
        if (p.dmPaper == dmPaper)
            return p.code;
    return kPaperCodeCustom;
}

// D0A0 page parameters, 34-byte payload (CAP1K sub_402D57 + CAP1EMN sub_1000BB50).
#pragma pack(push, 1)
struct PageParams {
    uint16_t dataSize32k;   // 0: total compressed bytes >> 15 (filled by the engine)
    uint16_t modelCode;     // 2: 420
    uint8_t  paperCode;     // 4
    uint8_t  flags;         // 5: bit0 = job header flag 2
    uint8_t  source;        // 6: paper source index (0 = tray)
    uint8_t  reserved7;     // 7
    uint8_t  density;       // 8: toner density
    uint8_t  density2[3];   // 9..11: 0x1F
    uint8_t  mediaType;     // 12
    uint8_t  resolution;    // 13: 0x11 = 600 dpi, 0x00 = 300 dpi
    uint8_t  const14[4];    // 14..17: 03 01 01 01
    uint8_t  refinement;    // 18: 2 = automatic image refinement on
    uint8_t  tonerSave;     // 19
    uint8_t  syncPage;      // 20: 1 = page sync (network)
    uint8_t  reserved21;    // 21
    uint16_t marginLeft;    // 22: left offset of the image (dots)
    uint16_t marginTop;     // 24: top offset of the image (lines)
    uint16_t lineBytes;     // 26
    uint16_t lines;         // 28
    uint16_t paperWidth;    // 30: dots
    uint16_t paperHeight;   // 32: dots
};
#pragma pack(pop)
static_assert(sizeof(PageParams) == 34, "D0A0 payload must be 34 bytes");

// Decoded basic status (A0A0 reply, 22 bytes; bytes 0-3 are the packet header).
struct Status {
    uint8_t raw[22] = {};
    uint8_t Flow() const { return raw[4]; }
    uint8_t Byte5() const { return raw[5]; }
    uint8_t Byte6() const { return raw[6]; }
    uint8_t Byte7() const { return raw[7]; }
    uint8_t TrayMask() const { return raw[8]; }       // bit (0x80 >> source) = paper present
    uint16_t Word10() const { return uint16_t(raw[10] | (raw[11] << 8)); }
    uint16_t PagesDecoded() const { return uint16_t(raw[12] | (raw[13] << 8)); }
    uint16_t PagesPrinted() const { return uint16_t(raw[14] | (raw[15] << 8)); }
    uint16_t PagesPushed() const { return uint16_t(raw[16] | (raw[17] << 8)); }
    uint16_t PagesCompleted() const { return uint16_t(raw[18] | (raw[19] << 8)); }
};

} // namespace capt
