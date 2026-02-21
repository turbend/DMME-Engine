#pragma once

#include "DriverInterface.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>

namespace dmme {
namespace core {
namespace renderer {

using Microsoft::WRL::ComPtr;

class DX11Driver final : public IGraphicsDriver {
public:
    DX11Driver();
    ~DX11Driver() override;

    DX11Driver(const DX11Driver&) = delete;
    DX11Driver& operator=(const DX11Driver&) = delete;

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

    // --- DX11 Specific Accessors (for future shader/mesh systems) ---
    ID3D11Device*        GetDevice() const;
    ID3D11DeviceContext* GetContext() const;

private:
    bool CreateDevice(HWND hwnd, bool enableDebug);
    bool EnumerateAdapter();
    void QueryCapabilities();
    bool CreateRenderTarget(int w, int h, TextureFormat fmt, int samples);
    bool CreateDepthStencil(int w, int h, int samples);
    bool CreateStagingTexture(int w, int h);
    void ReleaseRenderTarget();
    static std::string HRToString(HRESULT hr);

    // --- Device ---
    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGIFactory2>          m_dxgiFactory;
    ComPtr<IDXGIAdapter1>          m_adapter;
    D3D_FEATURE_LEVEL              m_featureLevel = D3D_FEATURE_LEVEL_11_0;

    // --- Render Target (off-screen) ---
    ComPtr<ID3D11Texture2D>          m_renderTexture;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11Texture2D>          m_depthTexture;
    ComPtr<ID3D11DepthStencilView>   m_dsv;

    // --- Staging (for CPU readback) ---
    ComPtr<ID3D11Texture2D>          m_stagingTexture;

    // --- State ---
    bool           m_initialized = false;
    int            m_targetWidth  = 0;
    int            m_targetHeight = 0;
    TextureFormat  m_targetFormat = TextureFormat::RGBA8_UNORM;
    int            m_sampleCount  = 1;
    HWND           m_hwnd         = nullptr;
    bool           m_debugEnabled = false;

    // --- Info ---
    GPUAdapterInfo m_adapterInfo;
    DriverCaps     m_caps;
    FrameStats     m_frameStats;
    uint64_t       m_frameCounter = 0;

    // --- Timing ---
    ComPtr<ID3D11Query>  m_disjointQuery;
    ComPtr<ID3D11Query>  m_timestampBegin;
    ComPtr<ID3D11Query>  m_timestampEnd;
};

// Factory function
std::unique_ptr<IGraphicsDriver> CreateDX11Driver();

} // namespace renderer
} // namespace core
} // namespace dmme