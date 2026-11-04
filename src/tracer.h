#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class CobsEncodedCommunication;
class UsbCommunication;

class Tracer {
  public:
    Tracer(std::string& port, uint32_t baud, size_t receiveQueueCapacity = 1024, int64_t idleReadDelayUs = 100);
    ~Tracer();

    bool start();
    void stop();
    std::vector<std::vector<uint8_t>> getMessages();
    bool writeMessage(const std::vector<uint8_t>& message);
    uint64_t droppedMessages();
    int64_t idleReadDelayUs() const { return _idleReadDelayUs; }

  private:
    std::string _port;
    uint32_t _baud;
    std::thread _thread;
    std::mutex _readMutex;
    // Serializes start/stop and the complete COBS write, including its shared TX buffer.
    std::mutex _lifecycleMutex;
    std::mutex _idleMutex;
    std::condition_variable _idleCondition;

    std::atomic<bool> _running{false};

    std::queue<std::vector<uint8_t>> _messages;
    const size_t _receiveQueueCapacity;
    const int64_t _idleReadDelayUs;
    uint64_t _droppedMessages = 0;
    bool _writeFailed = false;
    std::unique_ptr<UsbCommunication> _usbCommunication;
    std::unique_ptr<CobsEncodedCommunication> _cobsCommunication;

    bool isOpen();
    bool openPort();
    void closePort();
    void readLoop();
};
