#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "seven_bit_encoding.h"

namespace py = pybind11;

PYBIND11_MODULE(SevenBitEncoding, m)
{
    m.doc() = "7-bit encoding utilities";

    m.def("get_encoded_size", &SevenBitEncoding::getEncodedSize,
          R"pbdoc(
        get_encoded_size(value: int) -> int

        Return the number of bytes needed to encode the given value using 7-bit encoding.
        )pbdoc");

    m.def("encode_value", [](uint32_t value)
          {
        size_t size = SevenBitEncoding::getEncodedSize(value);
        std::vector<uint8_t> buffer(size);
        SevenBitEncoding::encodeValue(value, buffer.data());
        return py::bytes(reinterpret_cast<char*>(buffer.data()), size); }, R"pbdoc(
        encode_value(value: int) -> bytes

        Encode the given value using 7-bit encoding and return the resulting bytes.
        )pbdoc");

    m.def("decode_value", [](py::bytes input)
          {
        std::string data = input;
        size_t consumed = 0;
        uint32_t value = SevenBitEncoding::decodeValue(
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size(), consumed);
        return py::make_tuple(value, consumed); }, R"pbdoc(
        decode_value(encoded: bytes) -> tuple[int, int]

        Decode the value from the given bytes.
        Returns a tuple of (value, bytes_consumed).
        )pbdoc");

    m.def("get_encoded_buffer_size", &SevenBitEncoding::getEncodedBufferSize,
          R"pbdoc(
        get_encoded_buffer_size(buffer_length: int) -> int

        Return the maximum size needed to encode a buffer of the given length.
        )pbdoc");

    m.def("encode_buffer", [](py::bytes input)
          {
        std::string data = input;
        size_t outSize = SevenBitEncoding::getEncodedBufferSize(data.size());
        std::vector<uint8_t> buffer(outSize);
        size_t actualSize = SevenBitEncoding::encodeBuffer(
            reinterpret_cast<const uint8_t*>(data.data()),
            data.size(), buffer.data());
        return py::bytes(reinterpret_cast<char*>(buffer.data()), actualSize); }, R"pbdoc(
        encode_buffer(input: bytes) -> bytes

        Encode the input buffer using 7-bit encoding and return the encoded bytes.
        )pbdoc");

    m.def("decode_buffer", [](py::bytes input)
          {
        std::string data = input;
        std::vector<uint8_t> output(data.size());
        size_t actualSize = SevenBitEncoding::decodeBuffer(
            reinterpret_cast<const uint8_t *>(data.data()),
            data.size(), output.data(), output.size());
        return py::bytes(reinterpret_cast<char*>(output.data()), actualSize); }, R"pbdoc(
        decode_buffer(input: bytes) -> bytes

        Decode the input buffer using 7-bit encoding into an output buffer of the given length,
        and return the decoded bytes.
        )pbdoc");

    m.def("is_last_byte", &SevenBitEncoding::isLastByte,
          R"pbdoc(
        is_last_byte(byte: int) -> bool

        Return whether or not this byte is the last byte of a 7-bit encoded value.
        )pbdoc");
}
