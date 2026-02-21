#include "ClickThrough.h"
#include "utils/Logger.h"

namespace dmme {
namespace core {
namespace window {

// ===================================================================
// Construction
// ===================================================================

ClickThrough::ClickThrough()
    : m_buffer(nullptr)
    , m_width(0)
    , m_height(0)
    , m_threshold(10) {
    DMME_LOG_DEBUG("ClickThrough created with threshold={}", m_threshold);
}

// ===================================================================
// Threshold
// ===================================================================

void ClickThrough::SetThreshold(uint8_t threshold) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_threshold = threshold;
    DMME_LOG_DEBUG("ClickThrough threshold set to {}", threshold);
}

uint8_t ClickThrough::GetThreshold() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_threshold;
}

// ===================================================================
// Buffer Management
// ===================================================================

void ClickThrough::UpdateBuffer(const uint8_t* bgraBuffer, int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!bgraBuffer || width <= 0 || height <= 0) {
        m_buffer = nullptr;
        m_width  = 0;
        m_height = 0;
        return;
    }

    m_buffer = bgraBuffer;
    m_width  = width;
    m_height = height;
}

void ClickThrough::ClearBuffer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer = nullptr;
    m_width  = 0;
    m_height = 0;
}

// ===================================================================
// Hit Testing
// ===================================================================

bool ClickThrough::IsTransparentAt(int x, int y) const {
    return GetAlphaAt(x, y) <= m_threshold;
}

bool ClickThrough::IsOpaqueAt(int x, int y) const {
    return GetAlphaAt(x, y) > m_threshold;
}

uint8_t ClickThrough::GetAlphaAt(int x, int y) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Bounds check
    if (!m_buffer || x < 0 || y < 0 || x >= m_width || y >= m_height) {
        return 0;
    }

    // BGRA format: each pixel is 4 bytes [B, G, R, A]
    // Alpha is at offset 3 within each pixel.
    // Top-down layout: row 0 is at the top.
    const int pixelIndex = y * m_width + x;
    const int byteOffset = pixelIndex * 4 + 3;  // +3 for alpha channel

    return m_buffer[byteOffset];
}

} // namespace window
} // namespace core
} // namespace dmme