#include "tracer.h"
#include "usb_communication.h"
#include <IntegralCommunication/CobsEncodedCommunication.h>
#include <chrono>
#include <thread>

namespace {
    constexpr size_t CobsTxBufferSize = 4096;
    constexpr size_t CobsRxBufferSize = 4096;
} // namespace

Tracer::Tracer(const std::string& port, uint32_t baud) : _port(port), _baud(baud) {}

Tracer::~Tracer() {
    stop();
    closePort();
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
    if (!isOpen()) {
        if (!openPort()) {
            return false;
        }
    }

    if (_running) {
        return true;
    }

    _running = true;
    _thread = std::thread(&Tracer::readLoop, this);
    return true;
}

void Tracer::stop() {
    _running = false;
    if (_thread.joinable()) {
        _thread.join();
    }

    closePort();
}

void Tracer::readLoop() {
    std::vector<uint8_t> decoded(CobsRxBufferSize);

    while (_running) {
        if (!_cobsCommunication) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        size_t decodedLen = 0;
        if (_cobsCommunication->readMessage(decoded.data(), decoded.size(), decodedLen)) {
            std::lock_guard<std::mutex> lock(_readMutex);
            _messages.emplace(decoded.begin(), decoded.begin() + decodedLen);
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

std::vector<std::vector<uint8_t>> Tracer::getMessages() {
    std::vector<std::vector<uint8_t>> result;
    std::lock_guard<std::mutex> lock(_readMutex);
    while (!_messages.empty()) {
        result.push_back(std::move(_messages.front()));
        _messages.pop();
    }
    return result;
}

void Tracer::writeMessage(const std::vector<uint8_t>& message) {
    if (!_cobsCommunication || !isOpen()) {
        return;
    }

    _cobsCommunication->writeMessage(message.data(), message.size());
}
