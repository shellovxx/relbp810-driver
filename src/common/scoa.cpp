#include "scoa.h"
#include <algorithm>
#include <cstring>

namespace scoa {

namespace {

// Port of CAP1K sub_403BF5: number of bytes starting at `pos` that should be sent as literals.
// `line` is the current line, `prev` the previous one (nullptr for the first line of a band).
int CountLiterals(const uint8_t* line, const uint8_t* prev, int pos, int width, int maxCount)
{
    int count = 0;
    while (pos < width && count < maxCount) {
        uint8_t b = line[pos];
        if (b == 0x43)
            break;
        if (count == 0 && pos + 1 < width && b == line[pos + 1])
            break;
        if (pos + 2 < width && b == line[pos + 1] && b == line[pos + 2])
            break;
        if (prev && b == prev[pos]) {
            if (count <= 7)
                break;
            if (pos + 1 == width)
                break;
            uint8_t next = line[pos + 1];
            if (next == prev[pos + 1] || (pos + 2 < width && next == line[pos + 2]))
                break;
        }
        pos++;
        count++;
    }
    return count;
}

class ChunkWriter {
public:
    explicit ChunkWriter(std::vector<std::vector<uint8_t>>& chunks) : chunks_(chunks) { Start(); }
    void Put(uint8_t b) { buf_.push_back(b); }
    void Put(const uint8_t* p, int n) { buf_.insert(buf_.end(), p, p + n); }
    size_t Size() const { return buf_.size(); }
    void Flush()
    {
        if (buf_.empty())
            return;
        if (buf_.size() & 1)
            buf_.push_back(0x40);
        chunks_.push_back(std::move(buf_));
        buf_.clear();
    }
    void Restart()
    {
        Flush();
        Start();
    }
private:
    void Start() { buf_.clear(); buf_.reserve(kChunkFlushThreshold + 1024); buf_.push_back(0x40); }
    std::vector<std::vector<uint8_t>>& chunks_;
    std::vector<uint8_t> buf_;
};

} // namespace

void EncodeBand(const uint8_t* bits, size_t stride, size_t lineBytes, size_t lines,
                std::vector<std::vector<uint8_t>>& chunks)
{
    ChunkWriter out(chunks);
    const int W = static_cast<int>(lineBytes);

    for (size_t li = 0; li < lines; li++) {
        const uint8_t* cur = bits + li * stride;
        const uint8_t* prev = li ? cur - stride : nullptr;
        int pos = 0;

        while (pos < W) {
            int skip = 0;
            if (prev) {
                while (pos < W && cur[pos] == prev[pos]) {
                    pos++;
                    skip++;
                }
                if (pos == W) {
                    out.Put(0x41);
                    break;
                }
                while (skip > 7) {
                    int n = std::min(skip / 8, 31);
                    out.Put(static_cast<uint8_t>(0x80 + n));
                    skip -= 8 * n;
                }
            }

            if (pos + 1 < W && cur[pos] == cur[pos + 1]) {
                // A run of identical bytes.
                const uint8_t b = cur[pos];
                int run = 2;
                int p = pos + 2;
                while (p < W && run < 255 && cur[p] == b) {
                    p++;
                    run++;
                }
                if (b == 0x43) {
                    if (skip)
                        out.Put(static_cast<uint8_t>(0xC0 | skip));
                    if (run > 7) {
                        out.Put(static_cast<uint8_t>(0xA0 | (run >> 3)));
                        out.Put(static_cast<uint8_t>((run & 7) << 3));
                    } else {
                        out.Put(static_cast<uint8_t>(0xC0 | (run << 3)));
                    }
                    pos = p;
                    continue;
                }
                int lit = 0;
                if (!skip)
                    lit = CountLiterals(cur, prev, p, W, run <= 7 ? 255 : 7);
                if (lit == 0) {
                    // skip + run, no literals
                    uint8_t op = static_cast<uint8_t>(0x80 | ((run & 7) << 3) | (skip & 7));
                    if (run > 7) {
                        out.Put(static_cast<uint8_t>(0xA0 | (run >> 3)));
                        out.Put(op);
                    } else {
                        out.Put(static_cast<uint8_t>(op ^ 0xC0));
                    }
                    out.Put(b);
                    pos = p;
                    continue;
                }
                if (lit <= 7) {
                    if (run <= 7) {
                        out.Put(static_cast<uint8_t>(0xC0 | (run << 3) | lit));
                    } else {
                        out.Put(static_cast<uint8_t>(0xA0 | (run >> 3)));
                        out.Put(static_cast<uint8_t>(((run & 7) << 3) | lit));
                    }
                } else {
                    out.Put(static_cast<uint8_t>(0xA0 | (lit >> 3)));
                    out.Put(static_cast<uint8_t>(0x40 | ((run & 7) << 3) | (lit & 7)));
                }
                out.Put(b);
                out.Put(cur + p, lit);
                pos = p + lit;
                continue;
            }

            // No run at pos: literals.
            int lit = CountLiterals(cur, prev, pos, W, 255);
            if (lit == 0) {
                // cur[pos] is a lone 0x43.
                if (skip)
                    out.Put(static_cast<uint8_t>(0xC0 | skip));
                int p = pos + 1;
                int lit2 = CountLiterals(cur, prev, p, W, 255);
                if (lit2 <= 7) {
                    out.Put(static_cast<uint8_t>(0xC8 | lit2));
                } else {
                    out.Put(static_cast<uint8_t>(0xA0 | (lit2 >> 3)));
                    out.Put(static_cast<uint8_t>(0x48 | (lit2 & 7)));
                }
                out.Put(cur + p, lit2);
                pos = p + lit2;
                continue;
            }
            if (lit <= 7) {
                out.Put(static_cast<uint8_t>(((lit & 7) << 3) | (skip & 7)));
            } else {
                out.Put(static_cast<uint8_t>(0xA0 | (lit >> 3)));
                out.Put(static_cast<uint8_t>(0xC0 | ((lit & 7) << 3) | (skip & 7)));
            }
            out.Put(cur + pos, lit);
            pos += lit;
        }

        if (out.Size() >= kChunkFlushThreshold)
            out.Restart();
    }
    out.Flush();
}

bool Decode(const uint8_t* data, size_t size, size_t lineBytes, size_t lines, std::vector<uint8_t>& out)
{
    out.assign(lineBytes * lines, 0);
    std::vector<uint8_t> line(lineBytes, 0);
    size_t li = 0, x = 0, i = 0;
    auto need = [&](size_t n) { return i + n <= size; };
    auto fill = [&](uint8_t v, size_t n) {
        if (x + n > lineBytes) return false;
        memset(&line[x], v, n); x += n; return true;
    };
    auto copy = [&](size_t n) {
        if (x + n > lineBytes || !need(n)) return false;
        memcpy(&line[x], data + i, n); x += n; i += n; return true;
    };
    auto skipb = [&](size_t n) { if (x + n > lineBytes) return false; x += n; return true; };

    while (i < size && li < lines) {
        uint8_t b = data[i++];
        if (b == 0x40)
            continue;
        if (b == 0x42)
            break;
        if (b == 0x41) {
            x = lineBytes;
        } else if ((b & 0xE0) == 0x80) {
            if (!skipb(8 * (b & 0x1F))) return false;
        } else {
            size_t hi = 0;
            if ((b & 0xE0) == 0xA0) {
                hi = 8 * (b & 0x1F);
                if (!need(1)) return false;
                b = data[i++] ^ 0xC0;
            }
            size_t r = (b >> 3) & 7, l = b & 7;
            switch (b & 0xC0) {
            case 0x40: // skip l, run (hi+r) of X
                if (!need(1) || !skipb(l)) return false;
                { uint8_t v = data[i++]; if (!fill(v, hi + r)) return false; }
                break;
            case 0x80: // run r of X (or one 0x43 when hi && r == 1), then hi+l literals
                if (hi && r == 1) {
                    if (!fill(0x43, 1)) return false;
                } else {
                    if (!need(1)) return false;
                    uint8_t v = data[i++];
                    if (!fill(v, r)) return false;
                }
                if (!copy(hi + l)) return false;
                break;
            case 0xC0: {
                size_t run = hi + r;
                if (!hi && run == 0) {
                    if (!skipb(l)) return false;
                } else if (!hi && run == 1 && l) {
                    if (!fill(0x43, 1) || !copy(l)) return false;
                } else if (l) {
                    if (!need(1)) return false;
                    uint8_t v = data[i++];
                    if (!fill(v, run) || !copy(l)) return false;
                } else {
                    if (!fill(0x43, run)) return false;
                }
                break;
            }
            default: // 0x00: skip l, then hi+r literals
                if (!skipb(l) || !copy(hi + r)) return false;
                break;
            }
        }
        if (x >= lineBytes) {
            memcpy(&out[li * lineBytes], line.data(), lineBytes);
            li++;
            x = 0;
        }
    }
    return true;
}

} // namespace scoa
