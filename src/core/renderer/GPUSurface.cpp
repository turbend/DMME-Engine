#include "GPUSurface.h"
#include "utils/Logger.h"

namespace dmme {
namespace core {
namespace renderer {

// ===================================================================
// Construction / Destruction
// ===================================================================

GPUSurface::GPUSurface() {
    DMME_LOG_DEBUG("GPUSurface created");
}

GPUSurface::~GPUSurface() {
    if (m_created) {
        Destroy();
    }
}

// ===================================================================
// Create
// ===================================================================

bool GPUSurface::Create(IGraphicsDriver* driver, const RenderTargetDesc& desc) {
    if (!driver) {
        DMME_LOG_ERROR("GPUSurface::Create: null driver pointer");
        return false;
    }

    if (!driver->IsInitialized()) {
        DMME_LOG_ERROR("GPUSurface::Create: driver not initialized");
        return false;
    }

    if (desc.width <= 0 || desc.height <= 0) {
        DMME_LOG_ERROR("GPUSurface::Create: invalid dimensions {}x{}",
                       desc.width, desc.height);
        return false;
    }

    // Destroy existing surface first
    if (m_created) {
        Destroy();
    }

    m_driver   = driver;
    m_width    = desc.width;
    m_height   = desc.height;
    m_format   = desc.format;
    m_samples  = desc.samples;
    m_hasDepth = desc.hasDepth;

    if (!m_driver->CreateTarget(desc)) {
        DMME_LOG_ERROR("GPUSurface::Create: driver CreateTarget failed");
        m_driver = nullptr;
        return false;
    }

    // Pre-allocate readback buffer
    m_readback.Allocate(m_width, m_height);

    m_created = true;
    DMME_LOG_INFO("GPUSurface created: {}x{} format={} samples={} depth={}",
                  m_width, m_height, static_cast<int>(m_format),
                  m_samples, m_hasDepth);
    return true;
}

// ===================================================================
// Resize
// ===================================================================

bool GPUSurface::Resize(int width, int height) {
    if (!m_created || !m_driver) {
        DMME_LOG_ERROR("GPUSurface::Resize: surface not created");
        return false;
    }

    if (width <= 0 || height <= 0) {
        DMME_LOG_ERROR("GPUSurface::Resize: invalid dimensions {}x{}", width, height);
        return false;
    }

    if (width == m_width && height == m_height) {
        return true;  // No change needed
    }

    DMME_LOG_INFO("GPUSurface resizing: {}x{} -> {}x{}", m_width, m_height, width, height);

    if (!m_driver->ResizeTarget(width, height)) {
        DMME_LOG_ERROR("GPUSurface::Resize: driver ResizeTarget failed");
        return false;
    }

    m_width  = width;
    m_height = height;
    m_readback.Allocate(m_width, m_height);

    return true;
}

// ===================================================================
// Destroy
// ===================================================================

void GPUSurface::Destroy() {
    if (!m_created) return;

    DMME_LOG_INFO("GPUSurface destroying");

    if (m_driver && m_driver->IsInitialized()) {
        m_driver->DestroyTarget();
    }

    m_readback.data.clear();
    m_readback.data.shrink_to_fit();
    m_readback.width  = 0;
    m_readback.height = 0;
    m_created  = false;
    m_driver   = nullptr;
    m_width    = 0;
    m_height   = 0;
}

// ===================================================================
// Read Pixels
// ===================================================================

const PixelReadback* GPUSurface::ReadPixels() {
    if (!m_created || !m_driver) {
        DMME_LOG_ERROR("GPUSurface::ReadPixels: surface not created");
        return nullptr;
    }

    if (!m_driver->ReadbackPixels(m_readback)) {
        DMME_LOG_ERROR("GPUSurface::ReadPixels: driver readback failed");
        return nullptr;
    }

    return &m_readback;
}

// ===================================================================
// Queries
// ===================================================================

bool GPUSurface::IsCreated() const {
    return m_created;
}

int GPUSurface::GetWidth() const {
    return m_width;
}

int GPUSurface::GetHeight() const {
    return m_height;
}

TextureFormat GPUSurface::GetFormat() const {
    return m_format;
}

int GPUSurface::GetSampleCount() const {
    return m_samples;
}

} // namespace renderer
} // namespace core
} // namespace dmme