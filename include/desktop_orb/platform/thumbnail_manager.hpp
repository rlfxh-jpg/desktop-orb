#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <unordered_map>
#include <vector>

namespace desktop_orb::platform {

struct ThumbnailRequest {
    HWND source = nullptr;
    RECT bounds{};
    RECT clip{};
};

class ThumbnailManager {
public:
    explicit ThumbnailManager(HWND destination) : destination_(destination) {}
    ThumbnailManager(const ThumbnailManager&) = delete;
    ThumbnailManager& operator=(const ThumbnailManager&) = delete;
    ~ThumbnailManager();

    void Sync(const std::vector<ThumbnailRequest>& visible);
    void Clear();
    [[nodiscard]] bool Failed(HWND source) const;

private:
    struct Entry {
        HTHUMBNAIL thumbnail = nullptr;
        SIZE source_size{};
        bool failed = false;
    };
    HWND destination_;
    std::unordered_map<HWND, Entry> entries_;
};

}  // namespace desktop_orb::platform
