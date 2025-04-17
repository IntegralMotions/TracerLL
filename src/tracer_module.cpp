#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "tracer.h"

namespace py = pybind11;

PYBIND11_MODULE(Tracer, m)
{
    py::class_<Tracer>(m, "Tracer", R"pbdoc(
        Tracer(port: str, baud: int)

        A class that manages serial communication and message decoding using 7-bit encoding.
        )pbdoc")
        .def(py::init<const std::string &, uint32_t>(), py::arg("port"), py::arg("baud"))
        .def("start", &Tracer::start, R"pbdoc(
            start() -> boolean

            Open the serial port and start reading in a background thread.
        )pbdoc")
        .def("stop", &Tracer::stop, R"pbdoc(
            stop() -> None

            Stop the reading thread and close the serial port.
        )pbdoc")
        .def("get_messages", &Tracer::getMessages, R"pbdoc(
            get_messages() -> list[bytes]

            Return a list of complete messages received since the last call.
        )pbdoc")
        .def("write_message", [](Tracer &self, py::object msg)
             {
                 std::vector<uint8_t> buffer;

                 if (py::isinstance<py::bytes>(msg))
                 {
                     std::string s = msg.cast<std::string>();
                     buffer.assign(s.begin(), s.end());
                 }
                 else
                 {
                     buffer = msg.cast<std::vector<uint8_t>>();
                 }

                 self.writeMessage(buffer); }, py::arg("message"), R"pbdoc(
            write_message(message: bytes | list[int]) -> None

            Encode and send the given message over the serial port.
        )pbdoc",
             py::call_guard<py::gil_scoped_release>());
}
