#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace dmme {
namespace core {
namespace window {

// ------------------------------------------------------------------
// Fundamental geometry types for the window subsystem
// ------------------------------------------------------------------

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int width  = 0;
    int height = 0;
};

struct Rect {
    int left   = 0;
    int top    = 0;
    int right  = 0;
    int bottom = 0;

    int Width()  const { return right - left; }
    int Height() const { return bottom - top; }
    bool Contains(int px, int py) const {
        return px >= left && px < right && py >= top && py < bottom;
    }
};

// ------------------------------------------------------------------
// Monitor description (used by MultiMonitor)
// ------------------------------------------------------------------

struct MonitorInfo {
    std::wstring deviceName;
    Rect         workArea;      // usable area (excludes taskbar)
    Rect         fullArea;      // full monitor area
    uint32_t     dpiX   = 96;
    uint32_t     dpiY   = 96;
    bool         isPrimary = false;
    float        scaleFactor = 1.0f;  // dpiX / 96.0f
};

// ------------------------------------------------------------------
// Window configuration
// ------------------------------------------------------------------

struct WindowConfig {
    int          posX            = 100;
    int          posY            = 100;
    int          width           = 512;
    int          height          = 512;
    bool         alwaysOnTop     = true;
    bool         visible         = true;
    bool         toolWindow      = true;   // hide from taskbar/alt-tab
    std::wstring title           = L"DMME Mascot";
    uint8_t      alphaThreshold  = 10;     // pixels with alpha <= this pass clicks through
    uint8_t      initialOpacity  = 255;    // global window opacity (0-255)
};

// ------------------------------------------------------------------
// Mouse event data (forwarded by TransparentWindow)
// ------------------------------------------------------------------

enum class MouseButton : uint8_t {
    None   = 0,
    Left   = 1,
    Right  = 2,
    Middle = 3
};

struct MouseEvent {
    int         clientX  = 0;    // relative to window top-left
    int         clientY  = 0;
    int         screenX  = 0;    // absolute screen position
    int         screenY  = 0;
    MouseButton button   = MouseButton::None;
    bool        isDown   = false;
    bool        isMove   = false;
};

// ------------------------------------------------------------------
// Callback types
// ------------------------------------------------------------------

using MouseEventCallback  = std::function<void(const MouseEvent&)>;
using ResizeCallback      = std::function<void(int width, int height)>;
using CloseCallback       = std::function<void()>;

} // namespace window
} // namespace core
} // namespace dmme