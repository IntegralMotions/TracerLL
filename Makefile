BUILD_DIR  := build
GENERATOR  := Unix Makefiles
BUILD_TYPE ?= Debug
DEPENDENCY_TESTS ?= OFF
PYBIND11_DIR := $(shell python -m pybind11 --cmakedir)

.PHONY: build test test-all clean configure debug release

configure:
	mkdir -p $(BUILD_DIR)
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
	      -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	      -DINTEGRAL_BUILD_DEPENDENCY_TESTS=$(DEPENDENCY_TESTS) \
	      -DPYBIND11_FINDPYTHON=ON \
	      -Dpybind11_DIR="$(PYBIND11_DIR)"

build: configure
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -L IntegralEmbeddedTracing

test-all: DEPENDENCY_TESTS=ON
test-all: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

debug:
	$(MAKE) BUILD_TYPE=Debug build

release:
	$(MAKE) BUILD_TYPE=Release build

clean:
	rm -rf $(BUILD_DIR)