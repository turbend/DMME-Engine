#include "FrameBuffer.h"
#include "utils/Logger.h"

namespace dmme {
namespace core {
namespace renderer {

// ===================================================================
// Construction / Destruction
// ===================================================================

FrameBuffer::FrameBuffer() {
    DMME_LOG_DEBUG("FrameBuffer created");
}

FrameBuffer::~FrameBuffer() {
    if (m_created) {
        Destroy();
    }
}

// ===================================================================
// Create
// ===================================================================

bool FrameBuffer::Create(IGraphicsDriver* driver, const RenderTargetDesc& desc,
                          const std::string& name) {
    if (!driver) {
        DMME_LOG_ERROR("FrameBuffer::Create '{}': null driver", name);
        return false;
    }

    if (!driver->IsInitialized()) {
        DMME_LOG_ERROR("FrameBuffer::Create '{}': driver not initialized", name);
        return false;
    }

    if (desc.width <= 0 || desc.height <= 0) {
        DMME_LOG_ERROR("FrameBuffer::Create '{}': invalid dimensions {}x{}",
                       name, desc.width, desc.height);
        return false;
    }

    if (m_created) {
        Destroy();
    }

    m_driver   = driver;
    m_name     = name;
    m_width    = desc.width;
    m_height   = desc.height;
    m_format   = desc.format;
    m_samples  = desc.samples;
    m_hasDepth = desc.hasDepth;

    // In the current single-target architecture, we track the
    // FrameBuffer metadata. When the driver is extended to support
    // multiple render targets, the actual GPU resource creation
    // will happen here. For now, FrameBuffer serves as a validated
    // configuration record that the pipeline can query.

    m_created = true;
    DMME_LOG_INFO("FrameBuffer '{}' created: {}x{} format={} depth={}",
                  m_name, m_width, m_height, static_cast<int>(m_format), m_hasDepth);
    return true;
}

// ===================================================================
// Resize
// ===================================================================

bool FrameBuffer::Resize(int width, int height) {
    if (!m_created) {
        DMME_LOG_ERROR("FrameBuffer::Resize '{}': not created", m_name);
        return false;
    }

    if (width <= 0 || height <= 0) {
        DMME_LOG_ERROR("FrameBuffer::Resize '{}': invalid {}x{}", m_name, width, height);
        return false;
    }

    if (width == m_width && height == m_height) {
        return true;
    }

    DMME_LOG_INFO("FrameBuffer '{}' resizing: {}x{} -> {}x{}",
                  m_name, m_width, m_height, width, height);

    m_width  = width;
    m_height = height;

    return true;
}

// ===================================================================
// Destroy
// ===================================================================

void FrameBuffer::Destroy() {
    if (!m_created) return;

    DMME_LOG_INFO("FrameBuffer '{}' destroyed", m_name);

    m_created  = false;
    m_bound    = false;
    m_driver   = nullptr;
    m_width    = 0;
    m_height   = 0;
}

// ===================================================================
// Bind / Unbind
// ===================================================================

bool FrameBuffer::Bind() {
    if (!m_created || !m_driver) {
        DMME_LOG_ERROR("FrameBuffer::Bind '{}': not ready", m_name);
        return false;
    }

    // In current single-target architecture, binding a FrameBuffer
    // sets the viewport to its dimensions. Multi-target binding
    // will be implemented when the driver supports it.

    Viewport vp;
    vp.x      = 0.0f;
    vp.y      = 0.0f;
    vp.width  = static_cast<float>(m_width);
    vp.height = static_cast<float>(m_height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    m_driver->SetViewport(vp);
    m_bound = true;

    DMME_LOG_DEBUG("FrameBuffer '{}' bound ({}x{})", m_name, m_width, m_height);
    return true;
}

void FrameBuffer::Unbind() {
    if (!m_bound) return;
    m_bound = false;
    DMME_LOG_DEBUG("FrameBuffer '{}' unbound", m_name);
}

// ===================================================================
// Queries
// ===================================================================

bool FrameBuffer::IsCreated() const {
    return m_created;
}

int FrameBuffer::GetWidth() const {
    return m_width;
}

int FrameBuffer::GetHeight() const {
    return m_height;
}

std::string FrameBuffer::GetName() const {
    return m_name;
}

} // namespace renderer
} // namespace core
} // namespace dmme