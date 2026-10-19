// Development helper: AddPrinterW with explicit error reporting (run elevated).
// usage: addprinter <printer name> <driver name> <port> [datatype]
#include <windows.h>
#include <winspool.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) {
        printf("usage: addprinter <name> <driver> <port> [datatype]\n");
        return 2;
    }
    PRINTER_INFO_2W pi = {};
    pi.pPrinterName = argv[1];
    pi.pDriverName = argv[2];
    pi.pPortName = argv[3];
    pi.pPrintProcessor = const_cast<LPWSTR>(L"winprint");
    pi.pDatatype = const_cast<LPWSTR>(argc > 4 ? argv[4] : L"RAW");
    pi.Attributes = PRINTER_ATTRIBUTE_LOCAL;
    HANDLE h = AddPrinterW(nullptr, 2, reinterpret_cast<LPBYTE>(&pi));
    if (!h) {
        DWORD e = GetLastError();
        wchar_t msg[512] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, msg, 512, nullptr);
        printf("AddPrinter failed: %lu (0x%08lX) %ls\n", e, e, msg);
        return 1;
    }
    printf("AddPrinter ok\n");
    ClosePrinter(h);
    return 0;
}
