add_library(adfmapper_core STATIC
  config.cpp
  curves.cpp
  scancodes.cpp
  pad_sdl.cpp
  sink_kbdmouse.cpp
  mapper.cpp
)

target_include_directories(adfmapper_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${ADF_REPO_ROOT}/third_party"
)

target_link_libraries(adfmapper_core PUBLIC ${ADF_SDL3})

# SDL3-static pulls in a number of Windows system libraries; list them
# explicitly so consumers (proxy DLL / launcher) link cleanly.
target_link_libraries(adfmapper_core PUBLIC
  user32
  gdi32
  winmm
  imm32
  setupapi
  advapi32
  shell32
  ole32
  oleaut32
  version
)
