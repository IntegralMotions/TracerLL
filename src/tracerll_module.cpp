#include <pybind11/pybind11.h>
#include "version.h"

namespace py = pybind11;

PYBIND11_MODULE(TracerLL, m)
{
    m.doc() = "Module containing submodules for low-level tracing.";
    m.attr("__version__") = TRACERLL_VERSION;
}
