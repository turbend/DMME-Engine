#include "OpenGLDriver.h"
#include "utils/Logger.h"

#include <cstring>
#include <algorithm>

namespace dmme {
namespace core {
namespace renderer {

// ===================================================================
// Factory
// ===================================================================

std::unique_ptr<IGraphicsDriver> CreateOpenGLDriver() {
    return std::make_unique<OpenGLDriver>();
}

// ===================================================================
// Construction / Destruction
// ===================================================================

OpenGLDriver::OpenGLDriver() {
    DMME_LOG_DEBUG("OpenGLDriver instance created (software fallback)");
}

OpenGLDriver::~OpenGLDriver() {
    if (m_initialized) {
        Shutdown();
    }
}

// ===================================================================
// Identification
// ===================================================================

GraphicsAPI OpenGLDriver::GetAPI() const {
    return GraphicsAPI::OpenGL;
}

std::string OpenGLDriver::GetDriverName() const {
    return "OpenGL 4.5 (Software Fallback)";
}

// ===================================================================
// Support Check
// ===================================================================

bool OpenGLDriver::IsSupported() const {
    // Software fallback is always supported on any Windows system.
    // This is the last-resort driver.
    return true;
}

// ===================================================================
// Lifecycle
// ===================================================================

bool OpenGLDriver::Initialize(HWND /*hwnd*/, const RenderConfig& config) {
    if (m_initialized) {
        DMME_LOG_WARN("OpenGLDriver::Initialize called on already-initialized driver");
        return true;
    }

    DMME_LOG_INFO("Initializing OpenGL driver (software fallback mode)");

    m_clearColor = config.clearColor;
    m_initialized = true;
    m_frameCounter = 0;
    m_frameStats = {};

    DMME_LOG_INFO("OpenGL driver initialized (software rasterization active)");
    DMME_LOG_WARN("GPU acceleration is NOT available -- performance will be limited");

    return true;
}

void OpenGLDriver::Shutdown() {
    if (!m_initialized) return;

    DMME_LOG_INFO("OpenGL driver shutting down");

    m_internalBuffer.data.clear();
    m_internalBuffer.data.shrink_to_fit();
    m_internalBuffer.width  = 0;
    m_internalBuffer.height = 0;
    m_targetWidth  = 0;
    m_targetHeight = 0;
    m_initialized  = false;

    DMME_LOG_INFO("OpenGL driver shutdown complete");
}

bool OpenGLDriver::IsInitialized() const {
    return m_initialized;
}

// ===================================================================
// GPU Info
// ===================================================================

GPUAdapterInfo OpenGLDriver::GetAdapterInfo() const {
    GPUAdapterInfo info;
    info.description   = L"Software Rasterizer (CPU Fallback)";
    info.vendorId      = 0;
    info.deviceId      = 0;
    info.dedicatedVRAM = 0;
    info.sharedMemory  = 0;
    info.isHardware    = false;
    return info;
}

DriverCaps OpenGLDriver::GetCapabilities() const {
    DriverCaps caps;
    caps.api                    = GraphicsAPI::OpenGL;
    caps.maxTextureSize         = 4096;
    caps.maxRenderTargets       = 1;
    caps.maxMSAASamples         = 1;
    caps.supportsCompute        = false;
    caps.supportsGeometryShader = false;
    caps.supportsTessellation   = false;
    caps.shaderModel            = "none";
    caps.driverVersion          = "software-1.0";
    return caps;
}

// ===================================================================
// Render Target
// ===================================================================

bool OpenGLDriver::CreateTarget(const RenderTargetDesc& desc) {
    if (!m_initialized) {
        DMME_LOG_ERROR("OpenGL CreateTarget: driver not initialized");
        return false;
    }

    if (desc.width <= 0 || desc.height <= 0) {
        DMME_LOG_ERROR("OpenGL CreateTarget: invalid dimensions {}x{}",
                       desc.width, desc.height);
        return false;
    }

    m_targetWidth  = desc.width;
    m_targetHeight = desc.height;

    m_internalBuffer.Allocate(m_targetWidth, m_targetHeight);

    DMME_LOG_INFO("OpenGL render target created (software): {}x{}",
                  m_targetWidth, m_targetHeight);
    return true;
}

bool OpenGLDriver::ResizeTarget(int width, int height) {
    if (!m_initialized) return false;
    if (width <= 0 || height <= 0) return false;
    if (width == m_targetWidth && height == m_targetHeight) return true;

    DMME_LOG_INFO("OpenGL resizing: {}x{} -> {}x{}",
                  m_targetWidth, m_targetHeight, width, height);

    m_targetWidth  = width;
    m_targetHeight = height;
    m_internalBuffer.Allocate(width, height);

    return true;
}

void OpenGLDriver::DestroyTarget() {
    m_internalBuffer.data.clear();
    m_internalBuffer.width  = 0;
    m_internalBuffer.height = 0;
    m_targetWidth  = 0;
    m_targetHeight = 0;
}

// ===================================================================
// Frame Lifecycle
// ===================================================================

bool OpenGLDriver::BeginFrame() {
    if (!m_initialized || m_targetWidth <= 0 || m_targetHeight <= 0) {
        return false;
    }

    m_frameStats.drawCalls = 0;
    m_frameStats.trianglesRendered = 0;

    return true;
}

void OpenGLDriver::Clear(const ClearColor& color) {
    m_clearColor = color;

    if (!m_internalBuffer.IsValid()) return;

    // Fill the entire buffer with the clear color
    uint8_t r = static_cast<uint8_t>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint8_t g = static_cast<uint8_t>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint8_t b = static_cast<uint8_t>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint8_t a = static_cast<uint8_t>(std::clamp(color.a, 0.0f, 1.0f) * 255.0f + 0.5f);

    uint8_t* pixels = m_internalBuffer.data.data();
    const int totalPixels = m_targetWidth * m_targetHeight;

    for (int i = 0; i < totalPixels; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
}

void OpenGLDriver::SetViewport(const Viewport& /*vp*/) {
    // Software fallback: viewport is implicitly the full target
}

bool OpenGLDriver::EndFrame() {
    if (!m_initialized) return false;

    m_frameCounter++;
    m_frameStats.frameNumber = m_frameCounter;
    m_frameStats.gpuTimeMs = 0.0f;

    return true;
}

// ===================================================================
// Pixel Readback
// ===================================================================

bool OpenGLDriver::ReadbackPixels(PixelReadback& output) {
    if (!m_internalBuffer.IsValid()) {
        DMME_LOG_ERROR("OpenGL ReadbackPixels: no valid internal buffer");
        return false;
    }

    output.Allocate(m_targetWidth, m_targetHeight);
    std::memcpy(output.data.data(), m_internalBuffer.data.data(),
                m_internalBuffer.data.size());

    return true;
}

// ===================================================================
// Frame Stats
// ===================================================================

FrameStats OpenGLDriver::GetFrameStats() const {
    return m_frameStats;
}

// ===================================================================
// Debug
// ===================================================================

void OpenGLDriver::SetDebugName(const std::string& name) {
    (void)name;  // Suppress unreferenced parameter warning for non-OpenGL platforms
    DMME_LOG_DEBUG("OpenGL debug name set (OpenGL stub)");
}

} // namespace renderer
} // namespace core
} // namespace dmme