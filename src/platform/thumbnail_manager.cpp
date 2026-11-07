#include "desktop_orb/platform/thumbnail_manager.hpp"

#include <algorithm>
#include <unordered_set>

namespace desktop_orb::platform {
namespace {

bool Intersect(const RECT& a, const RECT& b, RECT& output) {
    output = {std::max(a.left, b.left), std::max(a.top, b.top),
              std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    return output.right > output.left && output.bottom > output.top;
}

RECT Fit(const RECT& bounds, SIZE source) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (source.cx <= 0 || source.cy <= 0 || width <= 0 || height <= 0) return bounds;
    int fitted_width = width;
    int fitted_height = height;
    if (static_cast<long long>(source.cx) * height > static_cast<long long>(source.cy) * width)
        fitted_height = MulDiv(width, source.cy, source.cx);
    else fitted_width = MulDiv(height, source.cx, source.cy);
    const int x = bounds.left + (width - fitted_width) / 2;
    const int y = bounds.top + (height - fitted_height) / 2;
    return {x, y, x + fitted_width, y + fitted_height};
}

}  // namespace

ThumbnailManager::~ThumbnailManager() { Clear(); }

void ThumbnailManager::Clear() {
    for (auto& [_, entry] : entries_) {
        if (entry.thumbnail) DwmUnregisterThumbnail(entry.thumbnail);
    }
    entries_.clear();
}

bool ThumbnailManager::Failed(HWND source) const {
    const auto found = entries_.find(source);
    return found != entries_.end() && found->second.failed;
}

void ThumbnailManager::Sync(const std::vector<ThumbnailRequest>& visible) {
    std::unordered_set<HWND> active;
    for (const ThumbnailRequest& request : visible) active.insert(request.source);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!active.contains(it->first)) {
            if (it->second.thumbnail) DwmUnregisterThumbnail(it->second.thumbnail);
            it = entries_.erase(it);
        } else ++it;
    }

    for (const ThumbnailRequest& request : visible) {
        auto [it, inserted] = entries_.try_emplace(request.source);
        Entry& entry = it->second;
        if (inserted) {
            if (FAILED(DwmRegisterThumbnail(destination_, request.source, &entry.thumbnail))) {
                entry.failed = true;
                continue;
            }
            DwmQueryThumbnailSourceSize(entry.thumbnail, &entry.source_size);
        }
        if (entry.failed || !entry.thumbnail) continue;

        const RECT fitted = Fit(request.bounds, entry.source_size);
        RECT destination{};
        if (!Intersect(fitted, request.clip, destination)) {
            DWM_THUMBNAIL_PROPERTIES hidden{};
            hidden.dwFlags = DWM_TNP_VISIBLE;
            hidden.fVisible = FALSE;
            DwmUpdateThumbnailProperties(entry.thumbnail, &hidden);
            continue;
        }
        const int fitted_width = fitted.right - fitted.left;
        const int fitted_height = fitted.bottom - fitted.top;
        if (fitted_width <= 0 || fitted_height <= 0) continue;
        const RECT source{
            MulDiv(destination.left - fitted.left, entry.source_size.cx, fitted_width),
            MulDiv(destination.top - fitted.top, entry.source_size.cy, fitted_height),
            MulDiv(destination.right - fitted.left, entry.source_size.cx, fitted_width),
            MulDiv(destination.bottom - fitted.top, entry.source_size.cy, fitted_height),
        };
        DWM_THUMBNAIL_PROPERTIES properties{};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
        properties.rcDestination = destination;
        properties.rcSource = source;
        properties.fVisible = TRUE;
        properties.opacity = 255;
        if (FAILED(DwmUpdateThumbnailProperties(entry.thumbnail, &properties))) {
            DwmUnregisterThumbnail(entry.thumbnail);
            entry.thumbnail = nullptr;
            entry.failed = true;
        }
    }
}

}  // namespace desktop_orb::platform
