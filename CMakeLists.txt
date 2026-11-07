# 文件职责：构建 Win32 桌面程序，并把运行所需 PNG 复制到 exe 旁。
# 图标保持为独立文件；新增图片无需修改资源脚本或编译成二进制资源。
cmake_minimum_required(VERSION 3.24)
project(desktop_orb VERSION 0.1.0 LANGUAGES CXX)

add_executable(desktop-orb WIN32
    src/main.cpp
    src/app.cpp
    src/platform/monitor.cpp
    src/platform/asset_path.cpp
    src/platform/single_instance.cpp
    src/platform/settings.cpp
    src/platform/window_catalog.cpp
    src/platform/thumbnail_manager.cpp
    src/ui/window_base.cpp
    src/ui/drawing.cpp
    src/ui/icon_image.cpp
    src/ui/orb_artwork.cpp
    src/ui/orb_window.cpp
    src/ui/feature_menu.cpp
    src/ui/switcher_panel.cpp
)

target_include_directories(desktop-orb PRIVATE include)
target_compile_features(desktop-orb PRIVATE cxx_std_20)
target_compile_definitions(desktop-orb PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(desktop-orb PRIVATE dwmapi gdi32 user32 shell32 gdiplus)

# 核心逻辑：收集所有 PNG 并在每次构建时复制，新增图片无需修改此文件。
file(GLOB ICON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/icon/*.png")
add_custom_target(copy_icons ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:desktop-orb>/assets/icon"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ICON_FILES} "$<TARGET_FILE_DIR:desktop-orb>/assets/icon"
    DEPENDS desktop-orb ${ICON_FILES}
    VERBATIM
)

if(MSVC)
    target_compile_options(desktop-orb PRIVATE /W4 /permissive- /utf-8)
endif()
