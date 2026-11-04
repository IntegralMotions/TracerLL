"""Exercise native transport using a PTY or an explicitly configured COM pair."""

import collections
from contextlib import contextmanager
import os
import select
import threading
import time

import pytest

pytestmark = pytest.mark.skipif(
    os.name != "posix" and not (os.name == "nt" and os.environ.get("TRACER_TEST_PORT_A")
                                and os.environ.get("TRACER_TEST_PORT_B")),
    reason="Requires a POSIX pseudo-terminal or a configured Windows COM pair",
)


@contextmanager
def open_serial_pair():
    """Yield the tracer port and a peer exposing read/write on either platform."""
    if os.name == "nt":
        import serial

        peer = serial.Serial(os.environ["TRACER_TEST_PORT_B"], 115200, timeout=0.02, write_timeout=3)
        try:
            peer.reset_input_buffer()
            peer.reset_output_buffer()
            yield os.environ["TRACER_TEST_PORT_A"], peer
        finally:
            peer.close()
    else:
        import pty

        master, slave = pty.openpty()

        class PtyPeer:
            def read(self, size):
                return os.read(master, size) if select.select([master], [], [], 0.02)[0] else b""

            def write(self, data):
                return os.write(master, data)

        try:
            yield os.ttyname(slave), PtyPeer()
        finally:
            os.close(slave)
            os.close(master)


def decode_cobs(frame):
    result = bytearray()
    offset = 0
    while offset < len(frame):
        code = frame[offset]
        assert code and offset + code <= len(frame)
        offset += 1
        result.extend(frame[offset:offset + code - 1])
        offset += code - 1
        if code != 255 and offset < len(frame):
            result.append(0)
    return bytes(result)


@pytest.fixture
def serial_pair():
    from TracerLL import Tracer

    with open_serial_pair() as (port, peer):
        tracer = Tracer.Tracer(port, 115200)
        assert tracer.start()
        data = bytearray()
        done = threading.Event()

        def read_peer():
            while not done.is_set():
                data.extend(peer.read(65536))

        reader = threading.Thread(target=read_peer, daemon=True)
        reader.start()
        try:
            yield tracer, data
        finally:
            tracer.stop()
            done.set()
            reader.join(timeout=2)


def test_simultaneous_writes_preserve_complete_frames(serial_pair):
    tracer, data = serial_pair
    expected = [bytes([index, sequence]) + bytes([index + 1]) * 96
                for index in range(4) for sequence in range(50)]
    barrier = threading.Barrier(4)

    def write_group(index):
        barrier.wait(timeout=5)
        for message in expected[index * 50:(index + 1) * 50]:
            tracer.write_message(message)

    threads = [threading.Thread(target=write_group, args=(i,)) for i in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=10)
        assert not thread.is_alive(), "Writer did not finish"

    deadline = time.monotonic() + 5
    while data.count(0) < len(expected) and time.monotonic() < deadline:
        time.sleep(0.005)
    frames = bytes(data).split(b"\0")
    assert frames[-1] == b""
    assert collections.Counter(map(decode_cobs, frames[:-1])) == collections.Counter(expected)


def test_stop_and_restart_while_writing(serial_pair):
    tracer, _ = serial_pair
    running = threading.Event()
    running.set()
    started = threading.Event()

    def writer():
        started.set()
        while running.is_set():
            tracer.write_message(b"concurrent write")

    thread = threading.Thread(target=writer, daemon=True)
    thread.start()
    assert started.wait(2)
    try:
        for _ in range(20):
            tracer.stop()
            assert tracer.start()
    finally:
        running.clear()
        thread.join(timeout=5)
    assert not thread.is_alive(), "Writer did not stop"


def test_receive_queue_is_bounded_and_reports_drops():
    from TracerLL import Tracer

    with open_serial_pair() as (port, peer):
        tracer = Tracer.Tracer(port, 115200, receive_queue_capacity=4)
        try:
            assert tracer.start()
            data = b"".join(bytes([2, value, 0]) for value in range(1, 21))
            assert peer.write(data) == len(data)
            deadline = time.monotonic() + 3
            while tracer.dropped_messages() < 16 and time.monotonic() < deadline:
                time.sleep(0.005)
            assert tracer.dropped_messages() == 16
            assert tracer.get_messages() == [[1], [2], [3], [4]]
            tracer.stop()
            assert tracer.dropped_messages() == 16
        finally:
            tracer.stop()


def test_full_serial_output_times_out_and_stop_finishes():
    from TracerLL import Tracer

    with open_serial_pair() as (port, peer):
        tracer = Tracer.Tracer(port, 115200)
        try:
            assert tracer.start()
            started = time.monotonic()
            # Keep the peer open but do not drain it, so its input eventually fills.
            for _ in range(500):
                if not tracer.write_message(b"x" * 4000):
                    break
            else:
                pytest.fail("Virtual serial output did not fill")
            assert time.monotonic() - started < 5
            started = time.monotonic()
            tracer.stop()
            assert time.monotonic() - started < 2
        finally:
            tracer.stop()


def test_oversized_unterminated_frame_recovers_at_next_delimiter():
    from TracerLL import Tracer

    with open_serial_pair() as (port, peer):
        tracer = Tracer.Tracer(port, 115200)
        try:
            assert tracer.start()
            first = b"\x02\x11\x00" + b"\x01" * 4096
            assert peer.write(first) == len(first)
            time.sleep(0.02)
            second = b"\x01" * 100 + b"\x00\x02\x2a\x00"
            assert peer.write(second) == len(second)
            messages = []
            deadline = time.monotonic() + 3
            while len(messages) < 2 and time.monotonic() < deadline:
                messages.extend(tracer.get_messages())
                time.sleep(0.005)
            assert messages == [[0x11], [0x2A]]
        finally:
            tracer.stop()


@pytest.mark.parametrize("delay", [0, 25, 1000])
def test_receive_with_configured_idle_delay(delay):
    from TracerLL import Tracer

    with open_serial_pair() as (port, peer):
        tracer = Tracer.Tracer(port, 115200, idle_read_delay_us=delay)
        try:
            assert tracer.start()
            time.sleep(0.02)
            assert peer.write(b"\x02\x2a\x00") == 3
            deadline = time.monotonic() + 2
            messages = []
            while not messages and time.monotonic() < deadline:
                messages.extend(tracer.get_messages())
                time.sleep(0.001)
            assert messages == [[42]]
        finally:
            tracer.stop()


def test_stop_interrupts_long_idle_delay():
    from TracerLL import Tracer

    with open_serial_pair() as (port, _):
        tracer = Tracer.Tracer(port, 115200, idle_read_delay_us=1000000)
        try:
            assert tracer.start()
            time.sleep(0.02)
            started = time.monotonic()
            tracer.stop()
            assert time.monotonic() - started < 0.5, "Shutdown waited for the one-second idle delay"
        finally:
            tracer.stop()
