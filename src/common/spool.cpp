#include "spool.h"
#include "scoa.h"
#include <cstring>

namespace spool {

void Put(std::vector<uint8_t>& out, uint16_t cmd, const void* payload, size_t n)
{
    size_t sz = n + 4;
    out.push_back(uint8_t(cmd));
    out.push_back(uint8_t(cmd >> 8));
    out.push_back(uint8_t(sz));
    out.push_back(uint8_t(sz >> 8));
    if (n)
        out.insert(out.end(), static_cast<const uint8_t*>(payload), static_cast<const uint8_t*>(payload) + n);
}

void PutPage(std::vector<uint8_t>& out, const capt::PageParams& pp,
             const uint8_t* bits, size_t stride, size_t lines)
{
    Put(out, capt::CMD_PAGE_PARAMS, &pp, sizeof(pp));
    Put(out, capt::CMD_PAGE_BEGIN, nullptr, 0);
    std::vector<std::vector<uint8_t>> chunks;
    scoa::EncodeBand(bits, stride, pp.lineBytes, lines, chunks);
    chunks.push_back(scoa::EndOfPageChunk());
    for (auto& c : chunks)
        Put(out, capt::CMD_PAGE_DATA, c.data(), c.size());
    Put(out, capt::CMD_PAGE_END, nullptr, 0);
    PageEnd pe;
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&pe), reinterpret_cast<uint8_t*>(&pe) + sizeof(pe));
}

} // namespace spool
