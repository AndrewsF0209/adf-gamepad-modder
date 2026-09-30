add_executable(test_curves test_curves.cpp)
target_link_libraries(test_curves PRIVATE adfmapper_core)
add_test(NAME curves COMMAND test_curves)
