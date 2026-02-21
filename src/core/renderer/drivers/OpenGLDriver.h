#pragma once

#include "DriverInterface.h"
#include <string>

namespace dmme {
namespace core {
namespace renderer {

// OpenGLDriver serves as the fallback driver for systems where
// DirectX 11 is unavailable. In this initial implementation it
// provides a software-rasterized solid color output so the engine
// pipeline remains functional even without GPU acceleration.
//
// Future iterations will implement full OpenGL 4.5 rendering.
// For now, the critical contract methods (BeginFrame, EndFrame,
// ReadbackPixels) produce valid RGBA output that the layered
// window can composite.

class OpenGLDriver final : public IGraphicsDriver {
public:
    OpenGLDriver();
    ~OpenGLDriver() override;

    OpenGLDriver(const OpenGLDriver&) = delete;
    OpenGLDriver& operator=(const OpenGLDriver&) = delete;

    // --- IGraphicsDriver ---
    GraphicsAPI GetAPI() const override;
    std::string GetDriverName() const override;
    bool IsSupported() const override;
    bool Initialize(HWND hwnd, const RenderConfig& config) override;
    void Shutdown() override;
    bool IsInitialized() const override;
    GPUAdapterInfo GetAdapterInfo() const override;
    DriverCaps     GetCapabilities() const override;
    bool CreateTarget(const RenderTargetDesc& desc) override;
    bool ResizeTarget(int width, int height) override;
    void DestroyTarget() override;
    bool BeginFrame() override;
    void Clear(const ClearColor& color) override;
    void SetViewport(const Viewport& vp) override;
    bool EndFrame() override;
    bool ReadbackPixels(PixelReadback& output) override;
    FrameStats GetFrameStats() const override;
    void SetDebugName(const std::string& name) override;

private:
    bool           m_initialized = false;
    int            m_targetWidth  = 0;
    int            m_targetHeight = 0;
    ClearColor     m_clearColor;
    PixelReadback  m_internalBuffer;
    FrameStats     m_frameStats;
    uint64_t       m_frameCounter = 0;
};

// Factory function
std::unique_ptr<IGraphicsDriver> CreateOpenGLDriver();

} // namespace renderer
} // namespace core
} // namespace dmme