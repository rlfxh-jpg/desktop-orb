#pragma once

// 文件职责：定义平台层的资源定位接口，以可执行文件目录为基准查找资源。
// 本文件不负责读取、解码或绘制图片；调用方负责处理资源不存在的情况。
#include <filesystem>
#include <string_view>

namespace desktop_orb::platform {

// 作用：将 assets 目录下的相对路径转换为绝对路径，不依赖进程工作目录。
// 参数：relative_path 为相对于 assets 目录的路径，例如 icons/orb.png。
// 返回：完整路径；无法取得程序路径时返回空路径，由调用方处理失败。
[[nodiscard]] std::filesystem::path AssetPath(std::wstring_view relative_path);

}  // desktop_orb::platform 命名空间结束
