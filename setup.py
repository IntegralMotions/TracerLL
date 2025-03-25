import os
from setuptools import setup, Extension


# Function to extract version from pyproject.toml manually
def get_version_from_toml():
    with open("pyproject.toml", "r") as f:
        for line in f:
            if line.strip().startswith("version ="):
                # Extract the version number
                version_line = line.strip().split("=")[1].strip()
                return version_line.strip('"')


VERSION = get_version_from_toml()

# Write the version to a header file
with open("version.h", "w") as f:
    f.write(f'#define TRACERLL_VERSION "{VERSION}"\n')

module = Extension(
    "TracerLL",
    sources=["tracerll_module.cpp", "seven_bit_encoding.cpp"],
    language="c++",
)

setup(name="TracerLL", version=VERSION, ext_modules=[module])
