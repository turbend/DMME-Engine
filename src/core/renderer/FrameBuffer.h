#pragma once

#include "RenderTypes.h"
#include "drivers/DriverInterface.h"

#include <string>

namespace dmme {
namespace core {
namespace renderer {

// FrameBuffer represents an additional off-screen render target
// used for multi-pass rendering such as shadow maps, post-processing
// effects, or intermediate compositing passes.
//
// FrameBuffer is distinct from GPUSurface:
//   - GPUSurface = the PRIMARY render target where the mascot is drawn
//   - FrameBuffer = SECONDARY targets for multi-pass effects
//
// In the current implementation (Day 2), FrameBuffer tracks metadata
// and delegates actual GPU resource creation to the driver. Future
// days will add shadow map and post-processing FrameBuffers.
//
// FrameBuffer does NOT own the driver.

class FrameBuffer {
public:
    FrameBuffer();
    ~FrameBuffer();

    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    // Create the frame buffer via the driver.
    // name: debug identifier (e.g., "ShadowMap", "PostProcess")
    bool Create(IGraphicsDriver* driver, const RenderTargetDesc& desc,
                const std::string& name);

    // Resize the frame buffer.
    bool Resize(int width, int height);

    // Destroy GPU resources.
    void Destroy();

    // Bind this frame buffer as the active render target.
    // In the current architecture, the driver supports one active
    // target at a time. Binding a FrameBuffer unbinds the primary
    // GPUSurface target and vice versa. Full multi-target support
    // will be added when the driver interface supports it.
    bool Bind();

    // Unbind this frame buffer (restores previous state).
    void Unbind();

    // --- Queries ---
    bool        IsCreated() const;
    int         GetWidth() const;
    int         GetHeight() const;
    std::string GetName() const;

private:
    IGraphicsDriver*  m_driver   = nullptr;
    bool              m_created  = false;
    bool              m_bound    = false;
    int               m_width    = 0;
    int               m_height   = 0;
    TextureFormat     m_format   = TextureFormat::RGBA8_UNORM;
    int               m_samples  = 1;
    bool              m_hasDepth = false;
    std::string       m_name;
};

} // namespace renderer
} // namespace core
} // namespace dmme