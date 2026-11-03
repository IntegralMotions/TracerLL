"""Trace-protocol CRC and scalar configuration parsing (not COBS framing)."""

import binascii

from .TraceValueType import VALUE_FORMATS


def packet(payload):
    return payload + binascii.crc_hqx(payload, 0xFFFF).to_bytes(2, "little")


def checked_message(data):
    data = bytes(data)
    if len(data) < 3 or binascii.crc_hqx(data[:-2], 0xFFFF) != int.from_bytes(data[-2:], "little"):
        raise ValueError("Invalid trace message or CRC")
    return data[:-2]


def parse_config(message):
    if len(message) < 2:
        raise ValueError("Invalid configuration response")
    config = {}
    ids = set()
    offset = 2
    for _ in range(message[1]):
        if offset + 3 > len(message):
            raise ValueError("Truncated configuration response")
        variable_id, value_type, length = message[offset:offset + 3]
        offset += 3
        if offset + length > len(message) or value_type not in VALUE_FORMATS:
            raise ValueError("Invalid configuration entry")
        name = message[offset:offset + length].decode("utf-8")
        offset += length
        if name in config or variable_id in ids:
            raise ValueError("Duplicate trace variable name or ID")
        ids.add(variable_id)
        config[name] = (variable_id, value_type)
    if offset != len(message):
        raise ValueError("Extra bytes in configuration response")
    return config
