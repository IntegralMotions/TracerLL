"""Run the serial integration tests against the EmbeddedTracing example."""

import argparse
import os
import queue
import subprocess
import sys
import threading


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", help="Path to TracingSerialDevice")
    parser.add_argument("--device-port", help="Windows COM port for the device end")
    parser.add_argument("--host-port", help="Windows COM port for the tracer end")
    args = parser.parse_args()
    if bool(args.device_port) != bool(args.host_port):
        parser.error("--device-port and --host-port must be provided together")

    command = [args.executable]
    if args.device_port:
        command.append(args.device_port)
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1)
    lines = queue.Queue()

    def read_output():
        for line in process.stdout:
            print("device:", line, end="", flush=True)
            lines.put(line)
        lines.put(None)

    threading.Thread(target=read_output, daemon=True).start()
    try:
        try:
            line = lines.get(timeout=15)
        except queue.Empty:
            raise RuntimeError("Timed out waiting for the trace device to start")
        if line is None or not line.startswith("Serial port: "):
            raise RuntimeError("Trace device failed to start; see its output above")
        device_port = line.split(": ", 1)[1].strip()
        host_port = args.host_port or device_port
        env = os.environ.copy()
        env["TRACER_SERIAL_TEST"] = "1"
        env["TRACER_SERIAL_PORT"] = host_port
        print("Testing tracer port:", host_port, flush=True)
        result = subprocess.run([sys.executable, "-m", "pytest", "-v", "tests/integration/test_serial_tracing.py"],
                                env=env, timeout=90)
        return result.returncode
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


if __name__ == "__main__":
    sys.exit(main())
