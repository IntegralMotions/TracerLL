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

bool Tracer::openPort()
{
#ifdef _WIN32
    std::string full = "\\\\.\\" + _port;
    _handle = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    return _handle != INVALID_HANDLE_VALUE;
#else
    _fd = open(_port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (_fd < 0)
        return false;

    termios tty{};
    tcgetattr(_fd, &tty);
    cfsetospeed(&tty, _baud);
    cfsetispeed(&tty, _baud);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(_fd, TCSANOW, &tty);
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

void Tracer::start()
{
    if (!isOpen())
        openPort();

    if (_running)
        return;
    _running = true;
    _thread = std::thread(&Tracer::readLoop, this);
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
    std::vector<uint8_t> encoded(SevenBitEncoding::getEncodedBufferSize(message.size()));
    size_t encodedLen = SevenBitEncoding::encodeBuffer(
        message.data(),
        message.size(),
        encoded.data());

    std::lock_guard<std::mutex> lock(_writeMutex);

#ifdef _WIN32
    if (_handle && encodedLen > 0)
    {
        DWORD written = 0;
        WriteFile(_handle, encoded.data(), static_cast<DWORD>(encodedLen), &written, NULL);
    }
#else
    if (_fd >= 0 && encodedLen > 0)
    {
        write(_fd, encoded.data(), encoded.size());
    }
#endif
}
