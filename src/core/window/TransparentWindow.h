#pragma once

#include <Windows.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <memory>

#include "WindowTypes.h"

namespace dmme {
namespace core {
namespace window {

// Forward declarations
class ClickThrough;

class TransparentWindow {
public:
    TransparentWindow();
    ~TransparentWindow();

    // Non-copyable
    TransparentWindow(const TransparentWindow&) = delete;
    TransparentWindow& operator=(const TransparentWindow&) = delete;

    // ----- Lifecycle -----
    bool Initialize(const WindowConfig& config);
    void Shutdown();
    bool IsInitialized() const;

    // ----- Message Pump -----
    // Returns false when WM_QUIT is received.
    // Call this in your main loop.
    bool ProcessMessages();

    // ----- Frame Update -----
    // Accepts RGBA (non-premultiplied) pixel data from the renderer.
    // Converts internally to BGRA premultiplied and pushes to the
    // layered window via UpdateLayeredWindow.
    // width/height must match current window dimensions or the
    // internal buffer will be reallocated.
    bool UpdateFrame(const uint8_t* rgbaPixels, int width, int height);

    // ----- Position -----
    void  SetPosition(int x, int y);
    Point GetPosition() const;

    // ----- Size -----
    void SetSize(int width, int height);
    Size GetSize() const;

    // ----- Always On Top -----
    void SetAlwaysOnTop(bool enabled);
    bool IsAlwaysOnTop() const;

    // ----- Visibility -----
    void Show();
    void Hide();
    bool IsVisible() const;

    // ----- Global Opacity -----
    // 0 = fully transparent, 255 = fully opaque.
    // Used by OpacityController for fade effects.
    void    SetGlobalAlpha(uint8_t alpha);
    uint8_t GetGlobalAlpha() const;

    // ----- Click-Through Threshold -----
    void    SetAlphaHitThreshold(uint8_t threshold);
    uint8_t GetAlphaHitThreshold() const;

    // ----- Native Handle -----
    HWND GetHWND() const;

    // ----- Pixel Buffer Read Access (for hit testing, external queries) -----
    // Returns alpha at the given client-space coordinate.
    // Returns 0 if out of bounds.
    uint8_t GetAlphaAtClientPos(int cx, int cy) const;

    // ----- Callbacks -----
    void SetMouseEventCallback(MouseEventCallback cb);
    void SetResizeCallback(ResizeCallback cb);
    void SetCloseCallback(CloseCallback cb);

private:
    // Win32 window proc routing
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp);
    LRESULT InstanceWndProc(UINT msg, WPARAM wp, LPARAM lp);

    // Internal setup helpers
    bool RegisterWndClass();
    bool CreateHWND(const WindowConfig& cfg);
    bool AllocateBackBuffer(int w, int h);
    void FreeBackBuffer();
    void ApplyLayeredUpdate();
    void EnableDPIAwareness();

    // Pixel conversion: RGBA -> BGRA premultiplied alpha
    void ConvertRGBAToBGRAPremul(const uint8_t* src, int w, int h);

    // Win32 error formatting
    static std::string FormatWin32Error(DWORD code);

    // ----- Win32 Handles -----
    HWND     m_hwnd        = nullptr;
    HINSTANCE m_hinstance  = nullptr;
    HDC      m_memDC       = nullptr;
    HBITMAP  m_dib         = nullptr;
    HBITMAP  m_prevBitmap  = nullptr;

    // ----- Pixel Buffer -----
    // Points into the DIB section memory. Owned by Windows.
    // Format: BGRA premultiplied, top-down.
    uint8_t* m_pixels      = nullptr;
    int      m_bufW        = 0;
    int      m_bufH        = 0;

    // ----- State -----
    int      m_posX        = 0;
    int      m_posY        = 0;
    int      m_width       = 0;
    int      m_height      = 0;
    uint8_t  m_globalAlpha = 255;
    uint8_t  m_alphaThreshold = 10;
    bool     m_topmost     = true;
    bool     m_visible     = false;
    bool     m_initialized = false;

    // ----- Sub-component -----
    std::unique_ptr<ClickThrough> m_clickThrough;

    // ----- Callbacks -----
    MouseEventCallback m_mouseCallback;
    ResizeCallback     m_resizeCallback;
    CloseCallback      m_closeCallback;

    // ----- Thread Safety -----
    mutable std::mutex m_bufferMutex;

    // ----- Class Registration -----
    static constexpr const wchar_t* WND_CLASS = L"DMME_TransparentWindow_Class";
    static std::atomic<bool> s_classRegistered;
};

} // namespace window
} // namespace core
} // namespace dmme