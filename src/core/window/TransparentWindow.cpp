#include "TransparentWindow.h"
#include "ClickThrough.h"
#include "utils/Logger.h"

#include <Windows.h>
#include <dwmapi.h>
#include <ShellScalingApi.h>
#include <windowsx.h>
#include <cassert>
#include <cstring>

namespace dmme {
namespace core {
namespace window {

// Static member init
std::atomic<bool> TransparentWindow::s_classRegistered{false};

// ===================================================================
// Construction / Destruction
// ===================================================================

TransparentWindow::TransparentWindow()
    : m_clickThrough(std::make_unique<ClickThrough>()) {
}

TransparentWindow::~TransparentWindow() {
    Shutdown();
}

// ===================================================================
// Lifecycle
// ===================================================================

bool TransparentWindow::Initialize(const WindowConfig& cfg) {
    if (m_initialized) {
        DMME_LOG_WARN("TransparentWindow::Initialize called on already-initialized window");
        return true;
    }

    DMME_LOG_INFO("Initializing TransparentWindow ({}x{} at {},{})",
                  cfg.width, cfg.height, cfg.posX, cfg.posY);

    m_hinstance = GetModuleHandleW(nullptr);
    if (!m_hinstance) {
        DMME_LOG_CRITICAL("GetModuleHandle failed: {}", FormatWin32Error(GetLastError()));
        return false;
    }

    EnableDPIAwareness();

    if (!RegisterWndClass()) {
        return false;
    }

    m_posX   = cfg.posX;
    m_posY   = cfg.posY;
    m_width  = cfg.width;
    m_height = cfg.height;
    m_topmost       = cfg.alwaysOnTop;
    m_globalAlpha   = cfg.initialOpacity;
    m_alphaThreshold = cfg.alphaThreshold;

    if (!CreateHWND(cfg)) {
        return false;
    }

    if (!AllocateBackBuffer(m_width, m_height)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // Configure click-through sub-component
    m_clickThrough->SetThreshold(m_alphaThreshold);

    // Show or hide based on config
    if (cfg.visible) {
        Show();
    }

    m_initialized = true;
    DMME_LOG_INFO("TransparentWindow initialized successfully (HWND=0x{:X})",
                  reinterpret_cast<uintptr_t>(m_hwnd));
    return true;
}

void TransparentWindow::Shutdown() {
    if (!m_initialized) {
        return;
    }

    DMME_LOG_INFO("TransparentWindow shutting down");

    FreeBackBuffer();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    m_initialized = false;
    m_visible     = false;
    m_pixels      = nullptr;
    m_bufW        = 0;
    m_bufH        = 0;
}

bool TransparentWindow::IsInitialized() const {
    return m_initialized;
}

// ===================================================================
// Message Pump
// ===================================================================

bool TransparentWindow::ProcessMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            DMME_LOG_INFO("WM_QUIT received, exiting message loop");
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ===================================================================
// Frame Update
// ===================================================================

bool TransparentWindow::UpdateFrame(const uint8_t* rgbaPixels, int w, int h) {
    if (!m_initialized || !m_hwnd) {
        DMME_LOG_ERROR("UpdateFrame called on uninitialized window");
        return false;
    }

    if (!rgbaPixels) {
        DMME_LOG_ERROR("UpdateFrame received null pixel pointer");
        return false;
    }

    if (w <= 0 || h <= 0) {
        DMME_LOG_ERROR("UpdateFrame received invalid dimensions {}x{}", w, h);
        return false;
    }

    // Reallocate back buffer if size changed
    if (w != m_bufW || h != m_bufH) {
        DMME_LOG_INFO("Back buffer resize: {}x{} -> {}x{}", m_bufW, m_bufH, w, h);
        FreeBackBuffer();
        if (!AllocateBackBuffer(w, h)) {
            return false;
        }
        m_width  = w;
        m_height = h;
    }

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        ConvertRGBAToBGRAPremul(rgbaPixels, w, h);
    }

    // Update ClickThrough with current buffer state
    m_clickThrough->UpdateBuffer(m_pixels, m_bufW, m_bufH);

    ApplyLayeredUpdate();
    return true;
}

// ===================================================================
// Position
// ===================================================================

void TransparentWindow::SetPosition(int x, int y) {
    m_posX = x;
    m_posY = y;
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        // Also update layered window position
        if (m_initialized && m_pixels) {
            ApplyLayeredUpdate();
        }
    }
}

Point TransparentWindow::GetPosition() const {
    return {m_posX, m_posY};
}

// ===================================================================
// Size
// ===================================================================

void TransparentWindow::SetSize(int w, int h) {
    if (w <= 0 || h <= 0) {
        DMME_LOG_WARN("SetSize ignored: invalid dimensions {}x{}", w, h);
        return;
    }
    m_width  = w;
    m_height = h;
    if (m_hwnd) {
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

Size TransparentWindow::GetSize() const {
    return {m_width, m_height};
}

// ===================================================================
// Always On Top
// ===================================================================

void TransparentWindow::SetAlwaysOnTop(bool enabled) {
    m_topmost = enabled;
    if (m_hwnd) {
        HWND insertAfter = enabled ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(m_hwnd, insertAfter, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        DMME_LOG_DEBUG("Always-on-top set to {}", enabled);
    }
}

bool TransparentWindow::IsAlwaysOnTop() const {
    return m_topmost;
}

// ===================================================================
// Visibility
// ===================================================================

void TransparentWindow::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        m_visible = true;
        DMME_LOG_DEBUG("Window shown");
    }
}

void TransparentWindow::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        DMME_LOG_DEBUG("Window hidden");
    }
}

bool TransparentWindow::IsVisible() const {
    return m_visible;
}

// ===================================================================
// Global Alpha
// ===================================================================

void TransparentWindow::SetGlobalAlpha(uint8_t alpha) {
    m_globalAlpha = alpha;
    // Re-apply layered update if we have a buffer
    if (m_initialized && m_pixels) {
        ApplyLayeredUpdate();
    }
}

uint8_t TransparentWindow::GetGlobalAlpha() const {
    return m_globalAlpha;
}

// ===================================================================
// Click-Through Threshold
// ===================================================================

void TransparentWindow::SetAlphaHitThreshold(uint8_t threshold) {
    m_alphaThreshold = threshold;
    m_clickThrough->SetThreshold(threshold);
}

uint8_t TransparentWindow::GetAlphaHitThreshold() const {
    return m_alphaThreshold;
}

// ===================================================================
// Native Handle
// ===================================================================

HWND TransparentWindow::GetHWND() const {
    return m_hwnd;
}

// ===================================================================
// Alpha Query
// ===================================================================

uint8_t TransparentWindow::GetAlphaAtClientPos(int cx, int cy) const {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!m_pixels || cx < 0 || cy < 0 || cx >= m_bufW || cy >= m_bufH) {
        return 0;
    }
    // BGRA format: offset 3 is alpha
    int offset = (cy * m_bufW + cx) * 4 + 3;
    return m_pixels[offset];
}

// ===================================================================
// Callbacks
// ===================================================================

void TransparentWindow::SetMouseEventCallback(MouseEventCallback cb) {
    m_mouseCallback = std::move(cb);
}

void TransparentWindow::SetResizeCallback(ResizeCallback cb) {
    m_resizeCallback = std::move(cb);
}

void TransparentWindow::SetCloseCallback(CloseCallback cb) {
    m_closeCallback = std::move(cb);
}

// ===================================================================
// Static Window Procedure (routes to instance)
// ===================================================================

LRESULT CALLBACK TransparentWindow::StaticWndProc(HWND hwnd, UINT msg,
                                                   WPARAM wp, LPARAM lp) {
    TransparentWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TransparentWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<TransparentWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->InstanceWndProc(msg, wp, lp);
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ===================================================================
// Instance Window Procedure
// ===================================================================

LRESULT TransparentWindow::InstanceWndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_NCHITTEST: {
        // Convert screen coordinates to client coordinates
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(m_hwnd, &pt);

        // Ask ClickThrough whether this pixel is transparent
        if (m_clickThrough->IsTransparentAt(pt.x, pt.y)) {
            return HTTRANSPARENT;  // Click passes through to desktop
        }
        return HTCLIENT;  // We capture this click
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (m_mouseCallback) {
            MouseEvent evt;
            evt.clientX = GET_X_LPARAM(lp);
            evt.clientY = GET_Y_LPARAM(lp);
            POINT screenPt = {evt.clientX, evt.clientY};
            ClientToScreen(m_hwnd, &screenPt);
            evt.screenX = screenPt.x;
            evt.screenY = screenPt.y;
            evt.isDown  = true;
            evt.isMove  = false;
            if (msg == WM_LBUTTONDOWN) evt.button = MouseButton::Left;
            else if (msg == WM_RBUTTONDOWN) evt.button = MouseButton::Right;
            else evt.button = MouseButton::Middle;
            m_mouseCallback(evt);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (m_mouseCallback) {
            MouseEvent evt;
            evt.clientX = GET_X_LPARAM(lp);
            evt.clientY = GET_Y_LPARAM(lp);
            POINT screenPt = {evt.clientX, evt.clientY};
            ClientToScreen(m_hwnd, &screenPt);
            evt.screenX = screenPt.x;
            evt.screenY = screenPt.y;
            evt.isDown  = false;
            evt.isMove  = false;
            if (msg == WM_LBUTTONUP) evt.button = MouseButton::Left;
            else if (msg == WM_RBUTTONUP) evt.button = MouseButton::Right;
            else evt.button = MouseButton::Middle;
            m_mouseCallback(evt);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (m_mouseCallback) {
            MouseEvent evt;
            evt.clientX = GET_X_LPARAM(lp);
            evt.clientY = GET_Y_LPARAM(lp);
            POINT screenPt = {evt.clientX, evt.clientY};
            ClientToScreen(m_hwnd, &screenPt);
            evt.screenX = screenPt.x;
            evt.screenY = screenPt.y;
            evt.button  = MouseButton::None;
            evt.isDown  = false;
            evt.isMove  = true;
            m_mouseCallback(evt);
        }
        return 0;
    }

    case WM_SIZE: {
        int newW = LOWORD(lp);
        int newH = HIWORD(lp);
        if (newW > 0 && newH > 0 && (newW != m_width || newH != m_height)) {
            m_width  = newW;
            m_height = newH;
            DMME_LOG_DEBUG("Window resized to {}x{}", newW, newH);
            if (m_resizeCallback) {
                m_resizeCallback(newW, newH);
            }
        }
        return 0;
    }

    case WM_CLOSE: {
        DMME_LOG_INFO("WM_CLOSE received");
        if (m_closeCallback) {
            m_closeCallback();
        }
        // Don't destroy -- let the engine decide via Shutdown()
        Hide();
        return 0;
    }

    case WM_DESTROY: {
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        DMME_LOG_INFO("Display configuration changed");
        // MultiMonitor will handle re-enumeration
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ===================================================================
// Internal: Register Window Class
// ===================================================================

bool TransparentWindow::RegisterWndClass() {
    if (s_classRegistered.load()) {
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = m_hinstance;
    wc.hIcon         = nullptr;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // No background -- we paint everything
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = WND_CLASS;
    wc.hIconSm      = nullptr;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err == ERROR_CLASS_ALREADY_EXISTS) {
            s_classRegistered.store(true);
            return true;
        }
        DMME_LOG_CRITICAL("RegisterClassExW failed: {}", FormatWin32Error(err));
        return false;
    }

    s_classRegistered.store(true);
    DMME_LOG_DEBUG("Window class '{}' registered", "DMME_TransparentWindow_Class");
    return true;
}

// ===================================================================
// Internal: Create HWND
// ===================================================================

bool TransparentWindow::CreateHWND(const WindowConfig& cfg) {
    DWORD exStyle = WS_EX_LAYERED;
    if (cfg.alwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }
    if (cfg.toolWindow) {
        exStyle |= WS_EX_TOOLWINDOW;  // Hide from taskbar and Alt-Tab
    }

    // WS_POPUP: no title bar, no borders
    DWORD style = WS_POPUP;

    HWND hwnd = CreateWindowExW(
        exStyle,
        WND_CLASS,
        cfg.title.c_str(),
        style,
        cfg.posX, cfg.posY,
        cfg.width, cfg.height,
        nullptr,   // no parent
        nullptr,   // no menu
        m_hinstance,
        this       // pass this pointer for WM_NCCREATE
    );

    if (!hwnd) {
        DMME_LOG_CRITICAL("CreateWindowExW failed: {}", FormatWin32Error(GetLastError()));
        return false;
    }

    // m_hwnd is set in StaticWndProc during WM_NCCREATE
    assert(m_hwnd == hwnd);

    DMME_LOG_DEBUG("HWND created: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd));
    return true;
}

// ===================================================================
// Internal: Allocate DIB Section Back Buffer
// ===================================================================

bool TransparentWindow::AllocateBackBuffer(int w, int h) {
    if (w <= 0 || h <= 0) {
        DMME_LOG_ERROR("AllocateBackBuffer: invalid size {}x{}", w, h);
        return false;
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        DMME_LOG_ERROR("GetDC(nullptr) failed");
        return false;
    }

    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    if (!m_memDC) {
        DMME_LOG_ERROR("CreateCompatibleDC failed: {}", FormatWin32Error(GetLastError()));
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;  // Negative = top-down DIB
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage   = static_cast<DWORD>(w * h * 4);

    void* bits = nullptr;
    m_dib = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_dib || !bits) {
        DMME_LOG_CRITICAL("CreateDIBSection failed for {}x{}: {}",
                          w, h, FormatWin32Error(GetLastError()));
        DeleteDC(m_memDC);
        m_memDC = nullptr;
        return false;
    }

    m_prevBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_dib));
    m_pixels     = static_cast<uint8_t*>(bits);
    m_bufW       = w;
    m_bufH       = h;

    // Clear buffer to fully transparent black
    std::memset(m_pixels, 0, static_cast<size_t>(w) * h * 4);

    DMME_LOG_DEBUG("Back buffer allocated: {}x{} ({} bytes)",
                   w, h, w * h * 4);
    return true;
}

// ===================================================================
// Internal: Free Back Buffer
// ===================================================================

void TransparentWindow::FreeBackBuffer() {
    if (m_memDC) {
        if (m_prevBitmap) {
            SelectObject(m_memDC, m_prevBitmap);
            m_prevBitmap = nullptr;
        }
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }
    if (m_dib) {
        DeleteObject(m_dib);
        m_dib = nullptr;
    }
    m_pixels = nullptr;
    m_bufW   = 0;
    m_bufH   = 0;
}

// ===================================================================
// Internal: Convert RGBA to BGRA Premultiplied
// ===================================================================

void TransparentWindow::ConvertRGBAToBGRAPremul(const uint8_t* src, int w, int h) {
    // src: RGBA (non-premultiplied) from renderer
    // dst: m_pixels (BGRA premultiplied) for UpdateLayeredWindow
    //
    // For each pixel:
    //   dst.B = src.B * src.A / 255
    //   dst.G = src.G * src.A / 255
    //   dst.R = src.R * src.A / 255
    //   dst.A = src.A

    const int totalPixels = w * h;
    const uint8_t* in  = src;
    uint8_t*       out = m_pixels;

    for (int i = 0; i < totalPixels; ++i) {
        const uint8_t r = in[0];
        const uint8_t g = in[1];
        const uint8_t b = in[2];
        const uint8_t a = in[3];

        if (a == 255) {
            // Fully opaque: no multiplication needed, just swizzle
            out[0] = b;
            out[1] = g;
            out[2] = r;
            out[3] = 255;
        } else if (a == 0) {
            // Fully transparent: zero everything
            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
            out[3] = 0;
        } else {
            // Premultiply: channel * alpha / 255
            // Using (channel * alpha + 127) / 255 for better rounding
            out[0] = static_cast<uint8_t>((b * a + 127) / 255);
            out[1] = static_cast<uint8_t>((g * a + 127) / 255);
            out[2] = static_cast<uint8_t>((r * a + 127) / 255);
            out[3] = a;
        }

        in  += 4;
        out += 4;
    }
}

// ===================================================================
// Internal: Push Pixel Buffer to Layered Window
// ===================================================================

void TransparentWindow::ApplyLayeredUpdate() {
    if (!m_hwnd || !m_memDC || !m_pixels) {
        return;
    }

    POINT ptSrc  = {0, 0};
    POINT ptDst  = {m_posX, m_posY};
    SIZE  szWnd  = {static_cast<LONG>(m_bufW), static_cast<LONG>(m_bufH)};

    BLENDFUNCTION blend{};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = m_globalAlpha;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    BOOL result = UpdateLayeredWindow(
        m_hwnd,
        nullptr,    // destination DC (screen)
        &ptDst,     // window position on screen
        &szWnd,     // window size
        m_memDC,    // source DC with our BGRA bitmap
        &ptSrc,     // source origin
        0,          // color key (unused)
        &blend,     // alpha blending config
        ULW_ALPHA   // use per-pixel alpha
    );

    if (!result) {
        DMME_LOG_ERROR("UpdateLayeredWindow failed: {}", FormatWin32Error(GetLastError()));
    }
}

// ===================================================================
// Internal: Enable DPI Awareness
// ===================================================================

void TransparentWindow::EnableDPIAwareness() {
    // Try the most modern API first (Windows 10 1703+)
    // Fall back gracefully if unavailable
    static bool s_dpiSet = false;
    if (s_dpiSet) return;

    // SetProcessDpiAwarenessContext is available on Windows 10 1703+
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetDpiAwarenessContextFunc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto fn = reinterpret_cast<SetDpiAwarenessContextFunc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                DMME_LOG_INFO("DPI awareness set: Per-Monitor Aware V2");
                s_dpiSet = true;
                return;
            }
        }
    }

    // Fallback: SetProcessDpiAwareness (Windows 8.1+)
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using SetDpiAwarenessFunc = HRESULT(WINAPI*)(int);
        auto fn = reinterpret_cast<SetDpiAwarenessFunc>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (fn) {
            // PROCESS_PER_MONITOR_DPI_AWARE = 2
            HRESULT hr = fn(2);
            if (SUCCEEDED(hr)) {
                DMME_LOG_INFO("DPI awareness set: Per-Monitor Aware (fallback)");
                s_dpiSet = true;
                FreeLibrary(shcore);
                return;
            }
        }
        FreeLibrary(shcore);
    }

    // Last resort: basic DPI awareness
    SetProcessDPIAware();
    DMME_LOG_WARN("DPI awareness set: System Aware (legacy fallback)");
    s_dpiSet = true;
}

// ===================================================================
// Utility: Format Win32 Error Code to String
// ===================================================================

std::string TransparentWindow::FormatWin32Error(DWORD code) {
    if (code == 0) {
        return "Success (0)";
    }

    LPWSTR buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    std::string result = "Error code " + std::to_string(code);
    if (buffer && len > 0) {
        // Convert wide to narrow for logging
        int narrowLen = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len),
                                            nullptr, 0, nullptr, nullptr);
        if (narrowLen > 0) {
            std::string narrow(static_cast<size_t>(narrowLen), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(len),
                               narrow.data(), narrowLen, nullptr, nullptr);
            // Trim trailing newline
            while (!narrow.empty() && (narrow.back() == '\n' || narrow.back() == '\r')) {
                narrow.pop_back();
            }
            result += ": " + narrow;
        }
        LocalFree(buffer);
    }

    return result;
}

} // namespace window
} // namespace core
} // namespace dmme