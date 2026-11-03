"""Message identifiers for the current embedded tracing protocol."""

from enum import IntEnum


class TraceMessageType(IntEnum):
    GET_CONFIG_REQUEST = 0x01
    GET_CONFIG_RESPONSE = 0x02
    GET_ARRAY_CONFIG_REQUEST = 0x03
    GET_ARRAY_CONFIG_RESPONSE = 0x04
    START_TRACE_REQUEST = 0x05
    START_TRACE_RESPONSE = 0x06
    TRACE_DATA = 0x07
    STOP_TRACE_EVENT = 0x08
