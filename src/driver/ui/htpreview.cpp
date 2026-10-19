// Port of the Quality page sample renderer of CAP1UIK.DLL (see htpreview.h). The names of the
// original functions are given for each part; the scratch buffer keeps the original layout
// because the dither and diffusion loops read past the image width into neighbouring data.
#include "htpreview.h"
#include <cstring>

namespace ui {
namespace {

// Ordered dither matrices (8x8) of "Pattern 1" and "Pattern 2".
const uint8_t kPattern1[64] = {
    23, 63, 207, 231, 251, 191, 87, 31,
    71, 119, 135, 167, 199, 159, 127, 79,
    183, 151, 103, 39, 47, 111, 143, 215,
    247, 223, 95, 3, 15, 55, 175, 239,
    255, 187, 83, 27, 19, 59, 203, 227,
    195, 155, 123, 75, 67, 115, 131, 163,
    43, 107, 139, 211, 179, 147, 99, 35,
    11, 51, 171, 235, 243, 219, 91, 7,
};
const uint8_t kPattern2[64] = {
    243, 211, 179, 131, 147, 195, 227, 255,
    231, 163, 99, 67, 83, 115, 175, 223,
    199, 119, 51, 19, 35, 63, 111, 191,
    151, 87, 39, 3, 15, 31, 79, 143,
    135, 71, 23, 7, 11, 47, 95, 159,
    183, 103, 55, 43, 27, 59, 127, 207,
    215, 167, 123, 91, 75, 107, 171, 239,
    247, 235, 203, 155, 139, 187, 219, 251,
};
// "Smoothing": 200 sets of error weights (behind-below, ahead-below, below, next), sum 16.
const uint8_t kWeights[800] = {
    2, 2, 2, 10, 3, 1, 2, 10, 3, 1, 5, 7, 4, 0, 3, 9, 2, 2, 3, 9, 2, 2, 8, 4, 4, 0, 2, 10, 2, 2, 8, 4,
    2, 2, 2, 10, 4, 0, 4, 8, 4, 0, 6, 6, 4, 0, 5, 7, 3, 1, 3, 9, 2, 2, 3, 9, 2, 2, 2, 10, 4, 0, 7, 5,
    4, 0, 7, 5, 3, 1, 8, 4, 3, 1, 3, 9, 4, 0, 4, 8, 4, 0, 5, 7, 4, 0, 4, 8, 2, 2, 3, 9, 3, 1, 8, 4,
    3, 1, 7, 5, 2, 2, 4, 8, 4, 0, 8, 4, 3, 1, 5, 7, 4, 0, 7, 5, 3, 1, 3, 9, 2, 2, 5, 7, 4, 0, 8, 4,
    3, 1, 7, 5, 2, 2, 5, 7, 3, 1, 8, 4, 4, 0, 2, 10, 4, 0, 8, 4, 2, 2, 5, 7, 4, 0, 3, 9, 2, 2, 3, 9,
    2, 2, 7, 5, 3, 1, 4, 8, 2, 2, 5, 7, 2, 2, 5, 7, 3, 1, 7, 5, 4, 0, 4, 8, 3, 1, 8, 4, 3, 1, 8, 4,
    4, 0, 4, 8, 2, 2, 4, 8, 2, 2, 5, 7, 4, 0, 3, 9, 4, 0, 5, 7, 2, 2, 3, 9, 4, 0, 3, 9, 2, 2, 6, 6,
    3, 1, 6, 6, 4, 0, 3, 9, 4, 0, 6, 6, 4, 0, 7, 5, 4, 0, 4, 8, 4, 0, 3, 9, 2, 2, 4, 8, 3, 1, 6, 6,
    3, 1, 2, 10, 3, 1, 7, 5, 3, 1, 7, 5, 3, 1, 7, 5, 2, 2, 2, 10, 4, 0, 5, 7, 2, 2, 7, 5, 4, 0, 3, 9,
    2, 2, 8, 4, 2, 2, 6, 6, 4, 0, 7, 5, 3, 1, 5, 7, 3, 1, 5, 7, 2, 2, 3, 9, 2, 2, 4, 8, 2, 2, 6, 6,
    2, 2, 7, 5, 2, 2, 6, 6, 4, 0, 6, 6, 4, 0, 7, 5, 2, 2, 4, 8, 2, 2, 7, 5, 3, 1, 7, 5, 4, 0, 4, 8,
    4, 0, 2, 10, 3, 1, 3, 9, 4, 0, 8, 4, 3, 1, 4, 8, 2, 2, 8, 4, 2, 2, 3, 9, 2, 2, 4, 8, 3, 1, 4, 8,
    3, 1, 7, 5, 4, 0, 2, 10, 2, 2, 6, 6, 3, 1, 8, 4, 2, 2, 7, 5, 4, 0, 8, 4, 2, 2, 2, 10, 2, 2, 2, 10,
    4, 0, 2, 10, 3, 1, 7, 5, 2, 2, 2, 10, 2, 2, 6, 6, 3, 1, 5, 7, 4, 0, 2, 10, 4, 0, 5, 7, 2, 2, 3, 9,
    4, 0, 8, 4, 4, 0, 4, 8, 2, 2, 8, 4, 2, 2, 8, 4, 2, 2, 5, 7, 3, 1, 6, 6, 4, 0, 4, 8, 3, 1, 6, 6,
    2, 2, 4, 8, 2, 2, 4, 8, 2, 2, 2, 10, 3, 1, 3, 9, 4, 0, 4, 8, 4, 0, 4, 8, 4, 0, 5, 7, 2, 2, 8, 4,
    2, 2, 8, 4, 2, 2, 5, 7, 4, 0, 8, 4, 4, 0, 6, 6, 4, 0, 2, 10, 4, 0, 4, 8, 2, 2, 4, 8, 4, 0, 7, 5,
    2, 2, 4, 8, 2, 2, 8, 4, 2, 2, 5, 7, 3, 1, 5, 7, 4, 0, 4, 8, 4, 0, 5, 7, 3, 1, 6, 6, 3, 1, 4, 8,
    3, 1, 4, 8, 3, 1, 3, 9, 4, 0, 7, 5, 2, 2, 4, 8, 4, 0, 4, 8, 2, 2, 5, 7, 4, 0, 3, 9, 4, 0, 6, 6,
    3, 1, 3, 9, 3, 1, 4, 8, 2, 2, 8, 4, 4, 0, 5, 7, 4, 0, 4, 8, 4, 0, 4, 8, 3, 1, 4, 8, 3, 1, 2, 10,
    3, 1, 6, 6, 3, 1, 8, 4, 2, 2, 4, 8, 3, 1, 8, 4, 3, 1, 2, 10, 3, 1, 6, 6, 4, 0, 6, 6, 3, 1, 3, 9,
    3, 1, 3, 9, 2, 2, 4, 8, 3, 1, 7, 5, 3, 1, 8, 4, 2, 2, 6, 6, 2, 2, 3, 9, 4, 0, 7, 5, 3, 1, 2, 10,
    3, 1, 8, 4, 3, 1, 3, 9, 2, 2, 3, 9, 3, 1, 5, 7, 2, 2, 3, 9, 2, 2, 7, 5, 2, 2, 4, 8, 4, 0, 8, 4,
    2, 2, 3, 9, 2, 2, 6, 6, 4, 0, 2, 10, 4, 0, 3, 9, 2, 2, 7, 5, 2, 2, 3, 9, 2, 2, 2, 10, 2, 2, 5, 7,
    3, 1, 5, 7, 3, 1, 5, 7, 4, 0, 4, 8, 2, 2, 4, 8, 2, 2, 6, 6, 4, 0, 6, 6, 4, 0, 7, 5, 2, 2, 4, 8,
};

struct Ctx {
    int bytes = 0;                       // output bytes per row (WORD aligned)
    int width = 0;
    const uint8_t* r = nullptr;          // RGB -> gray tables
    const uint8_t* g = nullptr;
    const uint8_t* b = nullptr;
    const uint8_t* matrix = nullptr;     // dither matrix row of the current line
    const uint8_t* weights = nullptr;
    uint8_t* temp = nullptr;             // diffusion: corrected gray line
    const uint8_t* identity = nullptr;
    uint8_t* err = nullptr;              // diffusion: 16-bit errors for the next line
    uint8_t edge[4] = {};                // sub_6A915825 masks for the first/last byte
};

// sub_6A915782: gray tables with the brightness applied (0..256, 128 = neutral).
void GrayTables(uint8_t* t, int brightness)
{
    for (int i = 0; i < 256; i++) {
        int v = 2 * brightness - 256 + i;
        uint8_t c = uint8_t(v > 255 ? 255 : (v < 0 ? 0 : v));
        t[i] = uint8_t(uint16_t(30 * c) / 100);
        if (c == 0xFF)
            t[i]++;
        t[256 + i] = uint8_t(uint16_t(59 * c) / 100);
        t[512 + i] = uint8_t(uint16_t(11 * c) / 100);
    }
}

// sub_6A915825
void EdgeMasks(Ctx& c, int lead, int pad)
{
    uint8_t m = 0;
    for (int n = 8 - lead; n > 0; n--)
        m = uint8_t((m << 1) | 1);
    c.edge[0] = m;
    m = 0;
    for (int n = 8 - pad; n > 0; n--)
        m = uint8_t((m >> 1) | 0x80);
    c.edge[1] = m;
    m = 0;
    for (int n = lead; n > 0; n--)
        m = uint8_t((m >> 1) | 0x80);
    c.edge[2] = m;
    m = 0;
    for (int n = pad; n > 0; n--)
        m = uint8_t((m << 1) | 1);
    c.edge[3] = m;
}

// sub_6A91590C
void ContrastCurve(int contrast, uint8_t pivot, int k, uint8_t* t)
{
    for (int i = 0; i < 256; i++) {
        if (contrast == 100) {
            if (pivot != 0xFF || t[i] != 0xFF)
                t[i] = t[i] <= pivot ? 0 : 0xFF;
            continue;
        }
        int d = t[i] - pivot;
        uint8_t v;
        if (d <= 0) {
            int r = MulDiv(pivot - t[i], k, 50);
            v = uint8_t(pivot - r);
            if (pivot - r < 0)
                v = 0;
        } else {
            int r = MulDiv(d, k, 50);
            v = uint8_t(pivot + r);
            if (pivot + r > 255)
                v = 0xFF;
        }
        t[i] = v;
    }
}

// sub_6A915893
void ContrastTable(uint8_t* t, int contrast, int brightness)
{
    int p = 2 * brightness - 128;
    uint8_t pivot = uint8_t(p > 255 ? 255 : (p < 0 ? 0 : p));
    int k = 50;
    if (contrast == 100)
        pivot = brightness ? uint8_t(brightness - 1) : 0;
    else if (contrast <= 50)
        k = MulDiv(contrast, 50, 50);
    else
        k = MulDiv(50, 50, 100 - contrast);
    for (int i = 0; i < 256; i++)
        t[i] = uint8_t(i);
    ContrastCurve(contrast, pivot, k, t);
}

// sub_6A91599C (24 bpp case)
void ToGray(uint8_t* gray, const uint8_t* bgr, const Ctx& c)
{
    for (int i = 0; i < c.width; i++, bgr += 3)
        gray[i] = uint8_t(c.g[bgr[1]] + c.b[bgr[0]] + c.r[bgr[2]]);
}

// sub_6A915A95: ordered dither, bit 1 = black.
void Dither(uint8_t* out, const uint8_t* gray, const Ctx& c)
{
    const int n = c.bytes;
    uint8_t first = out[0] | c.edge[0];
    uint8_t last = out[n - 1] | c.edge[1];
    uint8_t acc = 0;
    for (int x = 0; x < n; x++) {
        for (int k = 0; k < 8; k++) {
            acc = uint8_t(acc << 1);
            if (c.matrix[k] > *gray++)
                acc |= 1;
        }
        out[x] = acc;
    }
    out[0] |= c.edge[2];
    out[n - 1] |= c.edge[3];
    out[0] &= first;
    out[n - 1] &= last;
}

inline int16_t Share(int16_t e, uint8_t w)
{
    return int16_t(int16_t(w * e + 7) >> 4);
}

inline uint8_t Corrected(uint8_t g, uint16_t e, const uint8_t* identity)
{
    int16_t v = int16_t(e + identity[g]);
    if (!g || v < 0)
        return 0;
    return (g == 0xFF || v >= 255) ? 0xFF : uint8_t(v);
}

// sub_6A916FA0: error diffusion, left to right.
void DiffuseLeft(uint8_t* out, const uint8_t* gray, int offset, const Ctx& c)
{
    const int n = c.bytes;
    const uint8_t* w = c.weights + offset;
    const uint8_t* wend = c.weights + 800;
    uint8_t first = out[0] | c.edge[0];
    uint8_t last = out[n - 1] | c.edge[1];
    uint16_t* e = reinterpret_cast<uint16_t*>(c.err + 2);
    for (int i = 0; i < 8 * n; i++)
        c.temp[i] = Corrected(gray[i], e[i], c.identity);
    memset(e, 0, size_t(2 * 8 * n));
    int16_t carry = 0;
    const uint8_t* t = c.temp;
    uint16_t* p = e;
    for (int x = 0; x < n; x++) {
        uint8_t acc = 0;
        for (int k = 0; k < 8; k++) {
            acc = uint8_t(acc << 1);
            int16_t v = int16_t(*t++ + carry);
            if (v <= 127)
                acc++;
            else
                v = int16_t(v - 255);
            p[-1] = uint16_t(p[-1] + Share(v, w[0]));
            p[1] = uint16_t(p[1] + Share(v, w[1]));
            p[0] = uint16_t(p[0] + Share(v, w[2]));
            p++;
            carry = Share(v, w[3]);
            w += 4;
        }
        if (w == wend)
            w -= 800;
        out[x] = acc;
    }
    out[0] = first & (out[0] | c.edge[2]);
    out[n - 1] = last & (out[n - 1] | c.edge[3]);
}

// sub_6A917175: error diffusion, right to left.
void DiffuseRight(uint8_t* out, const uint8_t* gray, int offset, const Ctx& c)
{
    const int n = c.bytes;
    const uint8_t* w = c.weights + offset;
    const uint8_t* wend = c.weights + 800;
    uint8_t first = out[0] | c.edge[0];
    uint8_t last = out[n - 1] | c.edge[1];
    uint16_t* e = reinterpret_cast<uint16_t*>(c.err);
    for (int i = 8 * n - 1; i >= 0; i--)
        c.temp[i] = Corrected(gray[i], e[i + 1], c.identity);
    memset(e, 0, size_t(16 * n));
    int16_t carry = 0;
    const uint8_t* t = c.temp + 8 * n - 1;
    uint16_t* p = e + 8 * n;
    for (int x = n - 1; x >= 0; x--) {
        uint8_t acc = 0;
        for (int k = 0; k < 8; k++) {
            acc = uint8_t(acc >> 1);
            int16_t v = int16_t(*t-- + carry);
            if (v <= 127)
                acc |= 0x80;
            else
                v = int16_t(v - 255);
            p[1] = uint16_t(p[1] + Share(v, w[0]));
            p[-1] = uint16_t(p[-1] + Share(v, w[1]));
            p[0] = uint16_t(p[0] + Share(v, w[2]));
            p--;
            carry = Share(v, w[3]);
            w += 4;
        }
        if (w == wend)
            w -= 800;
        out[x] = acc;
    }
    out[0] = first & (out[0] | c.edge[2]);
    out[n - 1] = last & (out[n - 1] | c.edge[3]);
}

} // namespace

// sub_6A9152E0 + sub_6A91537D
std::vector<uint8_t> HalftonePreview(const BITMAPINFOHEADER* bi, int mode, int brightness, int contrast,
                                     CrtRand& rng)
{
    if (!bi || bi->biBitCount != 24)
        return {};
    const int w = uint16_t(bi->biWidth);
    const int h = uint16_t(bi->biHeight);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(bi) + bi->biSize;
    const bool smooth = mode == 0;
    const bool pattern = mode == 1 || mode == 2;
    const int srcStride = 4 * ((((24 * w + 7) >> 3) + 3) >> 2);
    const int outStride = 2 * ((((w + 7) >> 3) + 1) >> 1);
    const int matrixBytes = pattern ? 64 : 0;
    const int grayBytes = w + 2;
    int tempBytes = 0, errBytes = 0, extra;
    if (!smooth) {
        extra = grayBytes + matrixBytes + 1169;
    } else {
        errBytes = 32 * ((((w + 7) >> 3) + 1) >> 1) + 4;
        tempBytes = 16 * ((((w + 7) >> 3) + 1) >> 1);
        extra = grayBytes + errBytes + tempBytes + matrixBytes + 2225;
    }
    std::vector<uint8_t> mem(size_t(srcStride + extra), 0);
    uint8_t* base = mem.data();
    uint8_t* matrices = base + 1024;
    uint8_t* line = base + 1024 + matrixBytes + 145;   // gray line, the RGB copy follows it
    uint8_t* identity = nullptr;
    uint8_t* weights = nullptr;
    uint8_t* temp = nullptr;
    uint8_t* err = nullptr;
    if (smooth) {
        identity = line;
        weights = identity + 256;
        temp = weights + 800;
        err = temp + tempBytes;
        line = err + errBytes;
    }
    const int bright = int16_t(MulDiv(2 * brightness, 256, 200));
    GrayTables(base, bright);
    if (pattern) {
        memcpy(matrices, mode == 2 ? kPattern2 : kPattern1, 64);
    } else if (smooth) {
        memcpy(weights, kWeights, sizeof(kWeights));
        for (int i = 0; i < 256; i++)
            identity[i] = uint8_t(i);
    }
    Ctx c;
    c.bytes = outStride;
    c.width = w;
    c.r = base;
    c.g = base + 256;
    c.b = base + 512;
    c.weights = weights;
    c.temp = temp;
    c.identity = identity;
    c.err = err;
    EdgeMasks(c, 0, -w & 7);
    const uint8_t* curve = nullptr;
    if (contrast != 50) {
        ContrastTable(base + 768, contrast, bright);
        curve = base + 768;
    }
    std::vector<uint8_t> out(size_t(outStride) * h, 0);
    for (int y = 0; y < h; y++) {
        const uint8_t* s = src + size_t(h - 1 - y) * srcStride;   // bottom-up DIB
        uint8_t* rgb = line + grayBytes;
        memcpy(rgb, s, size_t(3 * w));
        ToGray(line, rgb, c);
        if (curve)
            for (int x = 0; x < w; x++)
                line[x] = curve[line[x]];
        uint8_t* o = &out[size_t(y) * outStride];
        if (pattern) {
            c.matrix = matrices + 8 * (y & 7);
            Dither(o, line, c);
        } else if (smooth) {
            int offset = 32 * (rng.Next() % 25);
            if (y & 1)
                DiffuseRight(o, line, offset, c);
            else
                DiffuseLeft(o, line, offset, c);
        }
        for (int x = 0; x < outStride; x++)
            o[x] = uint8_t(~o[x]);
    }
    return out;
}

} // namespace ui
