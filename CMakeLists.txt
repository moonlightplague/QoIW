cmake_minimum_required(VERSION 3.19)
project(QoIW LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(cmake/QoiwOptions.cmake)
include(cmake/QoiwCores.cmake)
include(cmake/QoiwZstd.cmake)

add_library(qoiw_decode STATIC
    src/common/byte_reader.cpp
    src/container/reader.cpp
    src/backend/lossless_apply.cpp
    src/pipeline/decode_field.cpp)
target_link_libraries(qoiw_decode PRIVATE ${QOIW_ZSTD_TARGET})
target_include_directories(qoiw_decode PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
qoiw_configure_target(qoiw_decode)

add_library(qoiw STATIC
    src/backend/lossless_encode.cpp
    src/backend/pointwise.cpp
    src/common/byte_writer.cpp
    src/container/writer.cpp
    src/expr/compiler.cpp
    src/expr/evaluate.cpp
    src/expr/jet.cpp
    src/frontend/resolve.cpp
    src/frontend/prior.cpp)
add_library(QOIW::qoiw ALIAS qoiw)
qoiw_configure_target(qoiw)
target_link_libraries(qoiw PUBLIC qoiw_decode PRIVATE ${QOIW_ZSTD_TARGET})
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(qoiw PRIVATE OpenMP::OpenMP_CXX)
endif()

option(QOIW_BUILD_TESTS "Build QoIW tests" ON)
if(QOIW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
