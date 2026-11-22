function(qoiw_add_sz3)
    set(source_dir "${QoIW_SOURCE_DIR}/SZ3")
    if(NOT EXISTS "${source_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing SZ3 at ${source_dir}. Run git submodule update --init SZ3.")
    endif()
    set(SZ3_INSTALL OFF)
    set(BUILD_SZ3_BINARY OFF)
    set(BUILD_MDZ OFF)
    set(BUILD_PARAVIEW_PLUGIN OFF)
    set(BUILD_H5Z_FILTER OFF)
    add_subdirectory("${source_dir}" "${QoIW_BINARY_DIR}/SZ3" EXCLUDE_FROM_ALL)
    # The dependency's headers are outside QoIW's warning policy.
    set_property(TARGET SZ3core APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        "$<TARGET_PROPERTY:SZ3core,INTERFACE_INCLUDE_DIRECTORIES>")
endfunction()

qoiw_add_sz3()

# Each adapter supplies a matching header and a qoiw::<name>_core() factory.
function(qoiw_generate_builtin_cores output)
    set(adapter_includes "")
    set(adapter_factories "")
    foreach(source IN LISTS ARGN)
        get_filename_component(name "${source}" NAME_WE)
        string(APPEND adapter_includes "#include \"core/adapters/${name}.hpp\"\n")
        string(APPEND adapter_factories "    ${name}_core,\n")
    endforeach()
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/BuiltinCores.hpp.in" "${output}" @ONLY)
endfunction()
