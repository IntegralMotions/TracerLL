#pragma once

#include <string>
#include <vector>
#include <queue>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>

class Tracer
{
public:
    Tracer(const std::string &port, uint32_t baud);
    ~Tracer();

    bool start();
    void stop();
    std::vector<std::vector<uint8_t>> getMessages();
    void writeMessage(const std::vector<uint8_t> &message);

private:
    std::string _port;
    uint32_t _baud;
    std::thread _thread;
    std::mutex _readMutex;
    std::timed_mutex _writeMutex;

    std::atomic<bool> _running{false};

    std::vector<uint8_t> _incomplete;
    std::queue<std::vector<uint8_t>> _messages;

#ifdef _WIN32
    void *_handle = nullptr;
#else
    int _fd = -1;
#endif

    bool isOpen();
    bool openPort();
    void closePort();
    void readLoop();
};
