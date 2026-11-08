# 文件职责：构建 Win32 桌面程序，复制图标，并编译截图分析用的 Python 扩展模块。
# 截图视觉分析使用 Cython 生成的 Python 扩展、pybind11 嵌入及 OpenAI Python SDK。
cmake_minimum_required(VERSION 3.24)
project(desktop_orb VERSION 0.1.0 LANGUAGES CXX)

find_package(Python3 3.10 COMPONENTS Interpreter Development REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    RESULT_VARIABLE PYBIND11_CMAKE_RESULT
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT PYBIND11_CMAKE_RESULT EQUAL 0)
    message(FATAL_ERROR "请在当前 Python 环境执行：python -m pip install pybind11")
endif()
list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
find_package(pybind11 CONFIG REQUIRED)

# Python 源码通过 Cython 编译为当前解释器 ABI 对应的 .pyd 扩展模块。
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import Cython; print(Cython.__version__)"
    RESULT_VARIABLE CYTHON_IMPORT_RESULT
    OUTPUT_QUIET
)
if(NOT CYTHON_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR "请在当前 Python 环境执行：python -m pip install Cython")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))"
    RESULT_VARIABLE PYTHON_EXTENSION_SUFFIX_RESULT
    OUTPUT_VARIABLE PYTHON_EXTENSION_SUFFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT PYTHON_EXTENSION_SUFFIX_RESULT EQUAL 0 OR PYTHON_EXTENSION_SUFFIX STREQUAL "")
    message(FATAL_ERROR "无法从当前 Python 环境读取扩展模块文件后缀。")
endif()

set(SCREENSHOT_PYTHON_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/python/$<CONFIG>")
set(SCREENSHOT_PYTHON_MODULE "${SCREENSHOT_PYTHON_MODULE_DIR}/llm_worker${PYTHON_EXTENSION_SUFFIX}")
add_custom_command(
    OUTPUT "${SCREENSHOT_PYTHON_MODULE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SCREENSHOT_PYTHON_MODULE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env
            "DESKTOP_ORB_CYTHON_BUILD_DIR=${SCREENSHOT_PYTHON_MODULE_DIR}/generated"
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/python/build_extension.py"
            build_ext --build-lib "${SCREENSHOT_PYTHON_MODULE_DIR}"
            --build-temp "${SCREENSHOT_PYTHON_MODULE_DIR}/temp"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/python/llm_worker.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/python/build_extension.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/python"
    COMMENT "正在将截图分析模块编译为 Python 扩展 DLL"
    VERBATIM
)
add_custom_target(compile_screenshot_python DEPENDS "${SCREENSHOT_PYTHON_MODULE}")

function(add_screenshot_python_runtime target_name)
    # 每个可执行目标使用同一份扩展模块，并放到资源路径约定的 assets/python 目录。
    set(runtime_target "${target_name}-screenshot-python")
    add_custom_target(${runtime_target}
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/assets/python"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SCREENSHOT_PYTHON_MODULE}"
                "$<TARGET_FILE_DIR:${target_name}>/assets/python/llm_worker${PYTHON_EXTENSION_SUFFIX}"
        VERBATIM
    )
    add_dependencies(${runtime_target} compile_screenshot_python)
    add_dependencies(${target_name} ${runtime_target})
endfunction()

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
    src/platform/screenshot/screenshot_ai_client.cpp
    src/ui/common/window_base.cpp
    src/ui/common/drawing.cpp
    src/ui/common/icon_image.cpp
    src/ui/common/orb_artwork.cpp
    src/ui/common/orb_window.cpp
    src/ui/common/feature_layout.cpp
    src/ui/common/feature_menu.cpp
    src/ui/window_switcher/switcher_panel.cpp
    src/ui/screenshot/selection_window.cpp
)

target_include_directories(desktop-orb PRIVATE include)
target_compile_features(desktop-orb PRIVATE cxx_std_20)
target_compile_definitions(desktop-orb PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(desktop-orb PRIVATE dwmapi gdi32 user32 shell32 gdiplus comctl32 ole32 msimg32
    pybind11::embed Python3::Python)
add_screenshot_python_runtime(desktop-orb)

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

    add_executable(screenshot-ai-client-test
        tests/screenshot_ai_client_test.cpp
        src/platform/screenshot/screenshot_ai_client.cpp
        src/platform/common/asset_path.cpp
    )
    target_include_directories(screenshot-ai-client-test PRIVATE include)
    target_compile_features(screenshot-ai-client-test PRIVATE cxx_std_20)
    target_compile_definitions(screenshot-ai-client-test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(screenshot-ai-client-test PRIVATE pybind11::embed Python3::Python user32)
    if(MSVC)
        target_compile_options(screenshot-ai-client-test PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_screenshot_python_runtime(screenshot-ai-client-test)
    add_test(NAME screenshot-ai-client COMMAND screenshot-ai-client-test)
endif()
