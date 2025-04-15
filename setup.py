import os
from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext


def get_version_from_toml():
    with open("pyproject.toml", "r") as f:
        for line in f:
            if line.strip().startswith("version ="):
                return line.strip().split("=")[1].strip().strip('"')


VERSION = get_version_from_toml()

with open("src/version.h", "w") as f:
    f.write(f'#define TRACERLL_VERSION "{VERSION}"\n')

ext_modules = [
    Pybind11Extension("TracerLL", ["src/tracerll_module.cpp"], include_dirs=["src"]),
    Pybind11Extension(
        "TracerLL.SevenBitEncoding",
        ["src/seven_bit_encoding_module.cpp", "src/seven_bit_encoding.cpp"],
        include_dirs=["src"],
    ),
    Pybind11Extension(
        "TracerLL.Tracer",
        ["src/tracer_module.cpp", "src/tracer.cpp", "src/seven_bit_encoding.cpp"],
        include_dirs=["src"],
    ),
]

setup(
    name="TracerLL",
    version=VERSION,
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
