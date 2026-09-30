add_executable(test_kbm_host WIN32 main.cpp)
target_link_libraries(test_kbm_host PRIVATE dinput8 user32 gdi32)
