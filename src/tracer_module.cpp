#include "tracer.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_MODULE(Tracer, m) {
    py::class_<Tracer>(m, "Tracer", R"pbdoc(
        Tracer(port: str, baud: int)

        A class that manages serial communication and COBS message framing.
        )pbdoc")
        .def(py::init([](std::string port, uint32_t baud, size_t capacity, int64_t idleReadDelayUs) {
                 return std::make_unique<Tracer>(port, baud, capacity, idleReadDelayUs);
             }), py::arg("port"), py::arg("baud"), py::arg("receive_queue_capacity") = 1024,
             py::arg("idle_read_delay_us") = 100)
        .def_property_readonly("idle_read_delay_us", &Tracer::idleReadDelayUs,
                               "Idle read delay in microseconds; zero enables busy polling.")
        .def("start", &Tracer::start, R"pbdoc(
            start() -> boolean

            Open the serial port and start reading in a background thread.
        )pbdoc", py::call_guard<py::gil_scoped_release>())
        .def("stop", &Tracer::stop, R"pbdoc(
            stop() -> None

            Stop the reading thread and close the serial port.
        )pbdoc", py::call_guard<py::gil_scoped_release>())
        .def("get_messages", &Tracer::getMessages, R"pbdoc(
            get_messages() -> list[list[int]]

            Return a list of complete messages received since the last call.
        )pbdoc")
        .def("dropped_messages", &Tracer::droppedMessages,
             "Return the cumulative number of messages dropped because the receive queue was full.")
        .def(
            "write_message",
            [](Tracer& self, py::object msg) {
                std::vector<uint8_t> buffer;

                if (py::isinstance<py::bytes>(msg)) {
                    std::string s = msg.cast<std::string>();
                    buffer.assign(s.begin(), s.end());
                } else {
                    try {
                        buffer = msg.cast<std::vector<uint8_t>>();
                    } catch (const py::cast_error&) {
                        throw py::type_error("message must be bytes or a sequence of byte values");
                    }
                }

                py::gil_scoped_release release;
                return self.writeMessage(buffer);
            },
            py::arg("message"), R"pbdoc(
            write_message(message: bytes | list[int]) -> bool

            Return whether the entire encoded frame was accepted by the serial transport.
        )pbdoc");
}
