cmake_minimum_required(VERSION 3.24)
project(desktop_orb VERSION 0.1.0 LANGUAGES CXX)

add_executable(desktop-orb WIN32
    src/main.cpp
    src/app.cpp
    src/platform/monitor.cpp
    src/platform/settings.cpp
    src/platform/window_catalog.cpp
    src/platform/thumbnail_manager.cpp
    src/ui/window_base.cpp
    src/ui/drawing.cpp
    src/ui/orb_window.cpp
    src/ui/feature_menu.cpp
    src/ui/switcher_panel.cpp
)

target_include_directories(desktop-orb PRIVATE include)
target_compile_features(desktop-orb PRIVATE cxx_std_20)
target_compile_definitions(desktop-orb PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(desktop-orb PRIVATE dwmapi gdi32 user32 shell32)

if(MSVC)
    target_compile_options(desktop-orb PRIVATE /W4 /permissive- /utf-8)
endif()
