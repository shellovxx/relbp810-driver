// SCoA ("Smart Compression Architecture") codec used by Canon CAPT 1.x printers (LBP-810/LBP-1120).
//
// Reconstructed from the original Canon driver:
//   encoder  - CAP1K.DLL  sub_403852 / sub_403BF5 (band compressor)
//   decoder  - CAP1EMN.DLL sub_10007340           (used there to rotate pages)
//
// Stream grammar (one opcode byte, optionally followed by data). "skip s" means
// "keep s bytes of the previous line" (the decoder keeps one line buffer).
//   0x40                 padding / band start (no-op)
//   0x41                 end of line, rest of the line equals the previous line
//   0x42                 end of page
//   100nnnnn             skip 8*n bytes                              (n = 1..31)
//   00lllsss  L...       skip s, then l literal bytes                (l = 1..7)
//   01rrrsss  X          skip s, then X repeated r times             (r = 2..7)
//   11000sss             skip s                                      (s = 1..7)
//   11001lll  L...       one 0x43 byte, then l literal bytes
//   11rrr000             0x43 repeated r times                       (r = 2..7)
//   11rrrlll  X L...     X repeated r times, then l literal bytes    (r = 2..7, l = 1..7)
//   101HHHHH + second byte b2 (H = high 5 bits of an 8-bit count, b2 low bits complete it):
//     00rrrlll X L...    X repeated H*8+r times, then l literals   (l = 0: 0x43 run, no X)
//     01rrrlll X L...    X repeated r times, then H*8+l literals   (r = 1: one 0x43, no X)
//     10rrrsss X         skip s, then X repeated H*8+r times
//     11lllsss L...      skip s, then H*8+l literal bytes
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace scoa {

// Maximum size of one compressed chunk before CAP1K flushes it into a C0A0 packet.
constexpr size_t kChunkFlushThreshold = 31456;

// Compresses one band of a 1bpp bitmap (bit set = black, MSB = leftmost pixel).
// Each emitted chunk starts with 0x40 and has an even length (padded with 0x40),
// exactly like CAP1K. The first line of a band never references the previous line.
// Chunks are appended to `chunks`.
void EncodeBand(const uint8_t* bits, size_t stride, size_t lineBytes, size_t lines,
                std::vector<std::vector<uint8_t>>& chunks);

// The end-of-page chunk CAP1K sends after the last band ("@B").
inline std::vector<uint8_t> EndOfPageChunk() { return { 0x40, 0x42 }; }

// Decodes a concatenation of chunk payloads into `lines` lines of `lineBytes` each.
// Returns false on malformed input. Stops at 0x42 or when all lines are decoded.
bool Decode(const uint8_t* data, size_t size, size_t lineBytes, size_t lines, std::vector<uint8_t>& out);

} // namespace scoa
