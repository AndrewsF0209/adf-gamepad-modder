find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ADF_PROXY_TARGETS winmm dinput8 version)

foreach(adf_target IN LISTS ADF_PROXY_TARGETS)
  set(adf_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${adf_target}")
  file(MAKE_DIRECTORY "${adf_gen_dir}")

  execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/gen_proxy.py"
            --target "${adf_target}.dll"
            --arch "${ADF_ARCH_NAME}"
            --outdir "${adf_gen_dir}"
    RESULT_VARIABLE adf_gen_result
    COMMAND_ERROR_IS_FATAL ANY
  )

  add_library(adf_proxy_${adf_target} SHARED
    dllmain.cpp
    resolver.cpp
    "${adf_gen_dir}/proxy_stub.asm"
  )

  target_include_directories(adf_proxy_${adf_target} PRIVATE
    "${adf_gen_dir}"
  )
  target_link_libraries(adf_proxy_${adf_target} PRIVATE adfmapper_core)

  set_target_properties(adf_proxy_${adf_target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${adf_target}"
    SUFFIX ".dll"
    LINK_FLAGS "/DEF:\"${adf_gen_dir}/proxy.def\""
  )
endforeach()
