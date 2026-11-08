#pragma once

// 文件职责：定义桌面围栏读取桌面文件项目的分类与查询接口。
// 本模块只枚举已存在的桌面文件并委托 Windows Shell 打开项目，不移动或修改用户文件。
#include <string>
#include <vector>

namespace desktop_orb::platform {

// 作用：表示桌面围栏面板中的文件项目类别。
// 参数：无。返回：无。
enum class DesktopFenceCategory { programs, folders, documents, other };

// 作用：保存一个可显示和打开的桌面 Shell 项目信息。
// 参数：无。返回：无；图标索引属于系统共享的小图标列表，不由此结构释放。
struct DesktopItem {
    std::wstring name;
    std::wstring type_name;
    std::wstring path;
    int icon_index = -1;
};

class DesktopItemCatalog {
public:
    // 作用：枚举用户桌面与公共桌面中属于指定类别的文件项目。
    // 参数：category 为待查询类别。返回：按名称排序的项目；目录不可读时返回可读部分或空列表。
    [[nodiscard]] std::vector<DesktopItem> List(DesktopFenceCategory category) const;

    // 作用：取得桌面围栏分类在界面中显示的中文名称。
    // 参数：category 为目标分类。返回：静态中文标题；无效枚举值使用“其他”。
    [[nodiscard]] static const wchar_t* CategoryName(DesktopFenceCategory category);

    // 作用：通过 Windows Shell 打开文件或文件夹。
    // 参数：path 为完整项目路径。返回：Shell 接受打开请求时为 true，否则为 false。
    [[nodiscard]] static bool Open(const std::wstring& path);
};

}  // desktop_orb::platform 命名空间结束
