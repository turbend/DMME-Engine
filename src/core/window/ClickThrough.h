#pragma once

#include <cstdint>
#include <mutex>

namespace dmme {
namespace core {
namespace window {

// ClickThrough provides per-pixel alpha-based hit testing.
//
// It reads from a BGRA pixel buffer (typically the DIB section owned
// by TransparentWindow) and determines whether a given client-space
// coordinate should capture the click or let it pass through to the
// desktop.
//
// Thread safety: UpdateBuffer and IsTransparentAt can be called from
// different threads. Internal synchronization is handled via mutex.

class ClickThrough {
public:
    ClickThrough();
    ~ClickThrough() = default;

    // Non-copyable
    ClickThrough(const ClickThrough&) = delete;
    ClickThrough& operator=(const ClickThrough&) = delete;

    // --- Configuration ---

    // Pixels with alpha <= threshold are considered transparent
    // (clicks pass through). Default is 10.
    void    SetThreshold(uint8_t threshold);
    uint8_t GetThreshold() const;

    // --- Buffer Reference ---

    // Update the buffer pointer and dimensions.
    // The buffer must remain valid until the next call to UpdateBuffer
    // or until the ClickThrough is destroyed.
    // Buffer format: BGRA, 4 bytes per pixel, top-down.
    void UpdateBuffer(const uint8_t* bgraBuffer, int width, int height);

    // Clear the buffer reference (set to null).
    void ClearBuffer();

    // --- Hit Testing ---

    // Returns true if the pixel at (x, y) is transparent
    // (alpha <= threshold), meaning clicks should pass through.
    bool IsTransparentAt(int x, int y) const;

    // Returns true if the pixel at (x, y) is opaque enough
    // (alpha > threshold), meaning our window should capture the click.
    bool IsOpaqueAt(int x, int y) const;

    // Returns the raw alpha value at (x, y).
    // Returns 0 if out of bounds or no buffer set.
    uint8_t GetAlphaAt(int x, int y) const;

private:
    const uint8_t* m_buffer = nullptr;
    int            m_width  = 0;
    int            m_height = 0;
    uint8_t        m_threshold = 10;
    mutable std::mutex m_mutex;
};

} // namespace window
} // namespace core
} // namespace dmme