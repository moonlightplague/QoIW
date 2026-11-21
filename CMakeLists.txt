cmake_minimum_required(VERSION 3.19)
project(QoIW LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(cmake/QoiwOptions.cmake)
include(cmake/QoiwCores.cmake)

add_library(qoiw_decode STATIC src/common/byte_reader.cpp src/container/reader.cpp)
target_include_directories(qoiw_decode PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
qoiw_configure_target(qoiw_decode)

add_library(qoiw STATIC
    src/common/byte_writer.cpp
    src/container/writer.cpp
    src/expr/compiler.cpp
    src/expr/evaluate.cpp
    src/expr/jet.cpp)
add_library(QOIW::qoiw ALIAS qoiw)
qoiw_configure_target(qoiw)
target_link_libraries(qoiw PUBLIC qoiw_decode)

option(QOIW_BUILD_TESTS "Build QoIW tests" ON)
if(QOIW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
