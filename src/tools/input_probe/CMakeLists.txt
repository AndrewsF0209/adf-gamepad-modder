# Discreet 4-channel input diagnostic (DirectInput8 / Raw Input / window
# messages / async key state). Deliberately links NO dinput8 import library:
# dinput8.dll is loaded from the system directory at runtime so a deployed
# proxy DLL can never hook the probe itself.
add_executable(input_probe WIN32 main.cpp)
target_link_libraries(input_probe PRIVATE user32 gdi32 comdlg32 winmm)
