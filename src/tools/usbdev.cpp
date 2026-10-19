#include "usbdev.h"
#include <setupapi.h>
#include <initguid.h>

#pragma comment(lib, "setupapi.lib")

DEFINE_GUID(GUID_DEVINTERFACE_USBPRINT_LOCAL, 0x28d78fad, 0x5a12, 0x11d1, 0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2);

#define IOCTL_USBPRINT_GET_1284_ID CTL_CODE(FILE_DEVICE_UNKNOWN, 13, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USBPRINT_SOFT_RESET  CTL_CODE(FILE_DEVICE_UNKNOWN, 16, METHOD_BUFFERED, FILE_ANY_ACCESS)

std::vector<UsbPrinterInfo> EnumUsbPrinters()
{
    std::vector<UsbPrinterInfo> out;
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USBPRINT_LOCAL, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE)
        return out;
    SP_DEVICE_INTERFACE_DATA ifd = { sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &GUID_DEVINTERFACE_USBPRINT_LOCAL, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        std::vector<BYTE> buf(need);
        auto det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, det, need, nullptr, nullptr))
            continue;
        UsbPrinterInfo info;
        info.path = det->DevicePath;
        UsbPrinter p;
        if (p.Open(info.path))
            info.deviceId = p.GetDeviceId();
        out.push_back(info);
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

bool UsbPrinter::Open(const std::wstring& path)
{
    Close();
    h_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    return h_ != INVALID_HANDLE_VALUE;
}

void UsbPrinter::Close()
{
    if (h_ != INVALID_HANDLE_VALUE) {
        CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }
}

static bool WaitIo(HANDLE h, OVERLAPPED* ov, DWORD timeoutMs, DWORD* done)
{
    DWORD r = WaitForSingleObject(ov->hEvent, timeoutMs);
    if (r != WAIT_OBJECT_0) {
        CancelIoEx(h, ov);
        GetOverlappedResult(h, ov, done, TRUE);
        return false;
    }
    return GetOverlappedResult(h, ov, done, FALSE) != FALSE;
}

bool UsbPrinter::Write(const void* data, DWORD len, DWORD timeoutMs)
{
    const BYTE* p = static_cast<const BYTE*>(data);
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool ok = true;
    while (len && ok) {
        ResetEvent(ov.hEvent);
        DWORD done = 0;
        if (!WriteFile(h_, p, len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
            ok = false;
            break;
        }
        ok = WaitIo(h_, &ov, timeoutMs, &done);
        p += done;
        len -= done;
        if (ok && done == 0)
            ok = false;
    }
    CloseHandle(ov.hEvent);
    return ok;
}

int UsbPrinter::Read(void* data, DWORD len, DWORD timeoutMs)
{
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD done = 0;
    int ret;
    if (!ReadFile(h_, data, len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
        ret = -1;
    } else if (WaitIo(h_, &ov, timeoutMs, &done)) {
        ret = static_cast<int>(done);
    } else {
        ret = (GetLastError() == ERROR_OPERATION_ABORTED || done == 0) ? 0 : -1;
    }
    CloseHandle(ov.hEvent);
    return ret;
}

bool UsbPrinter::SoftReset()
{
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD done = 0;
    BOOL ok = DeviceIoControl(h_, IOCTL_USBPRINT_SOFT_RESET, nullptr, 0, nullptr, 0, nullptr, &ov);
    bool res = ok || (GetLastError() == ERROR_IO_PENDING && WaitIo(h_, &ov, 5000, &done));
    CloseHandle(ov.hEvent);
    return res;
}

std::string UsbPrinter::GetDeviceId()
{
    BYTE buf[1024] = {};
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD done = 0;
    BOOL ok = DeviceIoControl(h_, IOCTL_USBPRINT_GET_1284_ID, nullptr, 0, buf, sizeof(buf), nullptr, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = WaitIo(h_, &ov, 5000, &done);
    else
        GetOverlappedResult(h_, &ov, &done, FALSE);
    CloseHandle(ov.hEvent);
    if (!ok || done < 2)
        return {};
    // First two bytes are a big-endian length that includes themselves.
    size_t n = (static_cast<size_t>(buf[0]) << 8) | buf[1];
    if (n < 2 || n > done)
        n = done;
    return std::string(reinterpret_cast<char*>(buf + 2), n - 2);
}
