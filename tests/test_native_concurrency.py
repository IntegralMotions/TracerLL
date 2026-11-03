"""Exercise native write and lifecycle synchronization with a local PTY."""

import collections
import os
import select
import threading
import time

import pytest

pytestmark = pytest.mark.skipif(os.name != "posix", reason="Requires a POSIX pseudo-terminal")


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
    import pty
    from TracerLL import Tracer

    master, slave = pty.openpty()
    tracer = Tracer.Tracer(os.ttyname(slave), 115200)
    assert tracer.start()
    data = bytearray()
    done = threading.Event()

    def read_master():
        while not done.is_set():
            if select.select([master], [], [], 0.02)[0]:
                data.extend(os.read(master, 65536))

    reader = threading.Thread(target=read_master, daemon=True)
    reader.start()
    try:
        yield tracer, data
    finally:
        tracer.stop()
        done.set()
        reader.join(timeout=2)
        os.close(slave)
        os.close(master)


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
    import pty
    from TracerLL import Tracer

    master, slave = pty.openpty()
    tracer = Tracer.Tracer(os.ttyname(slave), 115200, receive_queue_capacity=4)
    try:
        assert tracer.start()
        os.write(master, b"".join(bytes([2, value, 0]) for value in range(1, 21)))
        deadline = time.monotonic() + 3
        while tracer.dropped_messages() < 16 and time.monotonic() < deadline:
            time.sleep(0.005)
        assert tracer.dropped_messages() == 16
        assert tracer.get_messages() == [[1], [2], [3], [4]]
        tracer.stop()
        assert tracer.dropped_messages() == 16
    finally:
        tracer.stop()
        os.close(slave)
        os.close(master)


def test_full_serial_output_times_out_and_stop_finishes():
    import pty
    from TracerLL import Tracer

    master, slave = pty.openpty()
    tracer = Tracer.Tracer(os.ttyname(slave), 115200)
    try:
        assert tracer.start()
        started = time.monotonic()
        # Never drain the master, so the PTY output eventually fills.
        for _ in range(500):
            if not tracer.write_message(b"x" * 4000):
                break
        else:
            pytest.fail("PTY output did not fill")
        assert time.monotonic() - started < 5
        started = time.monotonic()
        tracer.stop()
        assert time.monotonic() - started < 2
    finally:
        tracer.stop()
        os.close(slave)
        os.close(master)


def test_oversized_unterminated_frame_recovers_at_next_delimiter():
    import pty
    from TracerLL import Tracer

    master, slave = pty.openpty()
    tracer = Tracer.Tracer(os.ttyname(slave), 115200)
    try:
        assert tracer.start()
        os.write(master, b"\x02\x11\x00" + b"\x01" * 4096)
        time.sleep(0.02)
        os.write(master, b"\x01" * 100 + b"\x00\x02\x2a\x00")
        messages = []
        deadline = time.monotonic() + 3
        while len(messages) < 2 and time.monotonic() < deadline:
            messages.extend(tracer.get_messages())
            time.sleep(0.005)
        assert messages == [[0x11], [0x2A]]
    finally:
        tracer.stop()
        os.close(slave)
        os.close(master)
