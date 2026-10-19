// Development check (x86 only): compares our SCoA encoder with the original Canon encoder
// inside CAP1K.DLL (sub_403852) and decodes both with our decoder and with the original
// decoder inside CAP1EMN.DLL (sub_10007340).
//
// Build as 32-bit with /BASE:0x01000000 /DYNAMICBASE:NO so that CAP1K.DLL can load at 0x400000.
// usage: verify_scoa <dir with CAP1K.DLL and CAP1EMN.DLL>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <random>
#include "../../common/scoa.h"

typedef int(__stdcall* EncodeFn)(void* ctx, const uint8_t* bmp, uint8_t* tmp, int lineBytes, int lines);
// sub_10007340(dst, lineBytes, packetsBegin, packetsEnd, firstByte, endByte, firstLine, lines)
typedef int(__stdcall* DecodeFn)(uint8_t* dst, int lineBytes, const void* pk, const void* pkEnd,
                                 int x0, int x1, int line0, int lines);

static std::vector<uint8_t> CanonEncode(EncodeFn fn, const std::vector<uint8_t>& bmp, int lb, int lines)
{
    std::vector<uint8_t> ctx(0x20000, 0);
    std::vector<uint8_t> out(bmp.size() * 3 + 65536);
    std::vector<uint8_t> tmp(4 * 1024 * 1024);
    *reinterpret_cast<uint32_t*>(&ctx[44198]) = static_cast<uint32_t>(out.size());
    *reinterpret_cast<uint32_t*>(&ctx[44202]) = reinterpret_cast<uint32_t>(out.data());
    *reinterpret_cast<uint32_t*>(&ctx[44206]) = 0;
    fn(ctx.data(), bmp.data(), tmp.data(), lb, lines);
    out.resize(*reinterpret_cast<uint32_t*>(&ctx[44206]));
    return out;
}

static std::vector<uint8_t> OurEncode(const std::vector<uint8_t>& bmp, int lb, int lines)
{
    std::vector<std::vector<uint8_t>> chunks;
    scoa::EncodeBand(bmp.data(), lb, lb, lines, chunks);
    std::vector<uint8_t> out;
    for (auto& c : chunks) {
        uint16_t sz = static_cast<uint16_t>(c.size() + 4);
        out.push_back(0xA0); out.push_back(0xC0);
        out.push_back(static_cast<uint8_t>(sz)); out.push_back(static_cast<uint8_t>(sz >> 8));
        out.insert(out.end(), c.begin(), c.end());
    }
    return out;
}

static std::vector<uint8_t> MakeImage(int kind, int lb, int lines, std::mt19937& rng)
{
    std::vector<uint8_t> b(static_cast<size_t>(lb) * lines, 0);
    std::uniform_int_distribution<int> d(0, 255);
    for (int y = 0; y < lines; y++) {
        uint8_t* l = &b[static_cast<size_t>(y) * lb];
        switch (kind) {
        case 0: break;                                                     // blank
        case 1: for (int x = 0; x < lb; x++) l[x] = static_cast<uint8_t>(d(rng)); break; // noise
        case 2: for (int x = 0; x < lb; x++) l[x] = ((x / 7 + y / 5) & 1) ? 0xFF : 0x00; break;
        case 3: for (int x = 0; x < lb; x++) l[x] = (y & 1) ? 0xAA : 0x55; break; // dither
        case 4: for (int x = 0; x < lb; x++) l[x] = (d(rng) < 40) ? 0x43 : ((d(rng) < 60) ? static_cast<uint8_t>(d(rng)) : 0); break;
        case 5: // mostly identical lines with sparse changes
            if (y == 0) { for (int x = 0; x < lb; x++) l[x] = static_cast<uint8_t>(d(rng) & 0x81); }
            else { memcpy(l, l - lb, lb); for (int k = 0; k < 3; k++) l[d(rng) % lb] ^= static_cast<uint8_t>(d(rng)); }
            break;
        case 6: // runs of varying length incl. long ones and 0x43
            for (int x = 0; x < lb;) {
                int len = 1 + d(rng) % 300; uint8_t v = (d(rng) & 1) ? 0x43 : static_cast<uint8_t>(d(rng) & 0xF0);
                for (int k = 0; k < len && x < lb; k++) l[x++] = v;
            }
            break;
        default: // text-like: random rectangles copied over several lines
            if (y % 24 < 16) for (int x = 0; x < lb; x++) l[x] = ((x * 13 + (y % 24) * 7) % 11 < 3) ? 0x3C : ((x % 9) ? 0 : 0x81);
            break;
        }
    }
    return b;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: verify_scoa <dir>\n"); return 2; }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\CAP1K.DLL", argv[1]);
    HMODULE k = LoadLibraryA(path);
    snprintf(path, sizeof(path), "%s\\CAP1EMN.DLL", argv[1]);
    HMODULE e = LoadLibraryA(path);
    if (!k || !e) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto enc = reinterpret_cast<EncodeFn>(reinterpret_cast<uint8_t*>(k) + 0x3852);
    auto dec = reinterpret_cast<DecodeFn>(reinterpret_cast<uint8_t*>(e) + 0x7340);
    printf("CAP1K at %p, CAP1EMN at %p\n", k, e);

    std::mt19937 rng(12345);
    int fails = 0, total = 0;
    const int widths[] = { 8, 37, 255, 256, 300, 592, 600 };
    for (int kind = 0; kind <= 7; kind++) {
        for (int lb : widths) {
            for (int lines : { 1, 2, 17, 130 }) {
                auto img = MakeImage(kind, lb, lines, rng);
                auto a = CanonEncode(enc, img, lb, lines);
                auto b = OurEncode(img, lb, lines);
                total++;
                bool same = a == b;
                // decode ours with our decoder
                std::vector<uint8_t> payload;
                for (size_t i = 0; i + 4 <= b.size();) {
                    size_t sz = b[i + 2] | (b[i + 3] << 8);
                    payload.insert(payload.end(), b.begin() + i + 4, b.begin() + i + sz);
                    i += sz;
                }
                std::vector<uint8_t> d1;
                bool okd = scoa::Decode(payload.data(), payload.size(), lb, lines, d1) && d1 == img;
                // decode Canon's stream (+ "@B") with Canon's decoder
                std::vector<uint8_t> pk = a;
                pk.insert(pk.end(), { 0xA0, 0xC0, 0x06, 0x00, 0x40, 0x42 });
                std::vector<uint8_t> d2(img.size() + 64, 0);
                int r = dec(d2.data(), lb, pk.data(), pk.data() + pk.size(), 0, lb, 0, lines);
                d2.resize(img.size());
                bool okc = r && d2 == img;
                if (!same || !okd || !okc) {
                    fails++;
                    printf("kind=%d lb=%d lines=%d: same=%d (canon %zu, ours %zu) ourdec=%d canondec=%d\n",
                           kind, lb, lines, same, a.size(), b.size(), okd, okc);
                    if (!same) {
                        size_t n = min(a.size(), b.size()), i = 0;
                        while (i < n && a[i] == b[i]) i++;
                        printf("  first diff at %zu\n", i);
                    }
                }
            }
        }
    }
    printf("%d/%d cases matched\n", total - fails, total);
    return fails ? 1 : 0;
}
