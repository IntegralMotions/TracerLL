BUILD_DIR  := build
GENERATOR  := Unix Makefiles
BUILD_TYPE ?= Debug
DEPENDENCY_TESTS ?= OFF
PYTHON ?= python3
PYBIND11_DIR := $(shell $(PYTHON) -m pybind11 --cmakedir)

.PHONY: build test test-all clean configure debug release activate setup install

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

test:
	$(PYTHON) -m pytest tests

test-all: test

debug:
	$(MAKE) BUILD_TYPE=Debug build

release:
	$(MAKE) BUILD_TYPE=Release build

clean:
	rm -rf $(BUILD_DIR)

activate:
	@echo 'Run: source .venv/bin/activate'

setup: 
	python3 -m venv .venv
	.venv/bin/python -m pip install --upgrade pip
	.venv/bin/python -m pip install pybind11 scikit-build-core pytest build

install:
	$(PYTHON) -m pip install -e .
