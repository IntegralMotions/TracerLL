#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "version.h"
#include "seven_bit_encoding_module.h"

static PyMethodDef TracerLLMethods[] = {
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef tracerllmodule = {
    PyModuleDef_HEAD_INIT,
    "TracerLL",
    "Module containing submodules for low-level tracing.",
    -1,
    TracerLLMethods};

PyMODINIT_FUNC PyInit_TracerLL(void)
{
    PyObject *m = PyModule_Create(&tracerllmodule);
    if (!m)
    {
        PyErr_Print();
        return NULL;
    }

    PyObject *submod = PyInit_SevenBitEncoding();
    if (!submod)
    {
        PyErr_Print();
        Py_DECREF(m);
        return NULL;
    }

    if (PyModule_AddObject(m, "SevenBitEncoding", submod) < 0)
    {
        PyErr_Print();
        Py_DECREF(submod);
        Py_DECREF(m);
        return NULL;
    }

    if (PyModule_AddStringConstant(m, "__version__", TRACERLL_VERSION) < 0)
    {
        PyErr_Print();
        return NULL;
    }

    return m;
}
