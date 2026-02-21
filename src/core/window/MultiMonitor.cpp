#include "MultiMonitor.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <ShellScalingApi.h>
#include <algorithm>

namespace dmme {
namespace core {
namespace window {

// ===================================================================
// Construction
// ===================================================================

MultiMonitor::MultiMonitor() {
    DMME_LOG_DEBUG("MultiMonitor created");
    Refresh();
}

// ===================================================================
// Enumeration
// ===================================================================

int MultiMonitor::Refresh() {
    EnumContext ctx;

    BOOL result = EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                                       reinterpret_cast<LPARAM>(&ctx));
    if (!result) {
        DMME_LOG_ERROR("EnumDisplayMonitors failed");
        return 0;
    }

    // Calculate virtual desktop bounds
    Rect vBounds;
    if (!ctx.monitors.empty()) {
        vBounds.left   = ctx.monitors[0].fullArea.left;
        vBounds.top    = ctx.monitors[0].fullArea.top;
        vBounds.right  = ctx.monitors[0].fullArea.right;
        vBounds.bottom = ctx.monitors[0].fullArea.bottom;

        for (size_t i = 1; i < ctx.monitors.size(); ++i) {
            const auto& fa = ctx.monitors[i].fullArea;
            if (fa.left   < vBounds.left)   vBounds.left   = fa.left;
            if (fa.top    < vBounds.top)    vBounds.top    = fa.top;
            if (fa.right  > vBounds.right)  vBounds.right  = fa.right;
            if (fa.bottom > vBounds.bottom) vBounds.bottom = fa.bottom;
        }
    }

    // Commit under lock
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_monitors     = std::move(ctx.monitors);
        m_virtualBounds = vBounds;
    }

    DMME_LOG_INFO("MultiMonitor refresh: {} monitor(s) found, virtual desktop=({},{})--({},{})",
                  m_monitors.size(),
                  vBounds.left, vBounds.top, vBounds.right, vBounds.bottom);

    for (size_t i = 0; i < m_monitors.size(); ++i) {
        const auto& m = m_monitors[i];
        DMME_LOG_INFO("  Monitor {}: area=({},{})--({},{}) dpi={}x{} scale={:.2f} primary={}",
                      i,
                      m.fullArea.left, m.fullArea.top,
                      m.fullArea.right, m.fullArea.bottom,
                      m.dpiX, m.dpiY, m.scaleFactor,
                      m.isPrimary ? "yes" : "no");
    }

    return static_cast<int>(m_monitors.size());
}

// ===================================================================
// Monitor Enumeration Callback
// ===================================================================

BOOL CALLBACK MultiMonitor::MonitorEnumProc(HMONITOR hMon, HDC /*hdcMon*/,
                                             LPRECT /*lprcMon*/, LPARAM dwData) {
    auto* ctx = reinterpret_cast<EnumContext*>(dwData);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(hMon, &mi)) {
        DMME_LOG_WARN("GetMonitorInfoW failed for a monitor handle");
        return TRUE;  // continue enumeration
    }

    MonitorInfo info;
    info.deviceName  = mi.szDevice;

    info.fullArea.left   = mi.rcMonitor.left;
    info.fullArea.top    = mi.rcMonitor.top;
    info.fullArea.right  = mi.rcMonitor.right;
    info.fullArea.bottom = mi.rcMonitor.bottom;

    info.workArea.left   = mi.rcWork.left;
    info.workArea.top    = mi.rcWork.top;
    info.workArea.right  = mi.rcWork.right;
    info.workArea.bottom = mi.rcWork.bottom;

    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // Query DPI
    QueryMonitorDPI(hMon, info.dpiX, info.dpiY);
    info.scaleFactor = static_cast<float>(info.dpiX) / 96.0f;

    ctx->monitors.push_back(std::move(info));
    return TRUE;
}

// ===================================================================
// DPI Query
// ===================================================================

void MultiMonitor::QueryMonitorDPI(HMONITOR hMon, uint32_t& dpiX, uint32_t& dpiY) {
    dpiX = 96;
    dpiY = 96;

    // GetDpiForMonitor is available on Windows 8.1+
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (!shcore) {
        return;
    }

    using GetDpiForMonitorFunc = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    auto fn = reinterpret_cast<GetDpiForMonitorFunc>(
        GetProcAddress(shcore, "GetDpiForMonitor"));

    if (fn) {
        UINT dx = 96, dy = 96;
        // MDT_EFFECTIVE_DPI = 0
        HRESULT hr = fn(hMon, 0, &dx, &dy);
        if (SUCCEEDED(hr)) {
            dpiX = dx;
            dpiY = dy;
        }
    }

    FreeLibrary(shcore);
}

// ===================================================================
// Queries
// ===================================================================

int MultiMonitor::GetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_monitors.size());
}

std::optional<MonitorInfo> MultiMonitor::GetMonitor(int index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_monitors.size())) {
        return std::nullopt;
    }
    return m_monitors[static_cast<size_t>(index)];
}

std::optional<MonitorInfo> MultiMonitor::GetPrimaryMonitor() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& m : m_monitors) {
        if (m.isPrimary) {
            return m;
        }
    }
    // Fallback: return first monitor if none marked primary
    if (!m_monitors.empty()) {
        return m_monitors[0];
    }
    return std::nullopt;
}

Rect MultiMonitor::GetVirtualDesktopBounds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_virtualBounds;
}

int MultiMonitor::GetMonitorIndexAtPoint(int screenX, int screenY) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < static_cast<int>(m_monitors.size()); ++i) {
        if (m_monitors[static_cast<size_t>(i)].fullArea.Contains(screenX, screenY)) {
            return i;
        }
    }
    return -1;
}

int MultiMonitor::GetMonitorIndexForRect(const Rect& rect) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int bestIndex = -1;
    int bestArea  = 0;

    for (int i = 0; i < static_cast<int>(m_monitors.size()); ++i) {
        const auto& ma = m_monitors[static_cast<size_t>(i)].fullArea;

        // Calculate overlap
        int overlapLeft   = (std::max)(rect.left,  ma.left);
        int overlapTop    = (std::max)(rect.top,   ma.top);
        int overlapRight  = (std::min)(rect.right, ma.right);
        int overlapBottom = (std::min)(rect.bottom, ma.bottom);

        if (overlapLeft < overlapRight && overlapTop < overlapBottom) {
            int area = (overlapRight - overlapLeft) * (overlapBottom - overlapTop);
            if (area > bestArea) {
                bestArea  = area;
                bestIndex = i;
            }
        }
    }

    return bestIndex;
}

std::optional<Rect> MultiMonitor::GetMonitorBoundsAtPoint(int screenX, int screenY) const {
    int idx = GetMonitorIndexAtPoint(screenX, screenY);
    if (idx < 0) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    return m_monitors[static_cast<size_t>(idx)].fullArea;
}

bool MultiMonitor::CanCrossToMonitor(int fromMonitorIndex, int direction) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (fromMonitorIndex < 0 || fromMonitorIndex >= static_cast<int>(m_monitors.size())) {
        return false;
    }

    const auto& from = m_monitors[static_cast<size_t>(fromMonitorIndex)].fullArea;

    // Check if any other monitor is adjacent in the given direction
    for (int i = 0; i < static_cast<int>(m_monitors.size()); ++i) {
        if (i == fromMonitorIndex) continue;

        const auto& other = m_monitors[static_cast<size_t>(i)].fullArea;

        // Check vertical overlap (for left/right crossing)
        bool vertOverlap = (from.top < other.bottom) && (from.bottom > other.top);
        // Check horizontal overlap (for up/down crossing)
        bool horzOverlap = (from.left < other.right) && (from.right > other.left);

        switch (direction) {
            case 0: // left
                if (vertOverlap && other.right <= from.left && (from.left - other.right) <= 1) {
                    return true;
                }
                break;
            case 1: // right
                if (vertOverlap && other.left >= from.right && (other.left - from.right) <= 1) {
                    return true;
                }
                break;
            case 2: // up
                if (horzOverlap && other.bottom <= from.top && (from.top - other.bottom) <= 1) {
                    return true;
                }
                break;
            case 3: // down
                if (horzOverlap && other.top >= from.bottom && (other.top - from.bottom) <= 1) {
                    return true;
                }
                break;
            default:
                return false;
        }
    }

    return false;
}

float MultiMonitor::GetDPIScale(int index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < 0 || index >= static_cast<int>(m_monitors.size())) {
        return 1.0f;
    }
    return m_monitors[static_cast<size_t>(index)].scaleFactor;
}

} // namespace window
} // namespace core
} // namespace dmme