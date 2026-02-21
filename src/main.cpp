#include "utils/Logger.h"
#include "core/window/TransparentWindow.h"
#include "core/window/OpacityController.h"
#include "core/window/MultiMonitor.h"
#include "core/window/WindowTypes.h"

#include <Windows.h>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstring>

using namespace dmme::core::window;
using namespace dmme::utils;

// ===================================================================
// Generate test pixel data (a colored circle with alpha gradient)
// This simulates what the renderer would provide in production.
// ===================================================================

static void GenerateTestFrame(std::vector<uint8_t>& rgbaBuffer,
                               int width, int height,
                               float time) {
    const int centerX = width / 2;
    const int centerY = height / 2;
    const float maxRadius = static_cast<float>((std::min)(width, height)) / 2.0f - 10.0f;

    // Breathing animation: radius pulses slowly
    float breathFactor = 0.9f + 0.1f * std::sin(time * 1.5f);
    float effectiveRadius = maxRadius * breathFactor;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;

            float dx = static_cast<float>(x - centerX);
            float dy = static_cast<float>(y - centerY);
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < effectiveRadius) {
                // Inside the circle: colored with alpha based on distance from edge
                float edgeDist = effectiveRadius - dist;
                float alpha = 1.0f;
                if (edgeDist < 15.0f) {
                    alpha = edgeDist / 15.0f;  // soft edge
                }

                // Color: anime-skin-like warm tone with slight hue shift over time
                float hueShift = std::sin(time * 0.5f) * 0.1f;
                uint8_t r = static_cast<uint8_t>((std::min)(255.0f, 240.0f + hueShift * 100.0f));
                uint8_t g = static_cast<uint8_t>((std::min)(255.0f, 200.0f + hueShift * 50.0f));
                uint8_t b = static_cast<uint8_t>((std::min)(255.0f, 180.0f));
                uint8_t a = static_cast<uint8_t>(alpha * 255.0f);

                // Draw a simple face pattern
                float eyeL_dx = dx + 30.0f;
                float eyeL_dy = dy + 20.0f;
                float eyeR_dx = dx - 30.0f;
                float eyeR_dy = dy + 20.0f;
                float eyeL_dist = std::sqrt(eyeL_dx * eyeL_dx + eyeL_dy * eyeL_dy);
                float eyeR_dist = std::sqrt(eyeR_dx * eyeR_dx + eyeR_dy * eyeR_dy);

                // Eyes: dark circles
                if (eyeL_dist < 12.0f || eyeR_dist < 12.0f) {
                    r = 40; g = 40; b = 60;
                    a = 255;
                }

                // Mouth: simple arc (approximate with position check)
                if (dy > -10.0f && dy < -2.0f && std::fabs(dx) < 20.0f) {
                    r = 200; g = 80; b = 100;
                    a = 255;
                }

                rgbaBuffer[idx + 0] = r;
                rgbaBuffer[idx + 1] = g;
                rgbaBuffer[idx + 2] = b;
                rgbaBuffer[idx + 3] = a;
            } else {
                // Outside the circle: fully transparent
                rgbaBuffer[idx + 0] = 0;
                rgbaBuffer[idx + 1] = 0;
                rgbaBuffer[idx + 2] = 0;
                rgbaBuffer[idx + 3] = 0;
            }
        }
    }
}

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

    DMME_LOG_INFO("=== DMME Engine Starting ===");

    // ---------------------------------------------------------------
    // Step 2: Enumerate Monitors
    // ---------------------------------------------------------------

    MultiMonitor monitors;
    int monCount = monitors.GetCount();
    DMME_LOG_INFO("Detected {} monitor(s)", monCount);

    auto primary = monitors.GetPrimaryMonitor();
    if (!primary.has_value()) {
        DMME_LOG_CRITICAL("No primary monitor found. Cannot proceed.");
        Logger::Shutdown();
        return 1;
    }

    DMME_LOG_INFO("Primary monitor: ({},{})--({},{}) DPI scale={:.2f}",
                  primary->fullArea.left, primary->fullArea.top,
                  primary->fullArea.right, primary->fullArea.bottom,
                  primary->scaleFactor);

    // ---------------------------------------------------------------
    // Step 3: Calculate Window Position (center of primary monitor)
    // ---------------------------------------------------------------

    const int winWidth  = 300;
    const int winHeight = 300;
    int posX = primary->workArea.left +
               (primary->workArea.Width() - winWidth) / 2;
    int posY = primary->workArea.top +
               (primary->workArea.Height() - winHeight) / 2;

    // ---------------------------------------------------------------
    // Step 4: Configure and Create Transparent Window
    // ---------------------------------------------------------------

    WindowConfig config;
    config.posX           = posX;
    config.posY           = posY;
    config.width          = winWidth;
    config.height         = winHeight;
    config.alwaysOnTop    = true;
    config.visible        = true;
    config.toolWindow     = true;
    config.title          = L"DMME Mascot";
    config.alphaThreshold = 10;
    config.initialOpacity = 255;

    TransparentWindow window;

    // Set up mouse callback for testing
    window.SetMouseEventCallback([](const MouseEvent& evt) {
        if (evt.isMove) {
            // Don't log every move -- too noisy
            return;
        }
        const char* btnName = "None";
        if (evt.button == MouseButton::Left) btnName = "Left";
        else if (evt.button == MouseButton::Right) btnName = "Right";
        else if (evt.button == MouseButton::Middle) btnName = "Middle";

        DMME_LOG_INFO("Mouse {} {} at client({},{}) screen({},{})",
                      btnName,
                      evt.isDown ? "DOWN" : "UP",
                      evt.clientX, evt.clientY,
                      evt.screenX, evt.screenY);
    });

    window.SetCloseCallback([]() {
        DMME_LOG_INFO("Close requested by user");
        PostQuitMessage(0);
    });

    if (!window.Initialize(config)) {
        DMME_LOG_CRITICAL("Failed to initialize TransparentWindow");
        Logger::Shutdown();
        return 1;
    }

    // ---------------------------------------------------------------
    // Step 5: Set Up Opacity Controller
    // ---------------------------------------------------------------

    OpacityController opacityCtrl;
    opacityCtrl.SetFadeCompleteCallback([](float finalOpacity) {
        DMME_LOG_INFO("Fade complete: opacity={:.3f}", finalOpacity);
    });

    // Start with a fade-in effect
    opacityCtrl.SetOpacity(0.0f);
    opacityCtrl.FadeIn(2.0f);  // 2 second fade-in on startup

    // ---------------------------------------------------------------
    // Step 6: Allocate RGBA Pixel Buffer
    // ---------------------------------------------------------------

    std::vector<uint8_t> pixelBuffer(static_cast<size_t>(winWidth) * winHeight * 4, 0);

    // ---------------------------------------------------------------
    // Step 7: Main Loop
    // ---------------------------------------------------------------

    DMME_LOG_INFO("Entering main loop");

    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastFrame = startTime;
    bool running   = true;

    // Fade-out test: after 10 seconds, start fade-out, then quit after complete
    bool fadeOutScheduled = false;
    float totalRunTime    = 0.0f;

    while (running) {
        // -- Timing --
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrame).count();
        float elapsed   = std::chrono::duration<float>(now - startTime).count();
        lastFrame = now;
        totalRunTime += deltaTime;

        // -- Process Windows Messages --
        if (!window.ProcessMessages()) {
            running = false;
            break;
        }

        // -- Update Opacity Controller --
        opacityCtrl.Update(deltaTime);
        uint8_t currentAlpha = opacityCtrl.GetCurrentAlpha();
        window.SetGlobalAlpha(currentAlpha);

        // -- Generate Test Frame (simulates renderer output) --
        GenerateTestFrame(pixelBuffer, winWidth, winHeight, elapsed);

        // -- Push Frame to Window --
        window.UpdateFrame(pixelBuffer.data(), winWidth, winHeight);

        // -- Demo: Schedule fade-out after 15 seconds --
        if (!fadeOutScheduled && totalRunTime > 15.0f) {
            DMME_LOG_INFO("Demo: scheduling fade-out after 15 seconds");
            opacityCtrl.FadeOut(3.0f);
            opacityCtrl.SetFadeCompleteCallback([&running](float /*final*/) {
                DMME_LOG_INFO("Fade-out complete, requesting exit");
                PostQuitMessage(0);
            });
            fadeOutScheduled = true;
        }

        // -- Frame Rate Limit (~60fps) --
        // Simple sleep-based limiter. Production renderer would use vsync.
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
    // Step 8: Shutdown
    // ---------------------------------------------------------------

    DMME_LOG_INFO("Main loop exited, shutting down");
    window.Shutdown();

    DMME_LOG_INFO("=== DMME Engine Shutdown Complete ===");
    Logger::Shutdown();

    return 0;
}