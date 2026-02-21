#pragma once

#include "RenderTypes.h"
#include "drivers/DriverInterface.h"

#include <memory>

namespace dmme {
namespace core {
namespace renderer {

// GPUSurface manages the off-screen render target where the mascot
// is rendered. It wraps the driver's CreateTarget/ResizeTarget/
// DestroyTarget calls and maintains the associated state (dimensions,
// format, readback buffer).
//
// GPUSurface does NOT own the driver. The driver is owned by
// RenderPipeline and passed to GPUSurface as a reference.
//
// Lifecycle:
//   1. Create(driver, desc)   -- allocate render target via driver
//   2. Resize(w, h)           -- resize when window changes
//   3. ReadPixels()           -- get RGBA pixel data after render
//   4. Destroy()              -- release resources

class GPUSurface {
public:
    GPUSurface();
    ~GPUSurface();

    GPUSurface(const GPUSurface&) = delete;
    GPUSurface& operator=(const GPUSurface&) = delete;

    // Create the render target surface.
    // driver must remain valid for the lifetime of this surface.
    bool Create(IGraphicsDriver* driver, const RenderTargetDesc& desc);

    // Resize the surface. Internally destroys and recreates.
    bool Resize(int width, int height);

    // Destroy the surface and release driver resources.
    void Destroy();

    // Read rendered pixels from GPU to CPU.
    // Returns pointer to internal PixelReadback. Valid until next
    // ReadPixels() or Destroy() call.
    const PixelReadback* ReadPixels();

    // --- Queries ---
    bool IsCreated() const;
    int  GetWidth() const;
    int  GetHeight() const;
    TextureFormat GetFormat() const;
    int  GetSampleCount() const;

private:
    IGraphicsDriver*  m_driver     = nullptr;
    bool              m_created    = false;
    int               m_width      = 0;
    int               m_height     = 0;
    TextureFormat     m_format     = TextureFormat::RGBA8_UNORM;
    int               m_samples    = 1;
    bool              m_hasDepth   = true;
    PixelReadback     m_readback;
};

} // namespace renderer
} // namespace core
} // namespace dmme