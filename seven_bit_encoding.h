#pragma once

#include <inttypes.h>
#include <stddef.h>

namespace SevenBitEncoding
{
    size_t getEncodedSize(uint32_t value);
    void encodeValue(uint32_t value, uint8_t *output);
    uint32_t decodeValue(const uint8_t *input, size_t inputSize,
                         size_t &consumedBytes);

    size_t getEncodedBufferSize(const size_t bufferLength);
    size_t encodeBuffer(const uint8_t *inputBuffer, const size_t inputLength,
                        uint8_t *outputBuffer);
    size_t decodeBuffer(const uint8_t *inputBuffer, const size_t inputLength,
                        uint8_t *outputBuffer, const size_t outputLength);

    inline uint8_t leftMask(uint8_t n);
}
