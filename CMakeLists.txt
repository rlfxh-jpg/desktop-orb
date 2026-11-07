# 文件职责：构建 Win32 桌面程序，并把运行所需 PNG 复制到 exe 旁。
# 图标保持为独立文件；新增图片无需修改资源脚本或编译成二进制资源。
cmake_minimum_required(VERSION 3.24)
project(desktop_orb VERSION 0.1.0 LANGUAGES CXX)

add_executable(desktop-orb WIN32
    src/main.cpp
    src/app.cpp
    src/platform/common/monitor.cpp
    src/platform/common/asset_path.cpp
    src/platform/common/single_instance.cpp
    src/platform/common/settings.cpp
    src/platform/window_switcher/window_catalog.cpp
    src/platform/window_switcher/thumbnail_manager.cpp
    src/platform/screenshot/screenshot_service.cpp
    src/ui/common/window_base.cpp
    src/ui/common/drawing.cpp
    src/ui/common/icon_image.cpp
    src/ui/common/orb_artwork.cpp
    src/ui/common/orb_window.cpp
    src/ui/common/feature_layout.cpp
    src/ui/common/feature_menu.cpp
    src/ui/window_switcher/switcher_panel.cpp
)

target_include_directories(desktop-orb PRIVATE include)
target_compile_features(desktop-orb PRIVATE cxx_std_20)
target_compile_definitions(desktop-orb PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(desktop-orb PRIVATE dwmapi gdi32 user32 shell32 gdiplus comctl32 ole32)

# 核心逻辑：收集所有 PNG 并在每次构建时复制，新增图片无需修改此文件。
file(GLOB ICON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.png")
add_custom_target(copy_icons ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:desktop-orb>/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ICON_FILES} "$<TARGET_FILE_DIR:desktop-orb>/assets/icons"
    DEPENDS desktop-orb ${ICON_FILES}
    VERBATIM
)

if(MSVC)
    target_compile_options(desktop-orb PRIVATE /W4 /permissive- /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(feature-layout-test
        tests/feature_layout_test.cpp
        src/ui/common/feature_layout.cpp
        src/platform/common/monitor.cpp
    )
    target_include_directories(feature-layout-test PRIVATE include)
    target_compile_features(feature-layout-test PRIVATE cxx_std_20)
    target_compile_definitions(feature-layout-test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(feature-layout-test PRIVATE user32)
    if(MSVC)
        target_compile_options(feature-layout-test PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME feature-layout COMMAND feature-layout-test)
endif()
