#include "seven_bit_encoding.h"

namespace SevenBitEncoding
{
    inline uint8_t leftMask(uint8_t n)
    {
        return static_cast<uint8_t>((1 << (n)) - 1);
    }

    size_t getEncodedSize(uint32_t value)
    {
        size_t size = 0;
        do
        {
            size++;
            value >>= 7;
        } while (value > 0);
        return size;
    }

    void encodeValue(uint32_t value, uint8_t *output)
    {
        size_t index = 0;
        do
        {
            uint8_t byte = value & 0x7F; // Take the lower 7 bits
            value >>= 7;
            if (value > 0)
            {
                byte |= 0x80; // Set the 8th bit if more bytes are needed
            }
            output[index++] = byte;
        } while (value > 0);
    }

    uint32_t decodeValue(const uint8_t *input, size_t inputSize,
                         size_t &consumedBytes)
    {
        uint32_t length = 0;
        size_t shift = 0;
        consumedBytes = 0;

        for (size_t i = 0; i < inputSize; i++)
        {
            uint8_t byte = input[i];
            length |= (byte & 0x7F) << shift; // Extract 7 bits and shift into place
            consumedBytes++;

            if ((byte & 0x80) == 0) // Stop if the continuation bit is not set
            {
                break;
            }

            shift += 7;
            if (shift >= 32) // Prevent overflow for invalid input
            {
                break;
            }
        }

        return length;
    }

    size_t getEncodedBufferSize(const size_t bufferLength)
    {
        return (bufferLength > 0) ? bufferLength + ((bufferLength - 1) / 7) + 1 : 1;
    }

    size_t encodeBuffer(const uint8_t *in, size_t inLen, uint8_t *out)
    {
        if (inLen == 0)
            return 0;
        size_t outIndex = 0;
        uint8_t carry = 0;
        int carryBits = 0;
        for (size_t i = 0; i < inLen; i++)
        {
            uint8_t current = in[i];
            uint8_t septet = carry | (current >> (carryBits + 1));
            out[outIndex++] = septet | 0x80;
            carryBits++;
            carry = (current & leftMask(carryBits)) << (7 - carryBits);
            if (carryBits == 7)
            {
                out[outIndex++] = carry | 0x80;
                carry = 0;
                carryBits = 0;
            }
        }
        if (carryBits != 0)
        {
            uint8_t septet = carry;
            out[outIndex++] = septet;
        }
        out[outIndex - 1] &= 0x7F;
        return outIndex;
    }

    size_t decodeBuffer(const uint8_t *in, size_t inLen, uint8_t *out,
                        size_t outLen)
    {
        if (inLen == 0 || outLen == 0)
            return 0;
        size_t decoded = 0, encIndex = 0;
        int n = 0;
        while (decoded < outLen && encIndex + 1 < inLen)
        {
            uint8_t s = in[encIndex] & 0x7F;
            uint8_t t = in[encIndex + 1] & 0x7F;
            int bits = n + 1;
            uint8_t mask = ((1 << bits) - 1) << (7 - bits);
            uint8_t newCarry = t & mask;
            uint8_t r = newCarry >> (7 - bits);
            uint8_t A_upper = s & ((1 << (7 - n)) - 1);
            uint8_t A = (A_upper << bits) | r;
            out[decoded++] = A;
            n++;
            encIndex++;
            if (n == 7)
            {
                encIndex++;
                n = 0;
            }
        }
        return decoded;
    }

    bool isLastByte(const uint8_t byte)
    {
        return (byte & 0x80) == 0;
    }
}
