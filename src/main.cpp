#include "utils/Logger.h"
#include "core/window/TransparentWindow.h"
#include "core/window/OpacityController.h"
#include "core/window/MultiMonitor.h"
#include "core/window/WindowTypes.h"
#include "core/renderer/RenderPipeline.h"
#include "core/renderer/RenderTypes.h"
#include "core/renderer/drivers/DX11Driver.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace dmme::core::window;
using namespace dmme::core::renderer;
using namespace dmme::utils;
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

// ===================================================================
// Test Content Renderer
//
// Manages DX11 shaders for drawing a procedural mascot face.
// Adapts shader model target based on GPU feature level.
// Handles initialization failure gracefully -- tries once, if fails
// falls back to clear-color-only mode permanently.
// ===================================================================

class TestContentRenderer {
public:
    enum class State {
        Uninitialized,
        Ready,
        Failed     // permanent failure -- do not retry
    };

    State GetState() const { return m_state; }

    bool Initialize(IGraphicsDriver* driver) {
        if (m_state != State::Uninitialized) {
            return m_state == State::Ready;
        }

        if (!driver || driver->GetAPI() != GraphicsAPI::DX11) {
            DMME_LOG_WARN("TestContentRenderer: not DX11, using clear-color fallback");
            m_state = State::Failed;
            return false;
        }

        auto* dx11 = static_cast<DX11Driver*>(driver);
        auto* device = dx11->GetDevice();
        auto* context = dx11->GetContext();

        if (!device || !context) {
            DMME_LOG_ERROR("TestContentRenderer: null device or context");
            m_state = State::Failed;
            return false;
        }

        // Determine shader model based on actual feature level
        D3D_FEATURE_LEVEL fl = device->GetFeatureLevel();
        const char* vsTarget = nullptr;
        const char* psTarget = nullptr;

        switch (fl) {
            case D3D_FEATURE_LEVEL_11_1:
            case D3D_FEATURE_LEVEL_11_0:
                vsTarget = "vs_5_0";
                psTarget = "ps_5_0";
                break;
            case D3D_FEATURE_LEVEL_10_1:
                vsTarget = "vs_4_1";
                psTarget = "ps_4_1";
                break;
            case D3D_FEATURE_LEVEL_10_0:
                vsTarget = "vs_4_0";
                psTarget = "ps_4_0";
                break;
            case D3D_FEATURE_LEVEL_9_3:
                vsTarget = "vs_4_0_level_9_3";
                psTarget = "ps_4_0_level_9_3";
                break;
            default:
                vsTarget = "vs_4_0_level_9_1";
                psTarget = "ps_4_0_level_9_1";
                break;
        }

        DMME_LOG_INFO("TestContentRenderer: feature level {:#x}, using VS={} PS={}",
                      static_cast<unsigned>(fl), vsTarget, psTarget);

        // -----------------------------------------------------------
        // Vertex Shader: full-screen triangle from vertex ID
        //
        // SM 4.0+ supports SV_VertexID.
        // Generates 3 vertices that form a triangle covering the
        // entire screen. No vertex buffer needed.
        // -----------------------------------------------------------
        const char* vsCode = R"(
            float4 main(uint id : SV_VertexID) : SV_Position {
                float2 uv = float2((id << 1) & 2, id & 2);
                return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
            }
        )";

        // -----------------------------------------------------------
        // Pixel Shader: procedural circle with face pattern
        //
        // Written to be SM 4.0 compatible:
        //   - No advanced intrinsics
        //   - Simple math only
        //   - cbuffer register(b0)
        // -----------------------------------------------------------

        const char* psCode = R"(
            cbuffer FrameData : register(b0) {
                float4 params;
            };

            float4 main(float4 pos : SV_Position) : SV_Target {
                float elapsed = params.x;
                float texW    = params.y;
                float texH    = params.z;

                float2 uv = pos.xy / float2(texW, texH);
                float2 center = float2(0.5, 0.5);
                float2 d = uv - center;

                float aspect = texW / texH;
                d.x = d.x * aspect;

                float dist = sqrt(d.x * d.x + d.y * d.y);
                float breathe = 0.9 + 0.1 * sin(elapsed * 1.5);
                float radius = 0.35 * breathe;

                if (dist > radius) {
                    return float4(0.0, 0.0, 0.0, 0.0);
                }

                float edge = radius - dist;
                float soft = 1.0;
                if (edge < 0.03) {
                    soft = edge / 0.03;
                }

                float3 skinColor = float3(0.94, 0.78, 0.71);
                float3 color = skinColor;
                float alpha = soft;

                float2 eyeL = d - float2(-0.08, -0.04);
                float2 eyeR = d - float2(0.08, -0.04);
                float eyeLD = sqrt(eyeL.x * eyeL.x + eyeL.y * eyeL.y);
                float eyeRD = sqrt(eyeR.x * eyeR.x + eyeR.y * eyeR.y);

                if (eyeLD < 0.03 || eyeRD < 0.03) {
                    color = float3(0.15, 0.15, 0.25);
                    alpha = 1.0;
                }

                float2 pupilL = d - float2(-0.08, -0.045);
                float2 pupilR = d - float2(0.08, -0.045);
                float pupilLD = sqrt(pupilL.x * pupilL.x + pupilL.y * pupilL.y);
                float pupilRD = sqrt(pupilR.x * pupilR.x + pupilR.y * pupilR.y);

                if (pupilLD < 0.012 || pupilRD < 0.012) {
                    color = float3(0.9, 0.9, 1.0);
                }

                if (d.y > 0.04 && d.y < 0.07) {
                    float mx = d.x;
                    if (mx < 0.0) mx = -mx;
                    if (mx < 0.06) {
                        float t = 1.0 - (mx / 0.06);
                        color = color * (1.0 - t * 0.8) + float3(0.85, 0.35, 0.4) * (t * 0.8);
                    }
                }

                float2 blushL = d - float2(-0.12, 0.02);
                float2 blushR = d - float2(0.12, 0.02);
                float blushLD = sqrt(blushL.x * blushL.x + blushL.y * blushL.y);
                float blushRD = sqrt(blushR.x * blushR.x + blushR.y * blushR.y);

                if (blushLD < 0.035 || blushRD < 0.035) {
                    color = color * 0.7 + float3(1.0, 0.6, 0.6) * 0.3;
                }

                return float4(color * alpha, alpha);
            }
        )";

        // -----------------------------------------------------------
        // Compile Vertex Shader
        // -----------------------------------------------------------

        ComPtr<ID3DBlob> vsBlob, errorBlob;
        HRESULT hr = D3DCompile(
            vsCode, strlen(vsCode), "TestVS",
            nullptr, nullptr, "main", vsTarget,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &vsBlob, &errorBlob
        );

        if (FAILED(hr)) {
            std::string errMsg = "unknown";
            if (errorBlob) {
                errMsg = std::string(
                    static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize()
                );
            }
            DMME_LOG_ERROR("VS compile failed (target={}): {}", vsTarget, errMsg);
            m_state = State::Failed;
            return false;
        }

        // -----------------------------------------------------------
        // Compile Pixel Shader
        // -----------------------------------------------------------

        ComPtr<ID3DBlob> psBlob;
        errorBlob.Reset();

        hr = D3DCompile(
            psCode, strlen(psCode), "TestPS",
            nullptr, nullptr, "main", psTarget,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &psBlob, &errorBlob
        );

        if (FAILED(hr)) {
            std::string errMsg = "unknown";
            if (errorBlob) {
                errMsg = std::string(
                    static_cast<const char*>(errorBlob->GetBufferPointer()),
                    errorBlob->GetBufferSize()
                );
            }
            DMME_LOG_ERROR("PS compile failed (target={}): {}", psTarget, errMsg);
            m_state = State::Failed;
            return false;
        }

        // -----------------------------------------------------------
        // Create Shader Objects
        // -----------------------------------------------------------

        hr = device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, &m_vs
        );
        if (FAILED(hr)) {
            DMME_LOG_ERROR("CreateVertexShader failed: HRESULT {:#x}", static_cast<unsigned>(hr));
            m_state = State::Failed;
            return false;
        }

        hr = device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
            nullptr, &m_ps
        );
        if (FAILED(hr)) {
            DMME_LOG_ERROR("CreatePixelShader failed: HRESULT {:#x}", static_cast<unsigned>(hr));
            m_vs.Reset();
            m_state = State::Failed;
            return false;
        }

        // -----------------------------------------------------------
        // Create Constant Buffer (16 bytes = 4 floats, minimum aligned)
        // -----------------------------------------------------------

        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth      = 16;
        cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

        hr = device->CreateBuffer(&cbDesc, nullptr, &m_cbuffer);
        if (FAILED(hr)) {
            DMME_LOG_ERROR("CreateBuffer (cbuffer) failed: HRESULT {:#x}", static_cast<unsigned>(hr));
            m_vs.Reset();
            m_ps.Reset();
            m_state = State::Failed;
            return false;
        }

        // -----------------------------------------------------------
        // Create Blend State (enable alpha blending for premul output)
        // -----------------------------------------------------------

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable           = TRUE;
        blendDesc.RenderTarget[0].SrcBlend               = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend              = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp                = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha          = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha         = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha           = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask  = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = device->CreateBlendState(&blendDesc, &m_blendState);
        if (FAILED(hr)) {
            DMME_LOG_WARN("CreateBlendState failed, continuing without blend state");
            // Non-fatal -- will still work, just no blending
        }

        m_state = State::Ready;
        DMME_LOG_INFO("TestContentRenderer initialized successfully (SM={})", vsTarget);
        return true;
    }

    void Draw(IGraphicsDriver* driver, int width, int height, float elapsed) {
        if (m_state != State::Ready) {
            // Fallback: time-varying clear color
            DrawFallback(driver, elapsed);
            return;
        }

        auto* dx11 = static_cast<DX11Driver*>(driver);
        auto* context = dx11->GetContext();
        if (!context) return;

        // Update constant buffer
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context->Map(m_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            struct alignas(16) FrameData {
                float time;
                float width;
                float height;
                float padding;
            };
            auto* data = static_cast<FrameData*>(mapped.pData);
            data->time    = elapsed;
            data->width   = static_cast<float>(width);
            data->height  = static_cast<float>(height);
            data->padding = 0.0f;
            context->Unmap(m_cbuffer.Get(), 0);
        }

        // Set pipeline state
        context->VSSetShader(m_vs.Get(), nullptr, 0);
        context->PSSetShader(m_ps.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, m_cbuffer.GetAddressOf());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(nullptr);

        if (m_blendState) {
            float blendFactor[4] = {0, 0, 0, 0};
            context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
        }

        // Draw full-screen triangle (3 vertices, no vertex buffer)
        context->Draw(3, 0);

        // Reset blend state
        if (m_blendState) {
            context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        }
    }

    void Shutdown() {
        m_vs.Reset();
        m_ps.Reset();
        m_cbuffer.Reset();
        m_blendState.Reset();
        m_state = State::Uninitialized;
    }

private:
    void DrawFallback(IGraphicsDriver* driver, float elapsed) {
        if (!driver) return;
        float pulse = 0.3f + 0.2f * std::sin(elapsed * 2.0f);
        ClearColor testColor = {pulse * 0.4f, pulse * 0.6f, pulse * 0.8f, pulse};
        driver->Clear(testColor);
    }

    State m_state = State::Uninitialized;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
    ComPtr<ID3D11Buffer>       m_cbuffer;
    ComPtr<ID3D11BlendState>   m_blendState;
};

// ===================================================================
// Main Entry Point
// ===================================================================

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {

    // ---------------------------------------------------------------
    // Step 1: Initialize Logger
    // ---------------------------------------------------------------
    if (!Logger::Initialize("DMME", "logs")) {
        MessageBoxW(nullptr, L"Failed to initialize logger", L"DMME Error", MB_OK);
        return 1;
    }

    DMME_LOG_INFO("=== DMME Engine Starting (Day 2: GPU Rendering) ===");

    // ---------------------------------------------------------------
    // Step 2: Enumerate Monitors
    // ---------------------------------------------------------------
    MultiMonitor monitors;
    auto primary = monitors.GetPrimaryMonitor();
    if (!primary.has_value()) {
        DMME_LOG_CRITICAL("No primary monitor found");
        Logger::Shutdown();
        return 1;
    }

    // ---------------------------------------------------------------
    // Step 3: Create Transparent Window
    // ---------------------------------------------------------------
    const int winWidth  = 400;
    const int winHeight = 400;
    int posX = primary->workArea.left +
               (primary->workArea.Width() - winWidth) / 2;
    int posY = primary->workArea.top +
               (primary->workArea.Height() - winHeight) / 2;

    WindowConfig winCfg;
    winCfg.posX           = posX;
    winCfg.posY           = posY;
    winCfg.width          = winWidth;
    winCfg.height         = winHeight;
    winCfg.alwaysOnTop    = true;
    winCfg.visible        = true;
    winCfg.toolWindow     = true;
    winCfg.title          = L"DMME Mascot";
    winCfg.alphaThreshold = 10;
    winCfg.initialOpacity = 255;

    TransparentWindow window;

    window.SetMouseEventCallback([](const MouseEvent& evt) {
        if (evt.isMove) return;
        const char* btn = "None";
        if (evt.button == MouseButton::Left) btn = "Left";
        else if (evt.button == MouseButton::Right) btn = "Right";
        else if (evt.button == MouseButton::Middle) btn = "Middle";
        DMME_LOG_INFO("Mouse {} {} at ({},{})", btn,
                      evt.isDown ? "DOWN" : "UP", evt.clientX, evt.clientY);
    });

    window.SetCloseCallback([]() {
        DMME_LOG_INFO("Close requested");
        PostQuitMessage(0);
    });

    if (!window.Initialize(winCfg)) {
        DMME_LOG_CRITICAL("Failed to initialize window");
        Logger::Shutdown();
        return 1;
    }

    // ---------------------------------------------------------------
    // Step 4: Initialize Render Pipeline
    // ---------------------------------------------------------------
    RenderConfig renderCfg;
    renderCfg.preferredAPI    = GraphicsAPI::DX11;
    renderCfg.enableDebugLayer = false;
    renderCfg.targetWidth     = winWidth;
    renderCfg.targetHeight    = winHeight;
    renderCfg.clearColor      = {0.0f, 0.0f, 0.0f, 0.0f};

    #if defined(_DEBUG)
    renderCfg.enableDebugLayer = true;
    #endif

    RenderPipeline pipeline;

    if (!pipeline.Initialize(window.GetHWND(), renderCfg)) {
        DMME_LOG_CRITICAL("Failed to initialize render pipeline");
        window.Shutdown();
        Logger::Shutdown();
        return 1;
    }

    // Convert wstring to UTF-8 string for logging
    const auto& wadapterDesc = pipeline.GetAdapterInfo().description;
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wadapterDesc.c_str(), -1, NULL, 0, NULL, NULL);
    std::string adapterDesc(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wadapterDesc.c_str(), -1, &adapterDesc[0], size_needed, NULL, NULL);
    DMME_LOG_INFO("Render pipeline active: {} on {}",
                  GraphicsAPIName(pipeline.GetActiveAPI()),
                  adapterDesc);

    auto caps = pipeline.GetCapabilities();
    DMME_LOG_INFO("GPU Caps: maxTex={} maxRT={} maxMSAA={} compute={} SM={}",
                  caps.maxTextureSize, caps.maxRenderTargets,
                  caps.maxMSAASamples, caps.supportsCompute,
                  caps.shaderModel);

    // ---------------------------------------------------------------
    // Step 5: Initialize Test Content Renderer
    // ---------------------------------------------------------------
    TestContentRenderer testRenderer;
    testRenderer.Initialize(pipeline.GetDriver());

    if (testRenderer.GetState() == TestContentRenderer::State::Ready) {
        DMME_LOG_INFO("GPU shader rendering active");
    } else {
        DMME_LOG_WARN("GPU shader rendering failed, using clear-color fallback");
    }

    // ---------------------------------------------------------------
    // Step 6: Setup Opacity Controller
    // ---------------------------------------------------------------
    OpacityController opacityCtrl;
    opacityCtrl.SetOpacity(0.0f);
    opacityCtrl.FadeIn(1.5f);

    // ---------------------------------------------------------------
    // Step 6: Main Loop
    // ---------------------------------------------------------------
    DMME_LOG_INFO("Entering main render loop");

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrame = startTime;
    auto lastStatsLog = startTime;
    bool running = true;
    uint64_t frameCount = 0;

    while (running) {
        // -- Timing --
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrame).count();
        float elapsed   = std::chrono::duration<float>(now - startTime).count();
        lastFrame = now;

        // -- Windows Messages --
        if (!window.ProcessMessages()) {
            running = false;
            break;
        }

        // -- Update Opacity --
        opacityCtrl.Update(deltaTime);
        window.SetGlobalAlpha(opacityCtrl.GetCurrentAlpha());

        // -- Render Frame --
        if (pipeline.BeginFrame()) {
            testRenderer.Draw(
                pipeline.GetDriver(),
                pipeline.GetSurface()->GetWidth(),
                pipeline.GetSurface()->GetHeight(),
                elapsed
            );

            pipeline.EndFrame();

            // Readback and push to window
            const PixelReadback* pixels = pipeline.ReadbackFrame();
            if (pixels && pixels->IsValid()) {
                window.UpdateFrame(pixels->data.data(),
                                   pixels->width, pixels->height);
            }
        }

        // -- Periodic Stats Logging (every 5 seconds) --
        float timeSinceStats = std::chrono::duration<float>(now - lastStatsLog).count();
        if (timeSinceStats >= 5.0f) {
            auto stats = pipeline.GetFrameStats();
            float avgFps = (frameCount > 0)
                ? (static_cast<float>(frameCount) / elapsed) : 0.0f;
            DMME_LOG_INFO("Frame #{}: cpu={:.2f}ms gpu={:.2f}ms avgFPS={:.1f}",
                          stats.frameNumber, stats.frameTimeMs,
                          stats.gpuTimeMs, avgFps);
            lastStatsLog = now;
        }

        frameCount++;

        // -- Frame Rate Limit (~60fps) --
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float frameMs = std::chrono::duration<float, std::milli>(frameEnd - now).count();
        if (frameMs < 16.0f) {
            DWORD sleepMs = static_cast<DWORD>(16.0f - frameMs);
            if (sleepMs > 0 && sleepMs < 100) {
                Sleep(sleepMs);
            }
        }
    }

    // ---------------------------------------------------------------
    // Step 7: Shutdown
    // ---------------------------------------------------------------
    DMME_LOG_INFO("Main loop exited, shutting down");
    testRenderer.Shutdown();    pipeline.Shutdown();
    window.Shutdown();

    DMME_LOG_INFO("=== DMME Engine Shutdown Complete ===");
    Logger::Shutdown();

    return 0;
}