import struct
import time

import pytest

import importlib

embedded_tracer = importlib.import_module("TracerLL.EmbeddedTracer")
from TracerLL.TraceProtocol import checked_message, packet, parse_config


class FakeTransport:
    def __init__(self, port, baud, receive_queue_capacity=1024):
        self.sent = []
        self.messages = []
        self.open = False

    def start(self):
        self.open = True
        return True

    def stop(self):
        self.open = False

    def write_message(self, data):
        message = checked_message(data)
        self.sent.append(message)
        if message == b"\x01":
            self.messages.append(packet(b"\x02\x02\x09\x0b\x07current\x07\x04\x07counter"))
        elif message == b"\x05\x02\x09\x07":
            self.messages.append(packet(b"\x06\x02\x09\x01\x07\x00\x00"))
        elif message == b"\x05\x01\x07":
            self.messages.append(packet(b"\x06\x01\x07\x00\x00"))
        return True

    def get_messages(self):
        result, self.messages = self.messages, []
        return result


def test_discovery_mapping_updates_callbacks_and_shutdown(monkeypatch):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    tracer.connect()
    tracer.get_variable_config()
    assert tracer.start_trace()
    assert tracer.values.get_variable_config() == ["current", "counter"]
    assert tracer.values.get_id_from_name("current") == 9
    values = []
    tracer.subscribe("current", values.append)
    tracer.embedded_tracer.messages.append(packet(b"\x07\x02\x81" + struct.pack("<f", 2.5) + b"\x00\x2a"))
    deadline = time.monotonic() + 1
    while (not values or tracer.values["counter"] is None) and time.monotonic() < deadline:
        time.sleep(0.001)
    assert values == [2.5]
    assert tracer.values["current"] == 2.5
    assert tracer.values["counter"] == 42
    tracer.unsubscribe("current")
    tracer.close()
    assert not tracer.embedded_tracer.open
    assert tracer.embedded_tracer.sent[-1] == b"\x08"


def test_rejects_invalid_crc_and_truncated_config():
    with pytest.raises(ValueError):
        checked_message(b"\x01\x00\x00")
    with pytest.raises(ValueError):
        parse_config(b"\x02\x01\x00")


def test_selects_only_requested_variables(monkeypatch):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    try:
        tracer.connect()
        tracer.get_variable_config()
        assert tracer.start_trace(["counter"])
        assert b"\x05\x01\x07" in tracer.embedded_tracer.sent
        assert tracer._mapping == {0: ("counter", 4)}
        assert tracer.values.get_variable_config() == ["current", "counter"]
    finally:
        tracer.close()


@pytest.mark.parametrize("names", [["missing"], ["counter", "counter"], [], "counter"])
def test_rejects_invalid_selection(monkeypatch, names):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    tracer.connect()
    tracer.get_variable_config()
    with pytest.raises((ValueError, TypeError)):
        tracer.start_trace(names)
    tracer.close()
    assert not tracer.embedded_tracer.open


def test_failed_write_closes_transport(monkeypatch):
    class FailedTransport(FakeTransport):
        def write_message(self, data):
            return False

    monkeypatch.setattr(embedded_tracer, "NativeTracer", FailedTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    tracer.connect()
    with pytest.raises(OSError, match="Failed to write"):
        tracer.get_variable_config()
    tracer.close()
    assert not tracer.embedded_tracer.open


def test_explicit_lifecycle_and_unsubscribe_after_stop(monkeypatch):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    received = []
    try:
        assert tracer.connect()
        assert tracer.embedded_tracer.sent == []
        assert tracer.get_variable_config() == ["current", "counter"]
        tracer.subscribe("counter", received.append)
        assert tracer.start_trace(["counter"])
        tracer.embedded_tracer.messages.append(packet(b"\x07\x01\x00\x2a"))
        deadline = time.monotonic() + 1
        while not received and time.monotonic() < deadline:
            time.sleep(0.001)
        assert received == [42]
        assert tracer.values["counter"] == 42
        tracer.stop_trace()
        assert tracer.embedded_tracer.open
        tracer.unsubscribe("counter")
        assert tracer.start_trace(["counter"])
        tracer.stop_trace()
        assert tracer.embedded_tracer.sent == [b"\x01", b"\x05\x01\x07", b"\x08", b"\x05\x01\x07", b"\x08"]
    finally:
        tracer.close()
    assert not tracer.embedded_tracer.open


def test_explicit_lifecycle_requires_connection_and_configuration(monkeypatch):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    with pytest.raises(RuntimeError):
        tracer.get_variable_config()
    assert tracer.connect()
    with pytest.raises(RuntimeError):
        tracer.start_trace(["counter"])
    tracer.close()


def test_bad_message_does_not_discard_valid_messages_in_batch(monkeypatch):
    monkeypatch.setattr(embedded_tracer, "NativeTracer", FakeTransport)
    tracer = embedded_tracer.EmbeddedTracer("test", 115200)
    received = []
    try:
        tracer.connect()
        tracer.embedded_tracer.messages.append(b"\x01\x00\x00")  # Invalid CRC before config.
        assert tracer.get_variable_config() == ["current", "counter"]
        tracer.subscribe("counter", received.append)
        tracer.start_trace(["counter"])
        tracer.embedded_tracer.messages.extend([
            packet(b"\x07\x01\x00\x01"),
            b"\x01\x00\x00",  # Invalid CRC.
            packet(b"\x07\x01"),  # Valid CRC but truncated update.
            packet(b"\x07\x01\x00\x02"),
        ])
        deadline = time.monotonic() + 1
        while len(received) < 2 and time.monotonic() < deadline:
            time.sleep(0.001)
        assert received == [1, 2]
    finally:
        tracer.close()
