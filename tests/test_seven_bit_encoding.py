import pytest
from TracerLL import SevenBitEncoding


def test_get_encoded_size():
    assert SevenBitEncoding.get_encoded_size(0) == 1
    assert SevenBitEncoding.get_encoded_size(127) == 1
    assert SevenBitEncoding.get_encoded_size(128) > 1


def test_encode_decode_value():
    value = 300
    encoded = SevenBitEncoding.encode_value(value)
    decoded, consumed = SevenBitEncoding.decode_value(encoded)
    assert decoded == value
    assert consumed == len(encoded)


def test_get_encoded_buffer_size():
    assert SevenBitEncoding.get_encoded_buffer_size(10) >= 10


def test_encode_decode_buffer():
    data = b"hello"
    encoded = SevenBitEncoding.encode_buffer(data)
    decoded = SevenBitEncoding.decode_buffer(encoded)
    assert decoded == data


def test_is_last_byte():
    assert SevenBitEncoding.is_last_byte(0b01111111)  # Last byte (MSB = 0)
    assert not SevenBitEncoding.is_last_byte(0b10000000)  # Not last (MSB = 1)


def test_left_mask():
    assert SevenBitEncoding.left_mask(3) == 0b00000111
