#pragma once

#include "RenderTypes.h"
#include "GPUSurface.h"
#include "FrameBuffer.h"
#include "drivers/DriverInterface.h"

#include <memory>
#include <vector>
#include <functional>
#include <chrono>

// Forward declarations
struct HWND__;
typedef HWND__* HWND;

namespace dmme {
namespace core {
namespace renderer {

// RenderPipeline is the top-level orchestrator for all rendering.
//
// Responsibilities:
//   1. Driver selection: detect best available GPU driver
//   2. Driver lifecycle: init, shutdown
//   3. Primary render surface: create, resize
//   4. Frame lifecycle: BeginFrame -> [render commands] -> EndFrame
//   5. Pixel readback: GPU -> CPU for layered window compositing
//   6. Frame timing and statistics
//
// The pipeline does NOT know about meshes, materials, or scene
// objects. It provides the raw frame lifecycle. Higher-level systems
// (model loading, lighting, animation) will call draw commands
// through the active driver in future days.
//
// Usage:
//   RenderPipeline pipeline;
//   pipeline.Initialize(hwnd, config);
//   // per frame:
//   pipeline.BeginFrame();
//   // ... issue draw calls via pipeline.GetDriver() ...
//   pipeline.EndFrame();
//   auto* pixels = pipeline.ReadbackFrame();
//   window.UpdateFrame(pixels->data.data(), pixels->width, pixels->height);

class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    // --- Lifecycle ---

    // Initialize the rendering pipeline.
    // Selects the best available driver, creates device, allocates
    // the primary render surface.
    bool Initialize(HWND hwnd, const RenderConfig& config);

    // Shut down the pipeline and release all GPU resources.
    void Shutdown();

    bool IsInitialized() const;

    // --- Frame Lifecycle ---

    // Begin a new frame. Clears the render target.
    bool BeginFrame();

    // End the frame. Finalizes GPU work.
    bool EndFrame();

    // Read the rendered frame from GPU to CPU.
    // Returns pointer to internal readback buffer (valid until next
    // ReadbackFrame or Shutdown call).
    const PixelReadback* ReadbackFrame();

    // --- Resize ---

    // Resize the render target. Call when window size changes.
    bool Resize(int width, int height);

    // --- Queries ---

    // Get the active driver interface for issuing draw commands.
    IGraphicsDriver* GetDriver() const;

    // Get the primary GPU surface.
    GPUSurface* GetSurface();

    // Get current frame statistics.
    FrameStats GetFrameStats() const;

    // Get GPU adapter info.
    GPUAdapterInfo GetAdapterInfo() const;

    // Get driver capabilities.
    DriverCaps GetCapabilities() const;

    // Which graphics API is active?
    GraphicsAPI GetActiveAPI() const;

    // Get CPU-side frame time in milliseconds.
    float GetCPUFrameTimeMs() const;

private:
    // Driver selection: try drivers in priority order
    bool SelectAndInitDriver(HWND hwnd, const RenderConfig& config);

    // Registered driver factories
    struct DriverEntry {
        GraphicsAPI      api;
        DriverCreateFunc factory;
        int              priority;  // lower = higher priority
    };

    void RegisterDrivers();

    // --- Members ---
    std::unique_ptr<IGraphicsDriver>  m_driver;
    GPUSurface                        m_surface;
    RenderConfig                      m_config;
    bool                              m_initialized = false;
    bool                              m_frameActive = false;

    std::vector<DriverEntry>          m_driverRegistry;

    // --- Timing ---
    std::chrono::high_resolution_clock::time_point m_frameStart;
    float m_cpuFrameTimeMs = 0.0f;

    // --- Stats ---
    FrameStats m_lastStats;
};

} // namespace renderer
} // namespace core
} // namespace dmme