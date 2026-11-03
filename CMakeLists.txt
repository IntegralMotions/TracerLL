cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml" TRACERLL_PYPROJECT)
string(REGEX MATCH "\nversion[ \t]*=[ \t]*\"([^\"]+)\"" TRACERLL_VERSION_MATCH "\n${TRACERLL_PYPROJECT}")
if(NOT TRACERLL_VERSION_MATCH)
    message(FATAL_ERROR "Could not read project.version from pyproject.toml")
endif()
set(TRACERLL_VERSION "${CMAKE_MATCH_1}")

project(
    TracerLL
    VERSION ${TRACERLL_VERSION}
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    IntegralCommunication
    GIT_REPOSITORY https://github.com/IntegralMotions/IntegralCommunication.git
    GIT_TAG 858bf6cb2095cd74fd0601b9beeeefc9809fb57b
)

set(INTEGRALCOMM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(IntegralCommunication)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(Tracer MODULE
    src/tracer_module.cpp
    src/tracer.cpp
    src/usb_communication.cpp
)
target_compile_definitions(Tracer PRIVATE $<$<PLATFORM_ID:Windows>:NOMINMAX>)
target_link_libraries(Tracer PRIVATE
    IntegralCommunication::IntegralCommunication
)
install(TARGETS Tracer LIBRARY DESTINATION TracerLL)
