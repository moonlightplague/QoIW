cmake_minimum_required(VERSION 3.19)
project(QoIW LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(cmake/QoiwOptions.cmake)
include(cmake/QoiwCores.cmake)
include(cmake/QoiwZstd.cmake)

set(QOIW_CORE_SOURCES
    src/core/adapters/sz3.cpp)
qoiw_generate_builtin_cores("${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_cores.hpp" ${QOIW_CORE_SOURCES})

add_library(qoiw_decode STATIC
    src/common/byte_reader.cpp
    src/core/builtin.cpp
    src/container/reader.cpp
    src/backend/lossless_apply.cpp
    src/pipeline/decompress.cpp
    src/pipeline/decode_field.cpp
    ${QOIW_CORE_SOURCES})
target_link_libraries(qoiw_decode PRIVATE ${QOIW_ZSTD_TARGET} SZ3::SZ3)
target_include_directories(qoiw_decode PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(qoiw_decode PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
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
    src/frontend/prior.cpp
    src/frontend/prepare.cpp
    src/frontend/measure.cpp
    src/frontend/plan.cpp
    src/frontend/report.cpp
    src/frontend/sample.cpp
    src/frontend/search.cpp
    src/pipeline/compile_request.cpp
    src/pipeline/compress.cpp
    src/pipeline/verify.cpp)
add_library(QOIW::qoiw ALIAS qoiw)
qoiw_configure_target(qoiw)
target_link_libraries(qoiw PUBLIC qoiw_decode PRIVATE ${QOIW_ZSTD_TARGET})
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(qoiw PRIVATE OpenMP::OpenMP_CXX)
endif()

option(QOIW_BUILD_TOOLS "Build the QoIW command-line tool" ON)
if(QOIW_BUILD_TOOLS)
    add_executable(qoiw_cli tools/qoiw/main.cpp tools/qoiw/arguments.cpp tools/qoiw/files.cpp)
    set_target_properties(qoiw_cli PROPERTIES OUTPUT_NAME qoiw)
    qoiw_configure_target(qoiw_cli)
    target_include_directories(qoiw_cli SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11")
    target_link_libraries(qoiw_cli PRIVATE QOIW::qoiw)
endif()

option(QOIW_BUILD_BENCHMARKS "Build helpers for the QoIW benchmarks" OFF)
if(QOIW_BUILD_BENCHMARKS)
    add_executable(qoiw_regret_prior bench/regret/prior.cpp tools/qoiw/files.cpp)
    qoiw_configure_target(qoiw_regret_prior)
    target_include_directories(qoiw_regret_prior PRIVATE tools/qoiw)
    target_link_libraries(qoiw_regret_prior PRIVATE QOIW::qoiw)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OpenMP_CXX_FOUND)
        add_executable(qoiw_performance_probe bench/regret/performance.cpp tools/qoiw/files.cpp)
        qoiw_configure_target(qoiw_performance_probe)
        target_include_directories(qoiw_performance_probe PRIVATE tools/qoiw)
        target_link_libraries(qoiw_performance_probe PRIVATE QOIW::qoiw)
    endif()
endif()

option(QOIW_BUILD_TESTS "Build QoIW tests" ON)
if(QOIW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
