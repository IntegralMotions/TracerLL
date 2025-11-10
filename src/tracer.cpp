#include "tracer.h"
#include "seven_bit_encoding.h"
#include <iostream>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
using ssize_t = std::make_signed_t<size_t>;
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cerrno>
#include <cstring>
#endif

Tracer::Tracer(const std::string &port, uint32_t baud)
    : _port(port), _baud(baud)
{
}

Tracer::~Tracer()
{
    stop();
    closePort();
}

bool Tracer::openPort()
{
#ifdef _WIN32
    std::string full = "\\\\.\\" + _port;
    _handle = CreateFileA(
        full.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (_handle == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Tracer: failed to open port \"" << _port
                  << "\", error code " << GetLastError() << "\n";
        return false;
    }

    SetupComm(_handle, 1 << 16, 1 << 16);

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(_handle, &dcb))
    {
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

    if (!SetCommState(_handle, &dcb))
    {
        return false;
    }

    COMMTIMEOUTS tout = {};
    tout.ReadIntervalTimeout = 50;
    tout.ReadTotalTimeoutMultiplier = 0;
    tout.ReadTotalTimeoutConstant = 50;
    tout.WriteTotalTimeoutMultiplier = 0; // 1 ms per byte
    tout.WriteTotalTimeoutConstant = 0;   // +50 ms overhead
    SetCommTimeouts(_handle, &tout);

    EscapeCommFunction(_handle, SETDTR);
    EscapeCommFunction(_handle, SETRTS);
    Sleep(10);

    PurgeComm(_handle, PURGE_TXCLEAR | PURGE_RXCLEAR);
    return true;
#else
    _fd = open(_port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (_fd < 0)
    {
        std::cerr << "Tracer: failed to open port “" << _port
                  << "”: " << std::strerror(errno) << "\n";
        return false;
    }

    termios tty{};
    if (tcgetattr(_fd, &tty) != 0)
    {
        std::cerr << "Tracer: tcgetattr failed: "
                  << std::strerror(errno) << "\n";
    }
    cfmakeraw(&tty);
    cfsetospeed(&tty, _baud);
    cfsetispeed(&tty, _baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    tcflush(_fd, TCIOFLUSH);
    if (tcsetattr(_fd, TCSANOW, &tty) != 0)
    {
        std::cerr << "Tracer: tcsetattr failed: "
                  << std::strerror(errno) << "\n";
    }

    int flags = fcntl(_fd, F_GETFL);
    fcntl(_fd, F_SETFL, flags & ~O_NONBLOCK);

    return true;
#endif
}

bool Tracer::isOpen()
{
#ifdef _WIN32
    return _handle != INVALID_HANDLE_VALUE;
#else
    return _fd >= 0;
#endif
}

void Tracer::closePort()
{
#ifdef _WIN32
    if (_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }
#else
    if (_fd >= 0)
    {
        close(_fd);
        _fd = -1;
    }
#endif
}

bool Tracer::start()
{
    if (!isOpen())
    {
        if (!openPort())
        {
            return false;
        }
    }

    if (_running)
    {
        return true;
    }

    _running = true;
    _thread = std::thread(&Tracer::readLoop, this);
    return true;
}

void Tracer::stop()
{
    _running = false;
    if (_thread.joinable())
        _thread.join();

    closePort();
}

void Tracer::readLoop()
{
    std::cout << "Read loop" << std::endl;

    char buf[256];

#ifdef _WIN32
    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
#endif

    while (_running)
    {
#ifdef _WIN32
        DWORD bytesRead = 0;
        ResetEvent(ov.hEvent);

        // Start an overlapped read
        BOOL ok = ReadFile(_handle, buf, sizeof(buf), nullptr, &ov);
        if (!ok)
        {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                // Wait up to 100 ms for data this tick
                DWORD wait = WaitForSingleObject(ov.hEvent, 100);
                if (wait == WAIT_OBJECT_0)
                {
                    if (!GetOverlappedResult(_handle, &ov, &bytesRead, FALSE))
                        bytesRead = 0;
                }
                else
                {
                    // timeout/no data yet this iteration
                    bytesRead = 0;
                }
            }
            else
            {
                // transient error; clear and continue
                COMSTAT st{};
                DWORD errs = 0;
                ClearCommError(_handle, &errs, &st);
                bytesRead = 0;
            }
        }
        else
        {
            // Completed immediately
            if (!GetOverlappedResult(_handle, &ov, &bytesRead, TRUE))
                bytesRead = 0;
        }

        if (bytesRead == 0)
            continue;
#else
        ssize_t bytesRead = read(_fd, buf, sizeof(buf));
        if (bytesRead <= 0)
            continue;
#endif

        // decode / frame
        std::lock_guard<std::mutex> lock(_readMutex);
        for (ssize_t i = 0; i < static_cast<ssize_t>(bytesRead); ++i)
        {
            uint8_t byte = static_cast<uint8_t>(buf[i]);
            _incomplete.push_back(byte);
            if (SevenBitEncoding::isLastByte(byte))
            {
                std::vector<uint8_t> decoded(_incomplete.size());
                size_t decodedLen = SevenBitEncoding::decodeBuffer(
                    _incomplete.data(),
                    _incomplete.size(),
                    decoded.data(),
                    decoded.size());

                _messages.emplace(decoded.begin(), decoded.begin() + decodedLen);
                _incomplete.clear();
            }
        }
    }

#ifdef _WIN32
    CloseHandle(ov.hEvent);
#endif
}

std::vector<std::vector<uint8_t>> Tracer::getMessages()
{
    std::vector<std::vector<uint8_t>> result;
    std::lock_guard<std::mutex> lock(_readMutex);
    while (!_messages.empty())
    {
        result.push_back(std::move(_messages.front()));
        _messages.pop();
    }
    return result;
}

void Tracer::writeMessage(const std::vector<uint8_t> &message)
{
    std::vector<uint8_t> encoded(
        SevenBitEncoding::getEncodedBufferSize(message.size()));
    size_t encodedLen = SevenBitEncoding::encodeBuffer(
        message.data(), message.size(), encoded.data());
    if (!encodedLen || !isOpen())
    {
        return;
    }

#ifdef _WIN32
    // only attempt to lock for up to 100ms
    if (!_writeMutex.try_lock_for(std::chrono::milliseconds(100)))
    {
        std::cerr << "Tracer: writeMessage busy, timed out acquiring lock\n";
        return;
    }

    size_t offset = 0;
    while (offset < encodedLen)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        DWORD to_write = static_cast<DWORD>(std::min<size_t>(encodedLen - offset, 1 << 16));
        DWORD written = 0;
        BOOL ok = WriteFile(_handle, encoded.data() + offset, to_write, nullptr, &ov);
        if (!ok)
        {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                DWORD wait = WaitForSingleObject(ov.hEvent, 2000);
                if (wait == WAIT_OBJECT_0)
                {
                    if (!GetOverlappedResult(_handle, &ov, &written, FALSE))
                    {
                        written = 0;
                    }
                }
                else
                {
                    CancelIo(_handle);
                    written = 0;
                }
            }
            else
            {
                written = 0;
            }
        }
        else
        {
            if (!GetOverlappedResult(_handle, &ov, &written, TRUE))
                written = 0;
        }
        CloseHandle(ov.hEvent);
        if (written == 0)
        {
            std::cerr << "Tracer: write error\n";
            break;
        }
        offset += written;
    }

    _writeMutex.unlock();

#else
    if (!_writeMutex.try_lock_for(std::chrono::milliseconds(100)))
    {
        std::cerr << "Tracer: write busy\n";
        return;
    }

    size_t off = 0;
    while (off < n)
    {
        ssize_t w = ::write(_fd, encoded.data() + off, encodedLen - off);
        if (w > 0)
        {
            off += static_cast<size_t>(w);
            continue;
        }
        if (w == -1 && errno == EINTR)
            continue;
        if (w == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            continue;
        }
        std::cerr << "Tracer write failed: " << std::strerror(errno) << "\n";
        break;
    }
    _writeMutex.unlock();
#endif
}