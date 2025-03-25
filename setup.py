import os
from setuptools import setup, Extension

VERSION = "0.1.13"

# Write the version to a header file
with open("version.h", "w") as f:
    f.write(f'#define TRACERLL_VERSION "{VERSION}"\n')

module = Extension(
    "TracerLL",
    sources=["tracerll_module.cpp", "seven_bit_encoding.cpp"],
    language="c++",
)

setup(name="TracerLL", version=VERSION, ext_modules=[module])
