import os
from setuptools import setup, find_packages, Extension


def get_version_from_toml():
    with open("pyproject.toml", "r") as f:
        for line in f:
            if line.strip().startswith("version ="):
                version_line = line.strip().split("=")[1].strip()
                return version_line.strip('"')


VERSION = get_version_from_toml()

with open("src/version.h", "w") as f:
    f.write(f'#define TRACERLL_VERSION "{VERSION}"\n')

if not os.path.exists("src/TracerLL"):
    os.makedirs("src/TracerLL")

tracerll_module = Extension(
    "TracerLL",
    sources=["src/tracerll_module.cpp", "src/seven_bit_encoding_module.cpp", "src/seven_bit_encoding.cpp"],
    include_dirs=["."],
    language="c++",
)

seven_bit_encoding_module = Extension(
    "TracerLL.SevenBitEncoding",
    sources=["src/seven_bit_encoding_module.cpp", "src/seven_bit_encoding.cpp"],
    include_dirs=["."],
    language="c++",
)

setup(
    name="TracerLL",
    version=VERSION,
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    ext_modules=[tracerll_module, seven_bit_encoding_module],
)
