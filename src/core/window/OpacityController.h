#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <atomic>

namespace dmme {
namespace core {
namespace window {

// OpacityController manages global window opacity with smooth fade
// transitions. It does NOT directly call Win32 APIs. Instead, it
// computes the current alpha value each frame, and the owner
// (TransparentWindow) reads that value and applies it.
//
// This separation means OpacityController is testable without a
// real window, and can be reused in any context that needs a
// smooth 0-255 alpha ramp.
//
// Usage:
//   controller.SetTargetOpacity(0.0f);   // start fade-out
//   // each frame:
//   controller.Update(deltaTimeSeconds);
//   uint8_t alpha = controller.GetCurrentAlpha();
//   window.SetGlobalAlpha(alpha);

class OpacityController {
public:
    OpacityController();
    ~OpacityController() = default;

    // Non-copyable
    OpacityController(const OpacityController&) = delete;
    OpacityController& operator=(const OpacityController&) = delete;

    // --- Immediate Set ---

    // Set opacity immediately without any transition.
    // value: 0.0 (invisible) to 1.0 (fully opaque)
    void SetOpacity(float value);

    // --- Animated Transitions ---

    // Begin a smooth transition to the target opacity.
    // target: 0.0 to 1.0
    // durationSeconds: how long the transition takes (> 0)
    void FadeTo(float target, float durationSeconds);

    // Convenience: fade to fully visible
    void FadeIn(float durationSeconds);

    // Convenience: fade to fully invisible
    void FadeOut(float durationSeconds);

    // --- Frame Update ---

    // Call once per frame. Advances any active fade transition.
    // deltaSeconds: time since last frame (e.g., 0.016 for 60fps)
    void Update(float deltaSeconds);

    // --- Queries ---

    // Current opacity as float [0.0, 1.0]
    float GetCurrentOpacity() const;

    // Current opacity as uint8_t [0, 255] suitable for SetGlobalAlpha
    uint8_t GetCurrentAlpha() const;

    // Target opacity (what we are fading toward)
    float GetTargetOpacity() const;

    // Is a fade transition currently in progress?
    bool IsFading() const;

    // --- Callback ---

    // Called when a fade transition completes.
    using FadeCompleteCallback = std::function<void(float finalOpacity)>;
    void SetFadeCompleteCallback(FadeCompleteCallback cb);

private:
    float Clamp01(float v) const;

    float m_currentOpacity   = 1.0f;
    float m_targetOpacity    = 1.0f;
    float m_fadeSpeed        = 0.0f;   // units per second
    bool  m_fading           = false;

    FadeCompleteCallback m_fadeCompleteCallback;
    mutable std::mutex   m_mutex;
};

} // namespace window
} // namespace core
} // namespace dmme