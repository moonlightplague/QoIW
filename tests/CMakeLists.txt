add_executable(qoiw_test_byte_io unit/common/byte_io.cpp)
qoiw_configure_target(qoiw_test_byte_io)
target_link_libraries(qoiw_test_byte_io PRIVATE QOIW::qoiw)
add_test(NAME common.byte_io COMMAND qoiw_test_byte_io)

if(QOIW_CORE_SZ3)
    add_executable(qoiw_test_sz3_build cores/sz3_build.cpp)
    qoiw_configure_target(qoiw_test_sz3_build)
    target_link_libraries(qoiw_test_sz3_build PRIVATE SZ3::SZ3)
    add_test(NAME core.sz3_build COMMAND qoiw_test_sz3_build)
endif()

foreach(part reader writer)
    add_executable(qoiw_test_container_${part} unit/container/${part}.cpp)
    qoiw_configure_target(qoiw_test_container_${part})
    target_compile_definitions(qoiw_test_container_${part} PRIVATE
        QOIW_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/golden/m0-core.qoiw")
    add_test(NAME container.${part} COMMAND qoiw_test_container_${part})
endforeach()
target_link_libraries(qoiw_test_container_reader PRIVATE qoiw_decode)
target_link_libraries(qoiw_test_container_writer PRIVATE QOIW::qoiw)

add_library(qoiw_mock_cores STATIC ../src/core/adapters/mock.cpp)
qoiw_configure_target(qoiw_mock_cores)
target_link_libraries(qoiw_mock_cores PRIVATE QOIW::qoiw)

find_package(Threads REQUIRED)
add_executable(qoiw_test_mock_roundtrip cores/mock_roundtrip.cpp)
qoiw_configure_target(qoiw_test_mock_roundtrip)
target_link_libraries(qoiw_test_mock_roundtrip PRIVATE qoiw_mock_cores QOIW::qoiw Threads::Threads)
target_compile_definitions(qoiw_test_mock_roundtrip PRIVATE
    QOIW_MOCK_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/m0-mock-quantizer.qoiw"
    QOIW_LOSSLESS_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/golden/m1-lossless.qoiw")
add_test(NAME core.mock_roundtrip COMMAND qoiw_test_mock_roundtrip)

add_executable(qoiw_test_expr_reference unit/expr/reference.cpp)
qoiw_configure_target(qoiw_test_expr_reference)
target_link_libraries(qoiw_test_expr_reference PRIVATE QOIW::qoiw)
add_test(NAME expr.reference COMMAND qoiw_test_expr_reference)

add_executable(qoiw_test_expr_edges unit/expr/edge_cases.cpp)
qoiw_configure_target(qoiw_test_expr_edges)
target_link_libraries(qoiw_test_expr_edges PRIVATE QOIW::qoiw Threads::Threads)
add_test(NAME expr.edges COMMAND qoiw_test_expr_edges)

add_executable(qoiw_test_lossless unit/backend/lossless.cpp)
qoiw_configure_target(qoiw_test_lossless)
target_link_libraries(qoiw_test_lossless PRIVATE QOIW::qoiw)
add_test(NAME backend.lossless COMMAND qoiw_test_lossless)

add_executable(qoiw_test_pointwise unit/backend/pointwise.cpp)
qoiw_configure_target(qoiw_test_pointwise)
target_link_libraries(qoiw_test_pointwise PRIVATE QOIW::qoiw)
add_test(NAME backend.pointwise COMMAND qoiw_test_pointwise)

add_executable(qoiw_test_pointwise_roundtrip cores/pointwise_roundtrip.cpp)
qoiw_configure_target(qoiw_test_pointwise_roundtrip)
target_link_libraries(qoiw_test_pointwise_roundtrip PRIVATE qoiw_mock_cores QOIW::qoiw)
add_test(NAME core.pointwise_roundtrip COMMAND qoiw_test_pointwise_roundtrip)

add_executable(qoiw_test_resolve unit/frontend/resolve.cpp)
qoiw_configure_target(qoiw_test_resolve)
target_link_libraries(qoiw_test_resolve PRIVATE QOIW::qoiw)
add_test(NAME frontend.resolve COMMAND qoiw_test_resolve)
