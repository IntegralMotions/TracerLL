#include "tracer.h"
#include "usb_communication.h"
#include <IntegralCommunication/CobsEncodedCommunication.h>
#include <IntegralCommunication/Encoding/CobsEncoding.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
    constexpr size_t CobsTxBufferSize = 4096;
    constexpr size_t CobsRxBufferSize = 4096;
} // namespace

Tracer::Tracer(std::string& port, uint32_t baud, size_t receiveQueueCapacity)
    : _port(std::move(port)), _baud(baud), _receiveQueueCapacity(receiveQueueCapacity) {
    if (receiveQueueCapacity == 0) {
        throw std::invalid_argument("receive queue capacity must be positive");
    }
}

Tracer::~Tracer() {
    stop();
}

bool Tracer::openPort() {
    _usbCommunication = std::make_unique<UsbCommunication>(_port, _baud);
    if (!_usbCommunication->open()) {
        _usbCommunication.reset();
        return false;
    }

    _cobsCommunication =
        std::make_unique<CobsEncodedCommunication>(*_usbCommunication, CobsTxBufferSize, CobsRxBufferSize);
    return true;
}

bool Tracer::isOpen() {
    return _usbCommunication && _usbCommunication->isOpen();
}

void Tracer::closePort() {
    _cobsCommunication.reset();
    if (_usbCommunication) {
        _usbCommunication->close();
        _usbCommunication.reset();
    }
}

bool Tracer::start() {
    std::scoped_lock lock(_lifecycleMutex);
    if (!isOpen()) {
        if (!openPort()) {
            return false;
        }
    }

    if (_running) {
        return true;
    }

    {
        std::scoped_lock queueLock(_readMutex);
        _messages = {};
    }
    _writeFailed = false;

    _running = true;
    try {
        _thread = std::thread(&Tracer::readLoop, this);
    } catch (...) {
        _running = false;
        closePort();
        throw;
    }
    return true;
}

void Tracer::stop() {
    std::scoped_lock lock(_lifecycleMutex);
    _running = false;
    if (_thread.joinable()) {
        _thread.join();
    }

    closePort();
}

void Tracer::readLoop() {
    // The dependency's receiver stalls on a full unterminated frame. Read
    // incrementally here so oversized frames are discarded through their next
    // delimiter and subsequent frames can be received normally.
    std::array<uint8_t, CobsRxBufferSize> decoded{};
    std::array<uint8_t, CobsRxBufferSize - 1> encoded{};
    std::array<uint8_t, 1024> incoming{};
    size_t encodedSize = 0;
    bool discarding = false;

    while (_running) {
        const size_t available = _usbCommunication->available();
        const size_t count = available == 0 ? 0 : _usbCommunication->read(incoming.data(), std::min(available, incoming.size()));
        if (count == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        for (size_t i = 0; i < count; ++i) {
            const uint8_t byte = incoming[i];
            if (byte != CobsEncoding::Delimiter) {
                if (!discarding) {
                    if (encodedSize == encoded.size()) {
                        discarding = true;
                    } else {
                        encoded[encodedSize++] = byte;
                    }
                }
                continue;
            }

            size_t decodedLen = 0;
            if (!discarding && CobsEncoding::decodeBuffer(encoded.data(), encodedSize, decoded.data(), decoded.size(),
                                                          decodedLen)) {
                std::scoped_lock lock(_readMutex);
                if (_messages.size() < _receiveQueueCapacity) {
                    _messages.emplace(decoded.begin(), decoded.begin() + decodedLen);
                } else if (_droppedMessages != std::numeric_limits<uint64_t>::max()) {
                    ++_droppedMessages;
                }
            }
            encodedSize = 0;
            discarding = false;
        }
    }
}

std::vector<std::vector<uint8_t>> Tracer::getMessages() {
    std::vector<std::vector<uint8_t>> result;
    std::scoped_lock lock(_readMutex);
    while (!_messages.empty()) {
        result.push_back(std::move(_messages.front()));
        _messages.pop();
    }
    return result;
}

uint64_t Tracer::droppedMessages() {
    std::scoped_lock lock(_readMutex);
    return _droppedMessages;
}

bool Tracer::writeMessage(const std::vector<uint8_t>& message) {
    std::scoped_lock lock(_lifecycleMutex);
    if (!_cobsCommunication || !isOpen() || _writeFailed) {
        return false;
    }

    _usbCommunication->resetWriteStatus();
    const bool success = _cobsCommunication->writeMessage(message.data(), message.size()) &&
                         _usbCommunication->lastWriteSucceeded();
    if (!success) {
        // A partial frame may have been sent. Require an explicit reconnect.
        _writeFailed = true;
    }
    return success;
}
