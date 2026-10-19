// Development helper: renders a page through the printer driver *in this process*
// (DOCINFO datatype RAW bypasses EMF spooling), so driver problems can be debugged here.
// usage: rawprint <printer name> [pages]
#include <windows.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
        return 2;
    int pages = argc > 2 ? _wtoi(argv[2]) : 1;
    HDC dc = CreateDCW(L"WINSPOOL", argv[1], nullptr, nullptr);
    if (!dc) {
        printf("CreateDC failed %lu\n", GetLastError());
        return 1;
    }
    printf("HORZRES %d VERTRES %d LOGPIXELSX %d PHYSOFFSETX %d PHYSOFFSETY %d\n", GetDeviceCaps(dc, HORZRES),
           GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, PHYSICALOFFSETX),
           GetDeviceCaps(dc, PHYSICALOFFSETY));
    DOCINFOW di = { sizeof(di) };
    di.lpszDocName = L"rawprint test";
    di.lpszDatatype = L"RAW";
    int job = StartDocW(dc, &di);
    printf("StartDoc = %d (err %lu)\n", job, GetLastError());
    for (int p = 1; p <= pages && job > 0; p++) {
        int r = StartPage(dc);
        printf("StartPage = %d (err %lu)\n", r, GetLastError());
        HFONT f = CreateFontW(-200, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t text[64];
        swprintf_s(text, L"rawprint page %d", p);
        TextOutW(dc, 300, 300, text, int(wcslen(text)));
        SelectObject(dc, old);
        DeleteObject(f);
        Rectangle(dc, 100, 100, GetDeviceCaps(dc, HORZRES) - 100, GetDeviceCaps(dc, VERTRES) - 100);
        r = EndPage(dc);
        printf("EndPage = %d (err %lu)\n", r, GetLastError());
    }
    if (job > 0) {
        int r = EndDoc(dc);
        printf("EndDoc = %d (err %lu)\n", r, GetLastError());
    }
    DeleteDC(dc);
    return 0;
}
