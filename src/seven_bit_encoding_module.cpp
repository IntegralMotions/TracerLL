#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "seven_bit_encoding.h"

static PyObject *py_get_encoded_size(PyObject *, PyObject *args)
{
    unsigned int value;
    if (!PyArg_ParseTuple(args, "I", &value))
        return NULL;
    size_t size = SevenBitEncoding::getEncodedSize(value);
    return PyLong_FromSize_t(size);
}

static PyObject *py_encode_value(PyObject *, PyObject *args)
{
    unsigned int value;
    if (!PyArg_ParseTuple(args, "I", &value))
        return NULL;
    size_t size = SevenBitEncoding::getEncodedSize(value);
    uint8_t *buffer = new uint8_t[size];
    SevenBitEncoding::encodeValue(value, buffer);
    PyObject *result = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(buffer), size);
    delete[] buffer;
    return result;
}

static PyObject *py_decode_value(PyObject *, PyObject *args)
{
    const char *input;
    Py_ssize_t input_length;
    if (!PyArg_ParseTuple(args, "y#", &input, &input_length))
        return NULL;
    size_t consumed = 0;
    uint32_t value = SevenBitEncoding::decodeValue(reinterpret_cast<const uint8_t *>(input), input_length, consumed);
    return Py_BuildValue("Ik", value, consumed);
}

static PyObject *py_get_encoded_buffer_size(PyObject *, PyObject *args)
{
    size_t buffer_length;
    if (!PyArg_ParseTuple(args, "n", &buffer_length))
        return NULL;
    size_t size = SevenBitEncoding::getEncodedBufferSize(buffer_length);
    return PyLong_FromSize_t(size);
}

static PyObject *py_encode_buffer(PyObject *, PyObject *args)
{
    const char *input;
    Py_ssize_t input_length;
    if (!PyArg_ParseTuple(args, "y#", &input, &input_length))
        return NULL;
    size_t out_size = SevenBitEncoding::getEncodedBufferSize(input_length);
    uint8_t *out_buffer = new uint8_t[out_size];
    size_t actual_size = SevenBitEncoding::encodeBuffer(reinterpret_cast<const uint8_t *>(input), input_length, out_buffer);
    PyObject *result = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(out_buffer), actual_size);
    delete[] out_buffer;
    return result;
}

static PyObject *py_decode_buffer(PyObject *, PyObject *args)
{
    const char *input;
    Py_ssize_t input_length;
    if (!PyArg_ParseTuple(args, "y#", &input, &input_length))
        return NULL;
    uint8_t *out_buffer = new uint8_t[input_length];
    size_t actual_size = SevenBitEncoding::decodeBuffer(reinterpret_cast<const uint8_t *>(input), input_length, out_buffer, input_length);
    PyObject *result = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(out_buffer), actual_size);
    delete[] out_buffer;
    return result;
}

static PyObject *py_is_last_byte(PyObject *, PyObject *args)
{
    unsigned int byte;
    if (!PyArg_ParseTuple(args, "I", &byte))
        return NULL;
    bool result = SevenBitEncoding::isLastByte(static_cast<uint8_t>(byte));
    return result ? Py_True : Py_False;
}

static PyObject *py_left_mask(PyObject *, PyObject *args)
{
    unsigned int n;
    if (!PyArg_ParseTuple(args, "I", &n))
        return NULL;
    uint8_t mask = SevenBitEncoding::leftMask(static_cast<uint8_t>(n));
    return PyLong_FromUnsignedLong(mask);
}

static PyMethodDef SevenBitEncodingMethods[] = {
    {"get_encoded_size", py_get_encoded_size, METH_VARARGS, "Return the number of bytes needed to encode the given value using 7-bit encoding."},
    {"encode_value", py_encode_value, METH_VARARGS, "Encode a value using 7-bit encoding."},
    {"decode_value", py_decode_value, METH_VARARGS, "Decode a 7-bit encoded value."},
    {"get_encoded_buffer_size", py_get_encoded_buffer_size, METH_VARARGS, "Return the encoded buffer size for a given input size."},
    {"encode_buffer", py_encode_buffer, METH_VARARGS, "Encode a buffer using 7-bit encoding."},
    {"decode_buffer", py_decode_buffer, METH_VARARGS, "Decode a 7-bit encoded buffer."},
    {"is_last_byte", py_is_last_byte, METH_VARARGS, "Return whether or not the given byte is the last byte of a 7-bit encoded value."},
    {"left_mask", py_left_mask, METH_VARARGS, "Compute a left mask for n bits."},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef seven_bit_encoding_module = {
    PyModuleDef_HEAD_INIT,
    "TracerLL.SevenBitEncoding",
    "Submodule providing 7-bit encoding functions.",
    -1,
    SevenBitEncodingMethods};

PyMODINIT_FUNC PyInit_SevenBitEncoding(void)
{
    return PyModule_Create(&seven_bit_encoding_module);
}
