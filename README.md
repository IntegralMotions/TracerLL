# TracerLL

TracerLL is a Python package with C++ extension modules for high-performance
embedded tracing control.

The native code is built with CMake through `scikit-build-core` and `pybind11`.
USB serial transport and COBS framing are implemented in C++, so Python users
read and write decoded message payloads directly. Incoming frames that exceed
the receive limit are discarded through the next delimiter to restore framing.

## Requirements

- Python 3.12 or newer (CI tests Python 3.15)
- CMake 3.16 or newer
- A C++20 compiler
- Git, used by CMake `FetchContent` to fetch
  `IntegralMotions/IntegralCommunication`

Python build dependencies are declared in `pyproject.toml` and installed by
`pip` during build isolation.

## Create A Development Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install pybind11 scikit-build-core pytest build
```

You can also install the same development dependencies through the Makefile:

```bash
make setup
```

## Build And Install

Install the package into the active environment:

```bash
python -m pip install .
```

For local development, use editable mode:

```bash
python -m pip install -e .
```

Both commands build the C++ extension modules with CMake. The build downloads
the pinned `IntegralCommunication` commit declared in `CMakeLists.txt`.

## Run Tests

After installing the package into the active environment, run:

```bash
python -m pytest tests
```

The test suite covers:

- Tracer object construction and basic API behavior
- Embedded tracer protocol parsing and subscriptions

### Virtual serial integration test

The integration test connects to the virtual trace device from
`IntegralEmbeddedTracing/examples`. Start it with `make example` in that
repository, then use its printed serial port here:

```bash
TRACER_SERIAL_TEST=1 TRACER_SERIAL_PORT=/dev/ttys016 python -m pytest -v tests/integration/test_serial_tracing.py
```

The port defaults to `/dev/ttys016`, but pseudo-terminal names can change; verify
the printed path before running the test. This test sends commands, so do not run
it against a real device or a port in use by another client. It is skipped during
normal test runs unless `TRACER_SERIAL_TEST=1` is set.

CI runs the normal test suite and a separate virtual-serial integration job on
macOS, Linux, and Windows. The integration job checks out
`IntegralMotions/IntegralEmbeddedTracing` at `main` and builds its example;
that branch must include the serial example and Windows COM-port support. On
Windows it installs a checksum-pinned com0com driver and uses a virtual COM pair.
Driver installation and physical COM-port behavior depend on the runner and
hardware respectively.

The Windows integration job also runs the native concurrency and buffer tests
against that COM pair using pyserial for the test peer. To run them locally,
install pyserial and set `TRACER_TEST_PORT_A` and `TRACER_TEST_PORT_B` to the two
ends of an unused virtual COM pair before running
`python -m pytest -v tests/test_native_concurrency.py`.

## VS Code clangd

This project uses CMake `FetchContent` for `IntegralCommunication`. Configure
CMake once so clangd can read the generated compile database:

```bash
python -m pip install pybind11 scikit-build-core
cmake --preset clangd
```

VS Code is configured to read:

```text
build/clangd/compile_commands.json
```

After regenerating the compile database, restart clangd from the VS Code command
palette with `clangd: Restart language server`.

## Build A Wheel

To build a distributable wheel:

```bash
python -m pip install build
python -m build --wheel
```

The wheel is written to `dist/`.

## Versioning

The package version is defined in one place:

```toml
[project]
version = "0.3.0"
```

Update the `version` field in `pyproject.toml` when preparing a release.
CMake reads that value during configure, so the native build and Python package
metadata stay in sync.

## Usage

### High-level trace client

`EmbeddedTracer` discovers scalar variables and starts tracing them using the
current IntegralEmbeddedTracing protocol. Its transport handles COBS; the Python
client validates protocol CRCs and interprets trace values.

```python
from TracerLL import EmbeddedTracer

tracer = EmbeddedTracer("/dev/ttyUSB0", 115200)
try:
    tracer.connect()
    print(tracer.get_variable_config())
    tracer.subscribe("current_u", lambda value: print(value))
    tracer.start_trace(["current_u", "counter"])
    # Use tracer.values["current_u"] to read the most recent value.
    tracer.unsubscribe("current_u")
    tracer.stop_trace()
finally:
    tracer.close()
```

Callbacks run on a background reader thread and should return quickly. The
client starts all discovered scalar variables by default, or the specified names
(up to 64). All discovered names remain available in the configuration; values
for unselected variables remain `None` until they have been traced. Call
`stop_trace()` and then `start_trace()` to change the selection without closing
the connection.
Array tracing is not yet supported. This is a partial replacement for
the old `MotorDriverDesktop/Tracer` client, whose legacy wire format is not
compatible with the current protocol.

### Low-level transport

```python
from TracerLL import Tracer

tracer = Tracer.Tracer("/dev/ttyUSB0", 115200)

if tracer.start():
    tracer.write_message(b"hello")
    messages = tracer.get_messages()
    tracer.stop()
```

On Windows, pass the COM port name:

```python
tracer = Tracer.Tracer("COM10", 115200)
```

`Tracer.write_message(...)` accepts `bytes` or a sequence of integer byte
values. `get_messages()` returns lists of integer byte values. Message framing
is COBS-based internally. On Linux, supported baud rates are 9600, 19200, 38400,
57600, 115200, 230400, 460800, and 921600.

The receive queue holds up to 1024 messages by default. Set the optional
`receive_queue_capacity` constructor argument to change the limit. New messages
are dropped when full; `dropped_messages()` returns a cumulative, saturating
count. The count survives stop/start, while queued messages are cleared on start.
`EmbeddedTracer.dropped_messages()` exposes the same count.

`write_message()` returns `True` only when the complete encoded frame is accepted
by the serial transport. A failure may leave a partial frame on the wire, so
further writes fail until `stop()` and `start()`; the peer may also need to be
reset or resynchronized. This does not guarantee peer delivery or processing.
The high-level client raises `OSError` when a request write fails.

Concurrent writes and start/stop calls are serialized. Writes use a one-second
deadline; `stop()` waits for an in-progress write and the reader thread. On
Windows, an OS or driver that fails to complete cancellation may delay shutdown
beyond the deadline. Coordinate shutdown with producers.

## Modules

- `TracerLL.Tracer`: USB serial tracer using COBS-framed communication.
- `TracerLL.EmbeddedTracer`: high-level embedded tracing client.
