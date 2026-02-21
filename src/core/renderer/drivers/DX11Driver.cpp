#include "DX11Driver.h"
#include "utils/Logger.h"

#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <cstring>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

namespace dmme {
namespace core {
namespace renderer {

// ===================================================================
// Factory
// ===================================================================

std::unique_ptr<IGraphicsDriver> CreateDX11Driver() {
    return std::make_unique<DX11Driver>();
}

// ===================================================================
// Construction / Destruction
// ===================================================================

DX11Driver::DX11Driver() {
    DMME_LOG_DEBUG("DX11Driver instance created");
}

DX11Driver::~DX11Driver() {
    if (m_initialized) {
        Shutdown();
    }
}

// ===================================================================
// IGraphicsDriver -- Identification
// ===================================================================

GraphicsAPI DX11Driver::GetAPI() const {
    return GraphicsAPI::DX11;
}

std::string DX11Driver::GetDriverName() const {
    return "DirectX 11";
}

// ===================================================================
// IGraphicsDriver -- Support Check
// ===================================================================

bool DX11Driver::IsSupported() const {
    // Try to create a DXGI factory -- if this fails, DX11 is not available
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        DMME_LOG_DEBUG("DX11 not supported: CreateDXGIFactory1 failed");
        return false;
    }

    // Check if any hardware adapter exists
    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr)) {
        DMME_LOG_DEBUG("DX11 not supported: no adapters found");
        return false;
    }

    // Try to check D3D11 feature level
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL achievedLevel;
    hr = D3D11CreateDevice(
        adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        nullptr,      // don't keep device
        &achievedLevel,
        nullptr       // don't keep context
    );

    if (FAILED(hr)) {
        DMME_LOG_DEBUG("DX11 not supported: D3D11CreateDevice check failed");
        return false;
    }

    DMME_LOG_DEBUG("DX11 supported: feature level {:#x}", static_cast<unsigned>(achievedLevel));
    return true;
}

// ===================================================================
// IGraphicsDriver -- Initialize
// ===================================================================

bool DX11Driver::Initialize(HWND hwnd, const RenderConfig& config) {
    if (m_initialized) {
        DMME_LOG_WARN("DX11Driver::Initialize called on already-initialized driver");
        return true;
    }

    DMME_LOG_INFO("Initializing DX11 driver");
    m_hwnd = hwnd;
    m_debugEnabled = config.enableDebugLayer;

    if (!CreateDevice(hwnd, config.enableDebugLayer)) {
        return false;
    }

    if (!EnumerateAdapter()) {
        Shutdown();
        return false;
    }

    QueryCapabilities();

    m_initialized = true;
    m_frameCounter = 0;
    m_frameStats = {};

    DMME_LOG_INFO("DX11 driver initialized successfully");
    
    // Convert wstring to UTF-8 string for logging
    const auto& wdesc = m_adapterInfo.description;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wdesc.c_str(), -1, NULL, 0, NULL, NULL);
    std::string desc(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wdesc.c_str(), -1, &desc[0], size_needed, NULL, NULL);
    DMME_LOG_INFO("  GPU: {} (VRAM: {} MB)",
                  desc,
                  m_adapterInfo.dedicatedVRAM / (1024 * 1024));
    DMME_LOG_INFO("  Feature Level: {}", m_caps.shaderModel);

    return true;
}

// ===================================================================
// IGraphicsDriver -- Shutdown
// ===================================================================

void DX11Driver::Shutdown() {
    if (!m_initialized) return;

    DMME_LOG_INFO("DX11 driver shutting down");

    DestroyTarget();

    m_disjointQuery.Reset();
    m_timestampBegin.Reset();
    m_timestampEnd.Reset();

    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }

    m_context.Reset();
    m_adapter.Reset();
    m_dxgiFactory.Reset();

    // Debug layer: report live objects if debug was enabled
    if (m_debugEnabled && m_device) {
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug))) {
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        }
    }

    m_device.Reset();
    m_initialized = false;

    DMME_LOG_INFO("DX11 driver shutdown complete");
}

bool DX11Driver::IsInitialized() const {
    return m_initialized;
}

// ===================================================================
// IGraphicsDriver -- GPU Info
// ===================================================================

GPUAdapterInfo DX11Driver::GetAdapterInfo() const {
    return m_adapterInfo;
}

DriverCaps DX11Driver::GetCapabilities() const {
    return m_caps;
}

// ===================================================================
// IGraphicsDriver -- Render Target
// ===================================================================

bool DX11Driver::CreateTarget(const RenderTargetDesc& desc) {
    if (!m_initialized) {
        DMME_LOG_ERROR("CreateTarget called on uninitialized DX11 driver");
        return false;
    }

    if (desc.width <= 0 || desc.height <= 0) {
        DMME_LOG_ERROR("CreateTarget: invalid dimensions {}x{}", desc.width, desc.height);
        return false;
    }

    // Release existing target first
    DestroyTarget();

    m_targetWidth  = desc.width;
    m_targetHeight = desc.height;
    m_targetFormat = desc.format;
    m_sampleCount  = desc.samples;

    if (!CreateRenderTarget(desc.width, desc.height, desc.format, desc.samples)) {
        return false;
    }

    if (desc.hasDepth) {
        if (!CreateDepthStencil(desc.width, desc.height, desc.samples)) {
            ReleaseRenderTarget();
            return false;
        }
    }

    if (!CreateStagingTexture(desc.width, desc.height)) {
        ReleaseRenderTarget();
        return false;
    }

    // Create GPU timing queries
    D3D11_QUERY_DESC qd{};
    qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    m_device->CreateQuery(&qd, &m_disjointQuery);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    m_device->CreateQuery(&qd, &m_timestampBegin);
    m_device->CreateQuery(&qd, &m_timestampEnd);

    DMME_LOG_INFO("DX11 render target created: {}x{} format={} samples={}",
                  desc.width, desc.height, static_cast<int>(desc.format), desc.samples);
    return true;
}

bool DX11Driver::ResizeTarget(int width, int height) {
    if (!m_initialized) return false;
    if (width <= 0 || height <= 0) return false;
    if (width == m_targetWidth && height == m_targetHeight) return true;

    DMME_LOG_INFO("DX11 resizing render target: {}x{} -> {}x{}", m_targetWidth, m_targetHeight, width, height);

    RenderTargetDesc desc;
    desc.width   = width;
    desc.height  = height;
    desc.format  = m_targetFormat;
    desc.hasDepth = (m_dsv != nullptr);
    desc.samples = m_sampleCount;

    return CreateTarget(desc);
}

void DX11Driver::DestroyTarget() {
    if (m_context) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_rtv.Reset();
    m_renderTexture.Reset();
    m_dsv.Reset();
    m_depthTexture.Reset();
    m_stagingTexture.Reset();
    m_targetWidth  = 0;
    m_targetHeight = 0;
}

// ===================================================================
// IGraphicsDriver -- Frame Lifecycle
// ===================================================================

bool DX11Driver::BeginFrame() {
    if (!m_initialized || !m_rtv) {
        return false;
    }

    // Begin GPU timing
    if (m_disjointQuery) {
        m_context->Begin(m_disjointQuery.Get());
    }
    if (m_timestampBegin) {
        m_context->End(m_timestampBegin.Get());
    }

    // Bind render target
    ID3D11RenderTargetView* rtvs[] = { m_rtv.Get() };
    m_context->OMSetRenderTargets(1, rtvs, m_dsv.Get());

    m_frameStats.drawCalls = 0;
    m_frameStats.trianglesRendered = 0;

    return true;
}

void DX11Driver::Clear(const ClearColor& color) {
    if (!m_rtv) return;

    float clearColor[4] = { color.r, color.g, color.b, color.a };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);

    if (m_dsv) {
        m_context->ClearDepthStencilView(m_dsv.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

void DX11Driver::SetViewport(const Viewport& vp) {
    D3D11_VIEWPORT d3dVP;
    d3dVP.TopLeftX = vp.x;
    d3dVP.TopLeftY = vp.y;
    d3dVP.Width    = vp.width;
    d3dVP.Height   = vp.height;
    d3dVP.MinDepth = vp.minDepth;
    d3dVP.MaxDepth = vp.maxDepth;

    m_context->RSSetViewports(1, &d3dVP);
}

bool DX11Driver::EndFrame() {
    if (!m_initialized || !m_rtv) {
        return false;
    }

    // End GPU timing
    if (m_timestampEnd) {
        m_context->End(m_timestampEnd.Get());
    }
    if (m_disjointQuery) {
        m_context->End(m_disjointQuery.Get());
    }

    // Unbind render target
    m_context->OMSetRenderTargets(0, nullptr, nullptr);

    // Collect GPU timing
    if (m_disjointQuery && m_timestampBegin && m_timestampEnd) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        while (m_context->GetData(m_disjointQuery.Get(), &disjointData,
               sizeof(disjointData), 0) == S_FALSE) {
            // Spin wait -- acceptable for timing queries
        }

        if (!disjointData.Disjoint) {
            UINT64 tsBegin = 0, tsEnd = 0;
            m_context->GetData(m_timestampBegin.Get(), &tsBegin, sizeof(tsBegin), 0);
            m_context->GetData(m_timestampEnd.Get(), &tsEnd, sizeof(tsEnd), 0);
            float gpuMs = static_cast<float>(tsEnd - tsBegin) /
                          static_cast<float>(disjointData.Frequency) * 1000.0f;
            m_frameStats.gpuTimeMs = gpuMs;
        }
    }

    m_frameCounter++;
    m_frameStats.frameNumber = m_frameCounter;

    return true;
}

// ===================================================================
// IGraphicsDriver -- Pixel Readback
// ===================================================================

bool DX11Driver::ReadbackPixels(PixelReadback& output) {
    if (!m_initialized || !m_renderTexture || !m_stagingTexture) {
        DMME_LOG_ERROR("ReadbackPixels: driver not ready");
        return false;
    }

    // Copy render texture to staging texture (GPU->GPU, then mappable)
    m_context->CopyResource(m_stagingTexture.Get(), m_renderTexture.Get());

    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        DMME_LOG_ERROR("ReadbackPixels: Map failed: {}", HRToString(hr));
        return false;
    }

    // Ensure output buffer is allocated
    output.Allocate(m_targetWidth, m_targetHeight);

    // Copy row by row (mapped.RowPitch may differ from width*4)
    const uint8_t* srcData = static_cast<const uint8_t*>(mapped.pData);
    uint8_t*       dstData = output.data.data();
    const int      dstPitch = m_targetWidth * 4;

    for (int row = 0; row < m_targetHeight; ++row) {
        const uint8_t* srcRow = srcData + row * mapped.RowPitch;
        uint8_t*       dstRow = dstData + row * dstPitch;

        // DX11 RGBA8 is DXGI_FORMAT_R8G8B8A8_UNORM -> RGBA byte order
        // Our PixelReadback expects RGBA -> direct copy
        std::memcpy(dstRow, srcRow, static_cast<size_t>(dstPitch));
    }

    m_context->Unmap(m_stagingTexture.Get(), 0);

    return true;
}

// ===================================================================
// IGraphicsDriver -- Frame Stats
// ===================================================================

FrameStats DX11Driver::GetFrameStats() const {
    return m_frameStats;
}

// ===================================================================
// IGraphicsDriver -- Debug
// ===================================================================

void DX11Driver::SetDebugName(const std::string& name) {
    if (m_renderTexture) {
        m_renderTexture->SetPrivateData(WKPDID_D3DDebugObjectName,
            static_cast<UINT>(name.size()), name.c_str());
    }
}

// ===================================================================
// DX11 Specific Accessors
// ===================================================================

ID3D11Device* DX11Driver::GetDevice() const {
    return m_device.Get();
}

ID3D11DeviceContext* DX11Driver::GetContext() const {
    return m_context.Get();
}

// ===================================================================
// Internal: Create D3D11 Device
// ===================================================================

bool DX11Driver::CreateDevice(HWND /*hwnd*/, bool enableDebug) {
    UINT createFlags = 0;
    if (enableDebug) {
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
        DMME_LOG_INFO("DX11 debug layer ENABLED");
    }

    // Create DXGI Factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory));
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateDXGIFactory1 failed: {}", HRToString(hr));
        return false;
    }

    // Enumerate adapters -- pick the first hardware adapter
    hr = m_dxgiFactory->EnumAdapters1(0, &m_adapter);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("No DXGI adapters found: {}", HRToString(hr));
        return false;
    }

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    hr = D3D11CreateDevice(
        m_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,   // Unknown because we specified an adapter
        nullptr,                    // no software rasterizer
        createFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &m_featureLevel,
        &m_context
    );

    if (FAILED(hr)) {
        // Retry without debug layer if it failed (debug layer may not be installed)
        if (enableDebug) {
            DMME_LOG_WARN("DX11 device creation with debug layer failed, retrying without");
            createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            m_debugEnabled = false;

            hr = D3D11CreateDevice(
                m_adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                createFlags,
                featureLevels,
                _countof(featureLevels),
                D3D11_SDK_VERSION,
                &m_device,
                &m_featureLevel,
                &m_context
            );
        }

        if (FAILED(hr)) {
            DMME_LOG_CRITICAL("D3D11CreateDevice failed: {}", HRToString(hr));
            return false;
        }
    }

    DMME_LOG_DEBUG("D3D11 device created with feature level {:#x}",
                   static_cast<unsigned>(m_featureLevel));
    return true;
}

// ===================================================================
// Internal: Enumerate Adapter Info
// ===================================================================

bool DX11Driver::EnumerateAdapter() {
    if (!m_adapter) return false;

    DXGI_ADAPTER_DESC1 desc{};
    HRESULT hr = m_adapter->GetDesc1(&desc);
    if (FAILED(hr)) {
        DMME_LOG_ERROR("GetDesc1 failed: {}", HRToString(hr));
        return false;
    }

    m_adapterInfo.description   = desc.Description;
    m_adapterInfo.vendorId      = desc.VendorId;
    m_adapterInfo.deviceId      = desc.DeviceId;
    m_adapterInfo.dedicatedVRAM = desc.DedicatedVideoMemory;
    m_adapterInfo.sharedMemory  = desc.SharedSystemMemory;
    m_adapterInfo.isHardware    = !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);

    return true;
}

// ===================================================================
// Internal: Query Capabilities
// ===================================================================

void DX11Driver::QueryCapabilities() {
    m_caps.api = GraphicsAPI::DX11;
    m_caps.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;  // 16384
    m_caps.maxRenderTargets = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;  // 8
    m_caps.supportsCompute = (m_featureLevel >= D3D_FEATURE_LEVEL_11_0);
    m_caps.supportsGeometryShader = (m_featureLevel >= D3D_FEATURE_LEVEL_10_0);
    m_caps.supportsTessellation = (m_featureLevel >= D3D_FEATURE_LEVEL_11_0);

    // MSAA support check for RGBA8
    UINT msaaQuality = 0;
    m_caps.maxMSAASamples = 1;
    for (int samples = 8; samples >= 2; samples /= 2) {
        HRESULT hr = m_device->CheckMultisampleQualityLevels(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            static_cast<UINT>(samples),
            &msaaQuality
        );
        if (SUCCEEDED(hr) && msaaQuality > 0) {
            m_caps.maxMSAASamples = samples;
            break;
        }
    }

    switch (m_featureLevel) {
        case D3D_FEATURE_LEVEL_11_1: m_caps.shaderModel = "5_1"; break;
        case D3D_FEATURE_LEVEL_11_0: m_caps.shaderModel = "5_0"; break;
        case D3D_FEATURE_LEVEL_10_1: m_caps.shaderModel = "4_1"; break;
        case D3D_FEATURE_LEVEL_10_0: m_caps.shaderModel = "4_0"; break;
        default: m_caps.shaderModel = "unknown"; break;
    }

    DMME_LOG_INFO("DX11 caps: maxTex={} maxRT={} maxMSAA={} compute={} sm={}",
                  m_caps.maxTextureSize, m_caps.maxRenderTargets,
                  m_caps.maxMSAASamples, m_caps.supportsCompute,
                  m_caps.shaderModel);
}

// ===================================================================
// Internal: Create Render Target Texture + RTV
// ===================================================================

bool DX11Driver::CreateRenderTarget(int w, int h, TextureFormat fmt, int samples) {
    DXGI_FORMAT dxFormat;
    switch (fmt) {
        case TextureFormat::RGBA8_UNORM:
            dxFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case TextureFormat::RGBA16_FLOAT:
            dxFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        default:
            DMME_LOG_ERROR("Unsupported render target format: {}",
                           static_cast<int>(fmt));
            return false;
    }

    // Validate MSAA
    if (samples > 1) {
        UINT quality = 0;
        HRESULT hr = m_device->CheckMultisampleQualityLevels(
            dxFormat, static_cast<UINT>(samples), &quality);
        if (FAILED(hr) || quality == 0) {
            DMME_LOG_WARN("MSAA {}x not supported for format, falling back to 1x", samples);
            samples = 1;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width              = static_cast<UINT>(w);
    texDesc.Height             = static_cast<UINT>(h);
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = 1;
    texDesc.Format             = dxFormat;
    texDesc.SampleDesc.Count   = static_cast<UINT>(samples);
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags     = 0;
    texDesc.MiscFlags          = 0;

    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_renderTexture);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateTexture2D (render target) failed: {}", HRToString(hr));
        return false;
    }

    hr = m_device->CreateRenderTargetView(m_renderTexture.Get(), nullptr, &m_rtv);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateRenderTargetView failed: {}", HRToString(hr));
        m_renderTexture.Reset();
        return false;
    }

    m_sampleCount = samples;
    return true;
}

// ===================================================================
// Internal: Create Depth Stencil
// ===================================================================

bool DX11Driver::CreateDepthStencil(int w, int h, int samples) {
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width              = static_cast<UINT>(w);
    depthDesc.Height             = static_cast<UINT>(h);
    depthDesc.MipLevels          = 1;
    depthDesc.ArraySize          = 1;
    depthDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count   = static_cast<UINT>(samples);
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage              = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.CPUAccessFlags     = 0;
    depthDesc.MiscFlags          = 0;

    HRESULT hr = m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthTexture);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateTexture2D (depth stencil) failed: {}", HRToString(hr));
        return false;
    }

    hr = m_device->CreateDepthStencilView(m_depthTexture.Get(), nullptr, &m_dsv);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateDepthStencilView failed: {}", HRToString(hr));
        m_depthTexture.Reset();
        return false;
    }

    return true;
}

// ===================================================================
// Internal: Create Staging Texture (for CPU readback)
// ===================================================================

bool DX11Driver::CreateStagingTexture(int w, int h) {
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width              = static_cast<UINT>(w);
    stagingDesc.Height             = static_cast<UINT>(h);
    stagingDesc.MipLevels          = 1;
    stagingDesc.ArraySize          = 1;
    stagingDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    stagingDesc.SampleDesc.Count   = 1;  // Staging must be non-MSAA
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage              = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags          = 0;
    stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags          = 0;

    HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) {
        DMME_LOG_CRITICAL("CreateTexture2D (staging) failed: {}", HRToString(hr));
        return false;
    }

    return true;
}

// ===================================================================
// Internal: Release Render Target Resources
// ===================================================================

void DX11Driver::ReleaseRenderTarget() {
    if (m_context) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_rtv.Reset();
    m_renderTexture.Reset();
    m_dsv.Reset();
    m_depthTexture.Reset();
    m_stagingTexture.Reset();
}

// ===================================================================
// Internal: HRESULT to String
// ===================================================================

std::string DX11Driver::HRToString(HRESULT hr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buf);
}

} // namespace renderer
} // namespace core
} // namespace dmme