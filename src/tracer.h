#pragma once

#include <string>
#include <vector>
#include <queue>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

class CobsEncodedCommunication;
class UsbCommunication;

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

    std::atomic<bool> _running{false};

    std::queue<std::vector<uint8_t>> _messages;
    std::unique_ptr<UsbCommunication> _usbCommunication;
    std::unique_ptr<CobsEncodedCommunication> _cobsCommunication;

    bool isOpen();
    bool openPort();
    void closePort();
    void readLoop();
};
