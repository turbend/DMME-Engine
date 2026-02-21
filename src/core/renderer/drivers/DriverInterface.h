#pragma once

#include "core/renderer/RenderTypes.h"
#include <cstdint>
#include <memory>
#include <string>

// Forward declare Windows handle type without including Windows.h
// This keeps the interface header clean for all platforms
struct HWND__;
typedef HWND__* HWND;

namespace dmme {
namespace core {
namespace renderer {

// ------------------------------------------------------------------
// IGraphicsDriver -- Pure virtual interface for all GPU backends
//
// Every graphics driver (DX11, DX12, Vulkan, OpenGL) implements
// this interface. The RenderPipeline talks ONLY through this
// interface and never knows which driver is active.
//
// Lifecycle:
//   1. Initialize()    -- create device, context, enumerate GPU
//   2. CreateTarget()  -- create off-screen render target
//   3. [per frame]
//      a. BeginFrame()     -- prepare for rendering
//      b. Clear()          -- clear render target
//      c. SetViewport()    -- set viewport dimensions
//      d. ... draw calls ...
//      e. EndFrame()       -- finalize frame, trigger readback
//   4. ReadbackPixels() -- copy GPU render target to CPU memory
//   5. ResizeTarget()   -- handle window resize
//   6. Shutdown()       -- release all GPU resources
//
// Thread Safety:
//   All methods must be called from the same thread (the render
//   thread). This is a requirement of DX11, OpenGL, and most
//   graphics APIs.
// ------------------------------------------------------------------

class IGraphicsDriver {
public:
    virtual ~IGraphicsDriver() = default;

    // --- Identification ---
    virtual GraphicsAPI GetAPI() const = 0;
    virtual std::string GetDriverName() const = 0;

    // --- Static Capability Check ---
    // Returns true if this driver can run on the current system.
    // Called BEFORE Initialize() to decide which driver to pick.
    virtual bool IsSupported() const = 0;

    // --- Lifecycle ---

    // Initialize the graphics device.
    // hwnd: the TransparentWindow handle (needed for some APIs)
    // config: render configuration
    // Returns true on success.
    virtual bool Initialize(HWND hwnd, const RenderConfig& config) = 0;

    // Release all GPU resources and shut down.
    virtual void Shutdown() = 0;

    // Is the driver currently initialized and ready?
    virtual bool IsInitialized() const = 0;

    // --- GPU Info ---
    virtual GPUAdapterInfo GetAdapterInfo() const = 0;
    virtual DriverCaps     GetCapabilities() const = 0;

    // --- Render Target ---

    // Create the primary off-screen render target.
    // This is where the mascot gets rendered.
    virtual bool CreateTarget(const RenderTargetDesc& desc) = 0;

    // Resize the render target (e.g., window resize).
    virtual bool ResizeTarget(int width, int height) = 0;

    // Destroy the render target.
    virtual void DestroyTarget() = 0;

    // --- Frame Lifecycle ---

    // Prepare for a new frame. Binds render target, resets state.
    virtual bool BeginFrame() = 0;

    // Clear the render target with the given color.
    virtual void Clear(const ClearColor& color) = 0;

    // Set the viewport for rendering.
    virtual void SetViewport(const Viewport& vp) = 0;

    // Finalize the frame. Resolves MSAA if needed, prepares for readback.
    virtual bool EndFrame() = 0;

    // --- Pixel Readback ---

    // Copy the rendered frame from GPU to CPU memory.
    // The output is RGBA 8-bit per channel, top-down layout.
    // This is needed because we composite onto a layered window
    // via UpdateLayeredWindow, not via a swap chain.
    virtual bool ReadbackPixels(PixelReadback& output) = 0;

    // --- Frame Statistics ---
    virtual FrameStats GetFrameStats() const = 0;

    // --- Debug ---
    virtual void SetDebugName(const std::string& name) = 0;
};

// ------------------------------------------------------------------
// Factory function type for creating driver instances
// ------------------------------------------------------------------

using DriverCreateFunc = std::unique_ptr<IGraphicsDriver>(*)();

} // namespace renderer
} // namespace core
} // namespace dmme