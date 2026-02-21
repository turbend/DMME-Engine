#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace dmme {
namespace core {
namespace renderer {

// ------------------------------------------------------------------
// Graphics API Identifier
// ------------------------------------------------------------------

enum class GraphicsAPI : uint8_t {
    None     = 0,
    DX11     = 1,
    DX12     = 2,
    Vulkan   = 3,
    OpenGL   = 4
};

inline const char* GraphicsAPIName(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::DX11:   return "DirectX 11";
        case GraphicsAPI::DX12:   return "DirectX 12";
        case GraphicsAPI::Vulkan: return "Vulkan";
        case GraphicsAPI::OpenGL: return "OpenGL 4.5";
        default:                  return "None";
    }
}

// ------------------------------------------------------------------
// GPU Adapter Info (populated during driver initialization)
// ------------------------------------------------------------------

struct GPUAdapterInfo {
    std::wstring description;
    uint32_t     vendorId       = 0;
    uint32_t     deviceId       = 0;
    size_t       dedicatedVRAM  = 0;  // bytes
    size_t       sharedMemory   = 0;  // bytes
    bool         isHardware     = false;
};

// ------------------------------------------------------------------
// Render Target Description
// ------------------------------------------------------------------

enum class TextureFormat : uint8_t {
    RGBA8_UNORM  = 0,   // 8 bits per channel, normalized
    RGBA16_FLOAT = 1,   // 16 bits per channel, floating point
    DEPTH24_STENCIL8 = 2,
    DEPTH32_FLOAT    = 3
};

struct RenderTargetDesc {
    int           width    = 0;
    int           height   = 0;
    TextureFormat format   = TextureFormat::RGBA8_UNORM;
    bool          hasDepth = true;
    int           samples  = 1;  // MSAA sample count (1 = no MSAA)
};

// ------------------------------------------------------------------
// Clear Color
// ------------------------------------------------------------------

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// ------------------------------------------------------------------
// Viewport
// ------------------------------------------------------------------

struct Viewport {
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

// ------------------------------------------------------------------
// Frame Statistics
// ------------------------------------------------------------------

struct FrameStats {
    uint64_t frameNumber      = 0;
    float    frameTimeMs      = 0.0f;
    float    gpuTimeMs        = 0.0f;
    int      drawCalls        = 0;
    int      trianglesRendered = 0;
    size_t   vramUsedBytes    = 0;
};

// ------------------------------------------------------------------
// Driver Capabilities
// ------------------------------------------------------------------

struct DriverCaps {
    GraphicsAPI  api             = GraphicsAPI::None;
    int          maxTextureSize  = 0;
    int          maxRenderTargets = 0;
    int          maxMSAASamples  = 0;
    bool         supportsCompute = false;
    bool         supportsGeometryShader = false;
    bool         supportsTessellation   = false;
    std::string  shaderModel;      // e.g., "5_0", "5_1"
    std::string  driverVersion;
};

// ------------------------------------------------------------------
// Pixel Readback Buffer
// ------------------------------------------------------------------

struct PixelReadback {
    std::vector<uint8_t> data;   // RGBA 8-bit per channel
    int width  = 0;
    int height = 0;

    bool IsValid() const {
        return !data.empty() && width > 0 && height > 0 &&
               data.size() == static_cast<size_t>(width) * height * 4;
    }

    void Allocate(int w, int h) {
        width  = w;
        height = h;
        data.resize(static_cast<size_t>(w) * h * 4, 0);
    }

    void Clear() {
        std::fill(data.begin(), data.end(), 0);
    }
};

// ------------------------------------------------------------------
// Render Pipeline Configuration
// ------------------------------------------------------------------

struct RenderConfig {
    GraphicsAPI preferredAPI   = GraphicsAPI::DX11;
    bool        enableDebugLayer = false;  // DX11/DX12 debug validation
    bool        enableVSync      = false;  // We use off-screen, no vsync needed
    int         targetWidth      = 512;
    int         targetHeight     = 512;
    ClearColor  clearColor       = {0.0f, 0.0f, 0.0f, 0.0f};  // transparent black
};

} // namespace renderer
} // namespace core
} // namespace dmme