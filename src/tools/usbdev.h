// Direct access to a USB printer through usbprint.sys (used by the CLI test tool only;
// the driver itself talks to the printer through the spooler's port monitor).
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct UsbPrinterInfo {
    std::wstring path;
    std::string deviceId;   // IEEE-1284 device ID
};

std::vector<UsbPrinterInfo> EnumUsbPrinters();

class UsbPrinter {
public:
    ~UsbPrinter() { Close(); }
    bool Open(const std::wstring& path);
    void Close();
    // Writes the whole buffer; returns false on error/timeout.
    bool Write(const void* data, DWORD len, DWORD timeoutMs = 10000);
    // Reads whatever is available (up to len); returns bytes read, -1 on error. 0 on timeout.
    int Read(void* data, DWORD len, DWORD timeoutMs);
    bool SoftReset();
    std::string GetDeviceId();
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};
