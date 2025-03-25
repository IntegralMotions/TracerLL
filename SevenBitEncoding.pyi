# SevenBitTracer.pyi

def get_encoded_size(value: int) -> int:
    """
    get_encoded_size(value: int) -> int

    Return the number of bytes needed to encode the given value using 7-bit encoding.
    """
    ...

def encode_value(value: int) -> bytes:
    """
    encode_value(value: int) -> bytes

    Encode the given value using 7-bit encoding and return the resulting bytes.
    """
    ...

def decode_value(encoded: bytes) -> tuple[int, int]:
    """
    decode_value(encoded: bytes) -> tuple[int, int]

    Decode the value from the given bytes.
    Returns a tuple of (value, bytes_consumed).
    """
    ...

def get_encoded_buffer_size(buffer_length: int) -> int:
    """
    get_encoded_buffer_size(buffer_length: int) -> int

    Return the maximum size needed to encode a buffer of the given length.
    """
    ...

def encode_buffer(input: bytes) -> bytes:
    """
    encode_buffer(input: bytes) -> bytes

    Encode the input buffer using 7-bit encoding and return the encoded bytes.
    """
    ...

def decode_buffer(input: bytes, output_length: int) -> bytes:
    """
    decode_buffer(input: bytes, output_length: int) -> bytes

    Decode the input buffer using 7-bit encoding into an output buffer of the given length,
    and return the decoded bytes.
    """
    ...

def left_mask(n: int) -> int:
    """
    left_mask(n: int) -> int

    Return a left mask with n bits (i.e. (1 << n) - 1).
    """
    ...
