#pragma once

#include "WindowTypes.h"

#include <Windows.h>
#include <vector>
#include <mutex>
#include <optional>

namespace dmme {
namespace core {
namespace window {

// MultiMonitor enumerates all connected displays and provides
// geometry queries used by the rest of the window subsystem.
//
// Typical usage:
//   MultiMonitor mm;
//   mm.Refresh();                   // enumerate monitors
//   auto count = mm.GetCount();
//   auto info  = mm.GetMonitor(0);  // primary monitor info
//   auto bounds = mm.GetVirtualDesktopBounds(); // all monitors combined
//
// Call Refresh() again whenever WM_DISPLAYCHANGE is received.

class MultiMonitor {
public:
    MultiMonitor();
    ~MultiMonitor() = default;

    // Non-copyable
    MultiMonitor(const MultiMonitor&) = delete;
    MultiMonitor& operator=(const MultiMonitor&) = delete;

    // --- Enumeration ---

    // Re-scan all connected monitors.
    // Safe to call any time (e.g., on WM_DISPLAYCHANGE).
    // Returns the number of monitors found.
    int Refresh();

    // --- Queries ---

    // Number of monitors found in the last Refresh().
    int GetCount() const;

    // Info for monitor at the given index.
    // Returns std::nullopt if index is out of range.
    std::optional<MonitorInfo> GetMonitor(int index) const;

    // Info for the primary monitor (the one marked isPrimary).
    // Returns std::nullopt if no monitors found.
    std::optional<MonitorInfo> GetPrimaryMonitor() const;

    // The virtual desktop rectangle that spans all monitors.
    Rect GetVirtualDesktopBounds() const;

    // Which monitor contains the given screen-space point?
    // Returns monitor index, or -1 if the point is outside all monitors.
    int GetMonitorIndexAtPoint(int screenX, int screenY) const;

    // Which monitor contains the given rectangle (by largest overlap)?
    // Returns monitor index, or -1 if no overlap.
    int GetMonitorIndexForRect(const Rect& rect) const;

    // Get the bounds of the monitor that contains the point.
    // Returns std::nullopt if point is not on any monitor.
    std::optional<Rect> GetMonitorBoundsAtPoint(int screenX, int screenY) const;

    // Can the character cross from the current position toward the given
    // direction and land on another monitor?
    // direction: 0=left, 1=right, 2=up, 3=down
    bool CanCrossToMonitor(int fromMonitorIndex, int direction) const;

    // DPI scale factor for monitor at index.
    // Returns 1.0f if index is invalid.
    float GetDPIScale(int index) const;

private:
    // Win32 callback for EnumDisplayMonitors
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdcMon,
                                         LPRECT lprcMon, LPARAM dwData);

    // Internal struct used during enumeration callback
    struct EnumContext {
        std::vector<MonitorInfo> monitors;
    };

    // Get DPI for a specific monitor handle
    static void QueryMonitorDPI(HMONITOR hMon, uint32_t& dpiX, uint32_t& dpiY);

    std::vector<MonitorInfo> m_monitors;
    Rect                     m_virtualBounds;
    mutable std::mutex       m_mutex;
};

} // namespace window
} // namespace core
} // namespace dmme