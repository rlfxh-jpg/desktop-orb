# 文件职责：使用 Cython 将 llm_worker.py 构建为可由 CPython 导入的 Windows 扩展模块。
# 所属模块：截图大模型调用；生成的 .pyd 放入 CMake 指定的构建目录，不写入源码目录。
# 依赖：当前构建 Python 环境中的 Cython、setuptools 和与 CMake 匹配的 MSVC 工具链。

import os
from pathlib import Path

from Cython.Build import cythonize
from setuptools import Extension, setup


source_file = Path(__file__).resolve().parent / "llm_worker.py"
generated_directory = os.environ.get("DESKTOP_ORB_CYTHON_BUILD_DIR", "").strip()
if not generated_directory:
    raise RuntimeError("必须通过 DESKTOP_ORB_CYTHON_BUILD_DIR 指定 Cython 生成文件目录。")

# 核心逻辑：将 Cython 生成的 C++ 源码放在 build 目录，避免把编译产物混入 assets/python 源码。
extensions = cythonize(
    [Extension("llm_worker", [str(source_file)])],
    build_dir=generated_directory,
    compiler_directives={"language_level": 3},
)

setup(
    name="desktop-orb-screenshot-ai",
    ext_modules=extensions,
)
