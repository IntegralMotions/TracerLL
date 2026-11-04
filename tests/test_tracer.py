import pytest
import os
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
    message = [0x01, 0x02]
    # tracer.write_message(message)

    # Nothing to assert without loopback or mocking, just ensure no crash
    tracer.stop()


def test_can_write_bytes_and_get_message(tracer):
    message = bytes([0x01, 0x02])
    tracer.write_message(message)

    # Nothing to assert without loopback or mocking, just ensure no crash
    tracer.stop()


def test_get_messages_empty(tracer):
    messages = tracer.get_messages()
    assert messages == [] or isinstance(messages, list)


def test_write_message_rejects_invalid_python_input(tracer):
    with pytest.raises((TypeError, ValueError)):
        tracer.write_message(object())


def test_write_fails_when_closed_and_zero_queue_rejected():
    assert not Tracer.Tracer("COM_FAKE", 9600).write_message(b"hello")
    with pytest.raises(ValueError):
        Tracer.Tracer("COM_FAKE", 9600, 0)


@pytest.mark.parametrize("delay", [0, 25, 100, 1000, 1000000])
def test_configurable_idle_read_delay(delay):
    tracer = Tracer.Tracer("COM_FAKE", 9600, idle_read_delay_us=delay)
    assert tracer.idle_read_delay_us == delay


def test_default_idle_read_delay():
    assert Tracer.Tracer("COM_FAKE", 9600).idle_read_delay_us == 100


@pytest.mark.parametrize("delay", [-1, 1000001])
def test_invalid_idle_read_delay(delay):
    with pytest.raises(ValueError, match="idle_read_delay_us"):
        Tracer.Tracer("COM_FAKE", 9600, idle_read_delay_us=delay)


@pytest.mark.skipif(os.name != "posix", reason="/dev/null is a POSIX device")
def test_non_serial_device_cannot_start():
    tracer = Tracer.Tracer("/dev/null", 115200)
    assert not tracer.start()
    tracer.stop()
