# TracerLL

TracerLL is a Python package with C++ extension modules for high-performance
embedded tracing control.

The native code is built with CMake through `scikit-build-core` and `pybind11`.
USB serial transport is implemented in C++ as a `Communication` adapter, then
wrapped with `IntegralCommunication::CobsEncodedCommunication` so Python users
read and write decoded message payloads directly.

## Requirements

- Python 3.9 or newer
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
python -m pip install pytest
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

- COBS encoding Python bindings
- Seven-bit encoding compatibility bindings
- Tracer object construction and basic API behavior

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
version = "0.2.7"
```

Update the `version` field in `pyproject.toml` when preparing a release.
CMake reads that value during configure, so the native build and Python package
metadata stay in sync.

## Usage

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
values. Message framing is COBS-based internally.

## Modules

- `TracerLL.Tracer`: USB serial tracer using COBS-framed communication.
- `TracerLL.CobsEncoding`: Python access to COBS encode/decode helpers.
- `TracerLL.SevenBitEncoding`: compatibility access to seven-bit encode/decode
  helpers from `IntegralCommunication`.
