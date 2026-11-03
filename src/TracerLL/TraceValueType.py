"""Scalar value types defined by the embedded tracing protocol."""

from enum import IntEnum


class TraceValueType(IntEnum):
    UNKNOWN = 0x00
    CHAR = 0x01
    UCHAR = 0x02
    INT8 = 0x03
    UINT8 = 0x04
    INT16 = 0x05
    UINT16 = 0x06
    INT32 = 0x07
    UINT32 = 0x08
    INT64 = 0x09
    UINT64 = 0x0A
    FLOAT = 0x0B
    DOUBLE = 0x0C


VALUE_FORMATS = {
    TraceValueType.CHAR: "b", TraceValueType.UCHAR: "B", TraceValueType.INT8: "b", TraceValueType.UINT8: "B",
    TraceValueType.INT16: "h", TraceValueType.UINT16: "H", TraceValueType.INT32: "i", TraceValueType.UINT32: "I",
    TraceValueType.INT64: "q", TraceValueType.UINT64: "Q", TraceValueType.FLOAT: "f", TraceValueType.DOUBLE: "d",
}
