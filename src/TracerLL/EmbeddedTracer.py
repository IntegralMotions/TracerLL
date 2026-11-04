"""Client for the IntegralEmbeddedTracing protocol.

COBS framing is handled by the native TracerLL transport. This module only
handles protocol payloads, their CRC and typed trace values.
"""

import logging
import struct
import threading
import time
from collections import deque

from .Tracer import Tracer as NativeTracer
from .TraceMessageType import TraceMessageType as MessageType
from .TraceProtocol import checked_message, packet, parse_config
from .TraceValueType import VALUE_FORMATS
from .TraceValues import TraceValues

_LOG = logging.getLogger(__name__)


class EmbeddedTracer:
    """Discover variables, start streaming, and expose values and callbacks.

    Callbacks run on the reader thread and must not block. The transport is
    deliberately kept separate from this protocol layer.
    """

    def __init__(self, port, baudrate, receive_queue_capacity=1024, idle_read_delay_us=100):
        self.embedded_tracer = NativeTracer(port, baudrate, receive_queue_capacity, idle_read_delay_us)
        self.values = TraceValues()
        self._callbacks = {}
        self._callback_lock = threading.Lock()
        self._running = threading.Event()
        self._thread = None
        self._pending = deque()
        self._mapping = {}
        self._connected = False
        self._config = None

    def _send(self, payload):
        if not self.embedded_tracer.write_message(packet(payload)):
            raise OSError("Failed to write the complete trace frame")

    def _receive(self, message_type, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for raw in self.embedded_tracer.get_messages():
                try:
                    self._pending.append(checked_message(raw))
                except ValueError:
                    _LOG.warning("Discarding trace message with invalid CRC or length")
            while self._pending:
                message = self._pending.popleft()
                if message[0] == message_type:
                    return message
                if message[0] != MessageType.TRACE_DATA:
                    raise ValueError("Unexpected trace message type: {}".format(message[0]))
            time.sleep(0.001)
        raise TimeoutError("Timed out waiting for trace message {}".format(message_type))

    def connect(self):
        """Open the serial connection without sending protocol messages."""
        if self._connected:
            return True
        if not self.embedded_tracer.start():
            raise ConnectionError("Could not open trace serial port")
        self._connected = True
        self._config = None
        self._pending.clear()
        self.values._set_config({})
        return True

    def get_variable_config(self, timeout=5.0):
        """Request scalar configuration and return the discovered variable names."""
        if not self._connected:
            raise RuntimeError("Connect before requesting configuration")
        if self._running.is_set():
            raise RuntimeError("Stop tracing before requesting configuration")
        self._send(bytes([MessageType.GET_CONFIG_REQUEST]))
        config = parse_config(self._receive(MessageType.GET_CONFIG_RESPONSE, timeout))
        self._config = config
        self.values._set_config(config)
        return list(config)

    def start_trace(self, variables=None, timeout=5.0):
        """Start tracing selected names, or all discovered scalar variables."""
        if not self._connected or self._config is None:
            raise RuntimeError("Connect and request configuration before starting tracing")
        if self._running.is_set():
            raise RuntimeError("Stop tracing before starting another trace")
        config = self._config
        if variables is None:
            names = list(config)
        else:
            if isinstance(variables, (str, bytes)):
                raise TypeError("variables must be a sequence of variable names")
            names = list(variables)
            if any(not isinstance(name, str) for name in names):
                raise TypeError("variable names must be strings")
            if len(set(names)) != len(names):
                raise ValueError("Duplicate variable names")
            missing = [name for name in names if name not in config]
            if missing:
                raise ValueError("Unknown trace variables: {}".format(", ".join(missing)))
        if not 1 <= len(names) <= 64:
            raise ValueError("Select between 1 and 64 scalar trace variables")
        ids = [config[name][0] for name in names]
        self._send(bytes([MessageType.START_TRACE_REQUEST, len(ids)] + ids))
        try:
            response = self._receive(MessageType.START_TRACE_RESPONSE, timeout)
            expected_size = 3 + 2 * len(ids)
            if len(response) != expected_size or response[1] != len(ids) or response[-1] != 0:
                raise ValueError("Trace start request was rejected or malformed")
            by_id = {config[name][0]: (name, config[name][1]) for name in names}
            mapping = {}
            mapped_ids = set()
            for offset in range(2, len(response) - 1, 2):
                variable_id, trace_id = response[offset:offset + 2]
                if variable_id not in by_id or variable_id in mapped_ids or trace_id >= 64 or trace_id in mapping:
                    raise ValueError("Invalid trace variable mapping")
                mapped_ids.add(variable_id)
                mapping[trace_id] = by_id[variable_id]
            self._mapping = mapping
            self._running.set()
            self._thread = threading.Thread(target=self._read_loop, name="TracerLL-reader", daemon=True)
            self._thread.start()
            return True
        except Exception:
            self._running.clear()
            try:
                self._send(bytes([MessageType.STOP_TRACE_EVENT]))
            except OSError:
                _LOG.warning("Could not send stop after trace setup failed")
            raise

    def _process_update(self, message):
        if len(message) < 2:
            raise ValueError("Invalid trace update")
        offset = 2
        updates = []
        for _ in range(message[1]):
            if offset >= len(message):
                raise ValueError("Truncated trace update")
            header = message[offset]
            offset += 1
            size = 1 << (header >> 6)
            trace_id = header & 0x3F
            if trace_id not in self._mapping or offset + size > len(message):
                raise ValueError("Invalid trace update entry")
            name, value_type = self._mapping[trace_id]
            fmt = "<" + VALUE_FORMATS[value_type]
            if struct.calcsize(fmt) != size:
                raise ValueError("Trace value size does not match configuration")
            value = struct.unpack_from(fmt, message, offset)[0]
            updates.append((name, value))
            offset += size
        if offset != len(message):
            raise ValueError("Extra bytes in trace update")
        for name, value in updates:
            self.values._set(name, value)
            with self._callback_lock:
                callback = self._callbacks.get(name)
            if callback is not None:
                try:
                    callback(value)
                except Exception:
                    _LOG.exception("Trace callback failed for %s", name)

    def _read_loop(self):
        while self._running.is_set():
            messages = list(self._pending)
            self._pending.clear()
            for raw in self.embedded_tracer.get_messages():
                try:
                    messages.append(checked_message(raw))
                except ValueError:
                    _LOG.warning("Discarding trace message with invalid CRC or length")
            for message in messages:
                try:
                    if message[0] == MessageType.TRACE_DATA:
                        self._process_update(message)
                    else:
                        _LOG.warning("Unexpected trace message type: %s", message[0])
                except (ValueError, UnicodeError):
                    _LOG.exception("Invalid trace message")
            # Drain consecutive batches immediately; back off only when idle.
            if not messages:
                time.sleep(0.001)

    def subscribe(self, name, callback):
        if self.values.get_id_from_name(name) is None:
            raise KeyError(name)
        if not callable(callback):
            raise TypeError("callback must be callable")
        with self._callback_lock:
            self._callbacks[name] = callback

    def unsubscribe(self, name):
        with self._callback_lock:
            del self._callbacks[name]

    def dropped_messages(self):
        """Return the cumulative number of native receive-queue overflows."""
        return self.embedded_tracer.dropped_messages()

    def stop_trace(self):
        """Stop streaming while keeping the connection and subscriptions open."""
        if not self._connected:
            return
        if self._thread is threading.current_thread():
            raise RuntimeError("Cannot stop tracing from a trace callback")
        self._running.clear()
        if self._thread is not None:
            self._thread.join()
            self._thread = None
        self._send(bytes([MessageType.STOP_TRACE_EVENT]))
        self._pending.clear()

    def close(self):
        """Stop an active trace and close the serial connection."""
        if self._thread is threading.current_thread():
            raise RuntimeError("Cannot close the tracer from a trace callback")
        if self._connected:
            try:
                if self._running.is_set():
                    self.stop_trace()
            finally:
                self.embedded_tracer.stop()
                self._connected = False
                self._config = None
                self._pending.clear()
