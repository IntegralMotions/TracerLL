"""Manual integration test against a running TracingSerialDevice.

Set TRACER_SERIAL_TEST=1 to run against the example device.
"""

import binascii
import collections
import math
import os
import struct
import time

import pytest


PORT = os.environ.get("TRACER_SERIAL_PORT", "/dev/ttys016")
pytestmark = pytest.mark.skipif(
    os.environ.get("TRACER_SERIAL_TEST") != "1",
    reason="Set TRACER_SERIAL_TEST=1 and start the virtual trace device",
)


def packet(payload):
    return payload + binascii.crc_hqx(payload, 0xFFFF).to_bytes(2, "little")


def checked_message(raw):
    data = bytes(raw)
    assert len(data) >= 3, f"Message too short: {data.hex()}"
    assert binascii.crc_hqx(data[:-2], 0xFFFF) == int.from_bytes(data[-2:], "little"), data.hex()
    return data[:-2]


def receive(tracer, pending, expected_type, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pending.extend(checked_message(message) for message in tracer.get_messages())
        while pending:
            message = pending.popleft()
            if message[0] == expected_type:
                return message
            # A previous client may have left the device tracing. Frames already
            # in transit can arrive after our stop request during setup.
            if message[0] == 0x07 and expected_type in (0x02, 0x06):
                continue
            pytest.fail(f"Unexpected message type {message[0]:#x} while waiting for {expected_type:#x}")
        time.sleep(0.001)
    pytest.fail(f"Timed out waiting for message type {expected_type:#x} on {PORT}")


def test_tracerll_receives_motor_currents():
    tracer_module = pytest.importorskip("TracerLL.Tracer", reason="Install TracerLL to run this integration test")
    tracer = tracer_module.Tracer(PORT, 115200)
    assert tracer.start(), f"Could not open {PORT}; start the example and use its printed port"
    pending = collections.deque()
    started = False
    try:
        tracer.write_message(packet(bytes([0x08])))
        tracer.write_message(packet(bytes([0x01])))
        config = receive(tracer, pending, 0x02)
        assert config[1] == 4
        variables = {}
        offset = 2
        for _ in range(config[1]):
            assert offset + 3 <= len(config)
            variable_id, value_type, length = config[offset:offset + 3]
            offset += 3
            assert offset + length <= len(config)
            variables[config[offset:offset + length].decode("ascii")] = (variable_id, value_type)
            offset += length
        assert offset == len(config)
        assert variables == {
            "current_u": (0, 0x0B),
            "current_v": (1, 0x0B),
            "current_w": (2, 0x0B),
            "counter": (3, 0x04),
        }

        tracer.write_message(packet(bytes([0x05, 4, 0, 1, 2, 3])))
        started = True
        assert receive(tracer, pending, 0x06) == bytes([0x06, 4, 0, 0, 1, 1, 2, 2, 3, 3, 0])

        seen = set()
        counter_values = set()
        update_count = 0
        stream_end = time.monotonic() + 0.1
        while time.monotonic() < stream_end:
            update = receive(tracer, pending, 0x07, timeout=1.0)
            update_count += 1
            offset = 2
            for _ in range(update[1]):
                assert offset < len(update)
                header = update[offset]
                offset += 1
                variable_id = header & 0x3F
                size = 1 << (header >> 6)
                assert variable_id < 4 and size == (4 if variable_id < 3 else 1)
                assert offset + size <= len(update)
                if variable_id < 3:
                    current = struct.unpack_from("<f", update, offset)[0]
                    assert math.isfinite(current) and abs(current) <= 10.01
                else:
                    counter_values.add(update[offset])
                seen.add(variable_id)
                offset += size
            assert offset == len(update)
        assert seen == {0, 1, 2, 3}, f"Missing updates for IDs {set(range(4)) - seen}"
        assert update_count > 1, "Expected multiple updates during the 0.1-second stream"
        assert len(counter_values) > 1, "Counter did not change during the 0.1-second stream"
    finally:
        if started:
            tracer.write_message(packet(bytes([0x08])))
        tracer.stop()


def test_embedded_tracer_receives_motor_currents():
    from TracerLL import EmbeddedTracer

    tracer = EmbeddedTracer(PORT, 115200)
    try:
        tracer.connect()
        tracer.stop_trace()  # Clear a trace left by an earlier client.
        tracer.get_variable_config()
        tracer.start_trace()
        assert tracer.values.get_variable_config() == ["current_u", "current_v", "current_w", "counter"]
        readings = []
        tracer.subscribe("current_u", readings.append)
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline and (len(readings) < 2 or tracer.values["counter"] is None):
            time.sleep(0.001)
        assert len(readings) >= 2
        assert all(math.isfinite(value) and abs(value) <= 10.01 for value in readings)
        assert isinstance(tracer.values["counter"], int)
        tracer.unsubscribe("current_u")
    finally:
        tracer.close()


def test_embedded_tracer_selects_variables():
    from TracerLL import EmbeddedTracer

    tracer = EmbeddedTracer(PORT, 115200)
    try:
        tracer.connect()
        assert tracer.get_variable_config() == ["current_u", "current_v", "current_w", "counter"]
        readings = []
        tracer.subscribe("current_u", readings.append)
        tracer.start_trace(["counter", "current_u"])
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and (tracer.values["counter"] is None or tracer.values["current_u"] is None):
            time.sleep(0.001)
        assert isinstance(tracer.values["counter"], int)
        assert isinstance(tracer.values["current_u"], float)
        assert tracer.values["current_v"] is None
        assert tracer.values["current_w"] is None
        assert readings
        tracer.stop_trace()
        tracer.unsubscribe("current_u")
        tracer.start_trace(["counter"])
        tracer.stop_trace()
    finally:
        tracer.close()
