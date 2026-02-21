#include "RenderPipeline.h"
#include "drivers/DX11Driver.h"
#include "drivers/OpenGLDriver.h"
#include "utils/Logger.h"

#include <algorithm>
#include <Windows.h>

namespace dmme {
namespace core {
namespace renderer {

// ===================================================================
// Construction / Destruction
// ===================================================================

RenderPipeline::RenderPipeline() {
    RegisterDrivers();
    DMME_LOG_DEBUG("RenderPipeline created with {} registered drivers",
                   m_driverRegistry.size());
}

RenderPipeline::~RenderPipeline() {
    if (m_initialized) {
        Shutdown();
    }
}

// ===================================================================
// Driver Registration
// ===================================================================

void RenderPipeline::RegisterDrivers() {
    // Priority: lower number = tried first
    // DX11 is our primary driver for Windows
    // OpenGL is the software fallback

    m_driverRegistry.push_back({
        GraphicsAPI::DX11,
        &CreateDX11Driver,
        10
    });

    m_driverRegistry.push_back({
        GraphicsAPI::OpenGL,
        &CreateOpenGLDriver,
        100  // lowest priority -- fallback
    });

    // Future drivers:
    // m_driverRegistry.push_back({GraphicsAPI::Vulkan, &CreateVulkanDriver, 5});
    // m_driverRegistry.push_back({GraphicsAPI::DX12,   &CreateDX12Driver,   8});

    // Sort by priority
    std::sort(m_driverRegistry.begin(), m_driverRegistry.end(),
        [](const DriverEntry& a, const DriverEntry& b) {
            return a.priority < b.priority;
        });
}

// ===================================================================
// Initialize
// ===================================================================

bool RenderPipeline::Initialize(HWND hwnd, const RenderConfig& config) {
    if (m_initialized) {
        DMME_LOG_WARN("RenderPipeline::Initialize called on already-initialized pipeline");
        return true;
    }

    DMME_LOG_INFO("Initializing RenderPipeline");
    DMME_LOG_INFO("  Preferred API: {}", GraphicsAPIName(config.preferredAPI));
    DMME_LOG_INFO("  Target size: {}x{}", config.targetWidth, config.targetHeight);
    DMME_LOG_INFO("  Debug layer: {}", config.enableDebugLayer ? "enabled" : "disabled");

    m_config = config;

    if (!SelectAndInitDriver(hwnd, config)) {
        DMME_LOG_CRITICAL("RenderPipeline: no suitable GPU driver found");
        return false;
    }

    // Create primary render surface
    RenderTargetDesc surfaceDesc;
    surfaceDesc.width    = config.targetWidth;
    surfaceDesc.height   = config.targetHeight;
    surfaceDesc.format   = TextureFormat::RGBA8_UNORM;
    surfaceDesc.hasDepth = true;
    surfaceDesc.samples  = 1;  // No MSAA initially

    if (!m_surface.Create(m_driver.get(), surfaceDesc)) {
        DMME_LOG_CRITICAL("RenderPipeline: failed to create primary surface");
        m_driver->Shutdown();
        m_driver.reset();
        return false;
    }

    m_initialized = true;
    m_frameActive = false;

    DMME_LOG_INFO("RenderPipeline initialized successfully");
    DMME_LOG_INFO("  Active API: {}", GraphicsAPIName(m_driver->GetAPI()));
    
    // Convert wstring to UTF-8 string for logging
    const auto& wdesc = m_driver->GetAdapterInfo().description;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wdesc.c_str(), -1, NULL, 0, NULL, NULL);
    std::string desc(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wdesc.c_str(), -1, &desc[0], size_needed, NULL, NULL);
    DMME_LOG_INFO("  GPU: {}", desc);

    return true;
}

// ===================================================================
// Driver Selection
// ===================================================================

bool RenderPipeline::SelectAndInitDriver(HWND hwnd, const RenderConfig& config) {
    // Strategy:
    // 1. If a preferred API is specified, try that first
    // 2. Then fall through the priority-sorted registry
    // 3. Each driver is checked for support before init attempt

    // Try preferred API first if specified
    if (config.preferredAPI != GraphicsAPI::None) {
        for (const auto& entry : m_driverRegistry) {
            if (entry.api == config.preferredAPI) {
                DMME_LOG_INFO("Trying preferred driver: {}", GraphicsAPIName(entry.api));
                auto driver = entry.factory();
                if (driver->IsSupported()) {
                    if (driver->Initialize(hwnd, config)) {
                        m_driver = std::move(driver);
                        DMME_LOG_INFO("Preferred driver {} initialized successfully",
                                      GraphicsAPIName(entry.api));
                        return true;
                    }
                    DMME_LOG_WARN("Preferred driver {} init failed, trying fallbacks",
                                 GraphicsAPIName(entry.api));
                } else {
                    DMME_LOG_WARN("Preferred driver {} not supported on this system",
                                 GraphicsAPIName(entry.api));
                }
                break;
            }
        }
    }

    // Try all drivers in priority order
    for (const auto& entry : m_driverRegistry) {
        // Skip the preferred API if we already tried it
        if (entry.api == config.preferredAPI) {
            continue;
        }

        DMME_LOG_INFO("Trying fallback driver: {} (priority={})",
                      GraphicsAPIName(entry.api), entry.priority);

        auto driver = entry.factory();
        if (!driver->IsSupported()) {
            DMME_LOG_DEBUG("  {} not supported, skipping", GraphicsAPIName(entry.api));
            continue;
        }

        if (driver->Initialize(hwnd, config)) {
            m_driver = std::move(driver);
            DMME_LOG_INFO("Fallback driver {} initialized successfully",
                          GraphicsAPIName(entry.api));
            return true;
        }

        DMME_LOG_WARN("  {} init failed, trying next", GraphicsAPIName(entry.api));
    }

    return false;
}

// ===================================================================
// Shutdown
// ===================================================================

void RenderPipeline::Shutdown() {
    if (!m_initialized) return;

    DMME_LOG_INFO("RenderPipeline shutting down");

    if (m_frameActive) {
        DMME_LOG_WARN("Shutdown called during active frame, forcing end");
        m_frameActive = false;
    }

    m_surface.Destroy();

    if (m_driver) {
        m_driver->Shutdown();
        m_driver.reset();
    }

    m_initialized = false;
    DMME_LOG_INFO("RenderPipeline shutdown complete");
}

bool RenderPipeline::IsInitialized() const {
    return m_initialized;
}

// ===================================================================
// Frame Lifecycle
// ===================================================================

bool RenderPipeline::BeginFrame() {
    if (!m_initialized || !m_driver) {
        return false;
    }

    if (m_frameActive) {
        DMME_LOG_WARN("BeginFrame called while frame already active");
        return false;
    }

    m_frameStart = std::chrono::high_resolution_clock::now();

    if (!m_driver->BeginFrame()) {
        DMME_LOG_ERROR("Driver BeginFrame failed");
        return false;
    }

    // Clear with configured clear color (transparent black by default)
    m_driver->Clear(m_config.clearColor);

    // Set default viewport to full surface
    Viewport vp;
    vp.x      = 0.0f;
    vp.y      = 0.0f;
    vp.width  = static_cast<float>(m_surface.GetWidth());
    vp.height = static_cast<float>(m_surface.GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    m_driver->SetViewport(vp);

    m_frameActive = true;
    return true;
}

bool RenderPipeline::EndFrame() {
    if (!m_initialized || !m_driver) {
        return false;
    }

    if (!m_frameActive) {
        DMME_LOG_WARN("EndFrame called without active frame");
        return false;
    }

    if (!m_driver->EndFrame()) {
        DMME_LOG_ERROR("Driver EndFrame failed");
        m_frameActive = false;
        return false;
    }

    // Calculate CPU frame time
    auto frameEnd = std::chrono::high_resolution_clock::now();
    m_cpuFrameTimeMs = std::chrono::duration<float, std::milli>(
        frameEnd - m_frameStart).count();

    m_lastStats = m_driver->GetFrameStats();
    m_lastStats.frameTimeMs = m_cpuFrameTimeMs;

    m_frameActive = false;
    return true;
}

// ===================================================================
// Pixel Readback
// ===================================================================

const PixelReadback* RenderPipeline::ReadbackFrame() {
    if (!m_initialized) {
        DMME_LOG_ERROR("ReadbackFrame: pipeline not initialized");
        return nullptr;
    }

    if (m_frameActive) {
        DMME_LOG_ERROR("ReadbackFrame: frame still active, call EndFrame first");
        return nullptr;
    }

    return m_surface.ReadPixels();
}

// ===================================================================
// Resize
// ===================================================================

bool RenderPipeline::Resize(int width, int height) {
    if (!m_initialized) return false;

    if (m_frameActive) {
        DMME_LOG_WARN("Resize called during active frame, ignoring");
        return false;
    }

    return m_surface.Resize(width, height);
}

// ===================================================================
// Queries
// ===================================================================

IGraphicsDriver* RenderPipeline::GetDriver() const {
    return m_driver.get();
}

GPUSurface* RenderPipeline::GetSurface() {
    return &m_surface;
}

FrameStats RenderPipeline::GetFrameStats() const {
    return m_lastStats;
}

GPUAdapterInfo RenderPipeline::GetAdapterInfo() const {
    if (m_driver) return m_driver->GetAdapterInfo();
    return {};
}

DriverCaps RenderPipeline::GetCapabilities() const {
    if (m_driver) return m_driver->GetCapabilities();
    return {};
}

GraphicsAPI RenderPipeline::GetActiveAPI() const {
    if (m_driver) return m_driver->GetAPI();
    return GraphicsAPI::None;
}

float RenderPipeline::GetCPUFrameTimeMs() const {
    return m_cpuFrameTimeMs;
}

} // namespace renderer
} // namespace core
} // namespace dmme