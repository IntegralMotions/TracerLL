#include "tracer.h"
#include "seven_bit_encoding.h"
#include <iostream>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
using ssize_t = std::make_signed_t<size_t>; // <--- add this
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
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

bool Tracer::openPort()
{
#ifdef _WIN32
    std::string full = "\\\\.\\" + _port;
    _handle = CreateFileA(
        full.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        0, nullptr);

    if (_handle == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Tracer: failed to open port \"" << _port
                  << "\", error code " << GetLastError() << "\n";
        return false;
    }

    COMMTIMEOUTS tout = {};
    tout.ReadIntervalTimeout = 50;
    tout.ReadTotalTimeoutMultiplier = 10;
    tout.ReadTotalTimeoutConstant = 50;
    tout.WriteTotalTimeoutMultiplier = 1; // 1 ms per byte
    tout.WriteTotalTimeoutConstant = 50;  // +50 ms overhead
    SetCommTimeouts(_handle, &tout);

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
    cfsetospeed(&tty, _baud);
    cfsetispeed(&tty, _baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(_fd, TCSANOW, &tty) != 0)
    {
        std::cerr << "Tracer: tcsetattr failed: "
                  << std::strerror(errno) << "\n";
    }
    return true;
#endif
}

bool Tracer::isOpen()
{
#ifdef _WIN32
    return _handle != nullptr;
#else
    return _fd >= 0;
#endif
}

void Tracer::closePort()
{
#ifdef _WIN32
    if (_handle)
        CloseHandle(_handle);
#else
    if (_fd >= 0)
        close(_fd);
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
    char buf[256];
    while (_running)
    {
#ifdef _WIN32
        DWORD bytesRead = 0;
        if (ReadFile(_handle, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0)
#else
        ssize_t bytesRead = read(_fd, buf, sizeof(buf));
        if (bytesRead > 0)
#endif
        {
            std::lock_guard<std::mutex> lock(_readMutex);
            for (ssize_t i = 0; i < bytesRead; ++i)
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

                    std::vector<uint8_t> message(decoded.begin(), decoded.begin() + decodedLen);
                    _messages.push(std::move(message));
                    _incomplete.clear();
                }
            }
        }
    }
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
    if (encodedLen == 0)
        return;

#ifdef _WIN32
    // only attempt to lock for up to 100ms
    if (!_writeMutex.try_lock_for(std::chrono::milliseconds(100)))
    {
        std::cerr << "Tracer: writeMessage busy, timed out acquiring lock\n";
        return;
    }

    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    DWORD written = 0;
    BOOL ok = WriteFile(
        _handle,
        encoded.data(),
        static_cast<DWORD>(encodedLen),
        &written,
        &ov);

    if (!ok)
    {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            // wait up to 1 second for the write to finish
            if (WaitForSingleObject(ov.hEvent, 1000) == WAIT_OBJECT_0)
            {
                GetOverlappedResult(_handle, &ov, &written, FALSE);
            }
            else
            {
                std::cerr << "Tracer: writeMessage timed out on overlapped write\n";
                CancelIo(_handle);
            }
        }
        else
        {
            DWORD err = GetLastError();
            LPSTR msgBuf = nullptr;
            FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                err,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&msgBuf, 0, nullptr);

            std::cerr << "Tracer: writeMessage failed, error "
                      << err << "\n\t" << msgBuf;

            LocalFree(msgBuf);
        }
    }

    CloseHandle(ov.hEvent);
    _writeMutex.unlock();

#else
    // POSIX side stays the same, but only write `encodedLen` bytes
    if (_writeMutex.try_lock_for(std::chrono::milliseconds(100)))
    {
        ssize_t ret = ::write(_fd, encoded.data(), encodedLen);
        if (ret < 0)
            std::cerr << "Tracer write failed: " << std::strerror(errno) << "\n";
        _writeMutex.unlock();
    }
    else
    {
        std::cerr << "Tracer: writeMessage busy, timed out acquiring lock\n";
    }
#endif
}