#pragma once

#include <IntegralCommunication/Communication.h>
#include <cstdint>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

class UsbCommunication : public Communication {
  public:
    UsbCommunication(std::string port, uint32_t baud);
    ~UsbCommunication() override;

    UsbCommunication(const UsbCommunication&) = delete;
    UsbCommunication& operator=(const UsbCommunication&) = delete;

    bool open();
    void close();
    bool isOpen() const;

  private:
    size_t writeImpl(const uint8_t* data, size_t size) override;
    size_t availableImpl() override;
    size_t readImpl(uint8_t* data, size_t size) override;

    std::string _port;
    uint32_t _baud;
    std::timed_mutex _writeMutex;

#ifdef _WIN32
    HANDLE _handle = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};
