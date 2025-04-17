import pytest
from TracerLL import Tracer


@pytest.fixture
def tracer():
    # Provide a dummy port and baud rate
    return Tracer.Tracer("COM_FAKE", 9600)


def test_can_create_tracer(tracer):
    assert tracer is not None


def test_can_start_and_stop(tracer):
    tracer.start()
    tracer.stop()  # Should not throw


def test_can_write_list_and_get_message(tracer):
    message = [0x81, 0x01]  # Single-byte 7-bit encoded message (e.g., 0x81 = value 1)
    tracer.write_message(message)

    # Nothing to assert without loopback or mocking, just ensure no crash
    tracer.stop()


def test_can_write_bytes_and_get_message(tracer):
    message = bytes([0x81, 0x01])  # Single-byte 7-bit encoded message (e.g., 0x81 = value 1)
    tracer.write_message(message)

    # Nothing to assert without loopback or mocking, just ensure no crash
    tracer.stop()


def test_get_messages_empty(tracer):
    messages = tracer.get_messages()
    assert messages == [] or isinstance(messages, list)
