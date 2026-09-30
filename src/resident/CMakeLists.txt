# Resident mapper DLL: runs the full engine inside the configurator process
# so gamepad input is mapped in real-time while the app is open.
add_library(adf_resident SHARED main.cpp)
target_link_libraries(adf_resident PRIVATE adfmapper_core)
set_target_properties(adf_resident PROPERTIES PREFIX "")
