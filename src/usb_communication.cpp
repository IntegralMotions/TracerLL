#include "usb_communication.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#include <windows.h>
using ssize_t = std::make_signed_t<size_t>;
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

UsbCommunication::UsbCommunication(std::string port, uint32_t baud) : _port(std::move(port)), _baud(baud) {}

UsbCommunication::~UsbCommunication() {
    close();
}

bool UsbCommunication::open() {
    if (isOpen()) {
        return true;
    }

#ifdef _WIN32
    std::string full = "\\\\.\\" + _port;
    _handle = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                          nullptr);

    if (_handle == INVALID_HANDLE_VALUE) {
        std::cerr << "UsbCommunication: failed to open port \"" << _port << "\", error code " << GetLastError() << "\n";
        return false;
    }

    SetupComm(_handle, 1 << 16, 1 << 16);

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(_handle, &dcb)) {
        return false;
    }
    dcb.BaudRate = _baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    dcb.fTXContinueOnXoff = FALSE;

    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;

    if (!SetCommState(_handle, &dcb)) {
        return false;
    }

    COMMTIMEOUTS tout = {};
    tout.ReadIntervalTimeout = 50;
    tout.ReadTotalTimeoutMultiplier = 0;
    tout.ReadTotalTimeoutConstant = 50;
    tout.WriteTotalTimeoutMultiplier = 0;
    tout.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(_handle, &tout);

    EscapeCommFunction(_handle, SETDTR);
    EscapeCommFunction(_handle, SETRTS);
    Sleep(10);

    PurgeComm(_handle, PURGE_TXCLEAR | PURGE_RXCLEAR);
#else
    _fd = ::open(_port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (_fd < 0) {
        std::cerr << "UsbCommunication: failed to open port \"" << _port << "\": " << std::strerror(errno) << "\n";
        return false;
    }

    termios tty{};
    if (tcgetattr(_fd, &tty) != 0) {
        std::cerr << "UsbCommunication: tcgetattr failed: " << std::strerror(errno) << "\n";
    }
    cfmakeraw(&tty);
    cfsetospeed(&tty, _baud);
    cfsetispeed(&tty, _baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    tcflush(_fd, TCIOFLUSH);
    if (tcsetattr(_fd, TCSANOW, &tty) != 0) {
        std::cerr << "UsbCommunication: tcsetattr failed: " << std::strerror(errno) << "\n";
    }

    int flags = fcntl(_fd, F_GETFL);
    fcntl(_fd, F_SETFL, flags & ~O_NONBLOCK);
#endif

    return true;
}

void UsbCommunication::close() {
#ifdef _WIN32
    if (_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }
#else
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
#endif
}

bool UsbCommunication::isOpen() const {
#ifdef _WIN32
    return _handle != INVALID_HANDLE_VALUE;
#else
    return _fd >= 0;
#endif
}

size_t UsbCommunication::availableImpl() {
#ifdef _WIN32
    if (_handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    COMSTAT status{};
    DWORD errors = 0;
    if (!ClearCommError(_handle, &errors, &status)) {
        return 0;
    }
    return static_cast<size_t>(status.cbInQue);
#else
    if (_fd < 0) {
        return 0;
    }

    int available = 0;
    if (ioctl(_fd, FIONREAD, &available) != 0 || available < 0) {
        return 0;
    }
    return static_cast<size_t>(available);
#endif
}

size_t UsbCommunication::readImpl(uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || !isOpen()) {
        return 0;
    }

#ifdef _WIN32
    DWORD bytesRead = 0;
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = ReadFile(_handle, data, static_cast<DWORD>(std::min<size_t>(size, 1 << 16)), nullptr, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ov.hEvent, 100);
        if (wait == WAIT_OBJECT_0) {
            GetOverlappedResult(_handle, &ov, &bytesRead, FALSE);
        } else {
            CancelIo(_handle);
        }
    } else if (ok) {
        GetOverlappedResult(_handle, &ov, &bytesRead, TRUE);
    }
    CloseHandle(ov.hEvent);
    return static_cast<size_t>(bytesRead);
#else
    ssize_t bytesRead = ::read(_fd, data, size);
    if (bytesRead <= 0) {
        return 0;
    }
    return static_cast<size_t>(bytesRead);
#endif
}

size_t UsbCommunication::writeImpl(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || !isOpen()) {
        return 0;
    }

#ifdef _WIN32
    if (!_writeMutex.try_lock_for(std::chrono::milliseconds(100))) {
        std::cerr << "UsbCommunication: write busy\n";
        return 0;
    }

    size_t offset = 0;
    while (offset < size) {
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        DWORD toWrite = static_cast<DWORD>(std::min<size_t>(size - offset, 1 << 16));
        DWORD written = 0;
        BOOL ok = WriteFile(_handle, data + offset, toWrite, nullptr, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 2000);
            if (wait == WAIT_OBJECT_0) {
                GetOverlappedResult(_handle, &ov, &written, FALSE);
            } else {
                CancelIo(_handle);
            }
        } else if (ok) {
            GetOverlappedResult(_handle, &ov, &written, TRUE);
        }
        CloseHandle(ov.hEvent);
        if (written == 0) {
            std::cerr << "UsbCommunication: write error\n";
            break;
        }
        offset += written;
    }

    _writeMutex.unlock();
    return offset;
#else
    if (!_writeMutex.try_lock_for(std::chrono::milliseconds(100))) {
        std::cerr << "UsbCommunication: write busy\n";
        return 0;
    }

    size_t off = 0;
    while (off < size) {
        ssize_t written = ::write(_fd, data + off, size - off);
        if (written > 0) {
            off += static_cast<size_t>(written);
            continue;
        }
        if (written == -1 && errno == EINTR) {
            continue;
        }
        if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        std::cerr << "UsbCommunication: write failed: " << std::strerror(errno) << "\n";
        break;
    }
    _writeMutex.unlock();
    return off;
#endif
}
