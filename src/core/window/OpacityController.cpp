#include "OpacityController.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace dmme {
namespace core {
namespace window {

// ===================================================================
// Construction
// ===================================================================

OpacityController::OpacityController()
    : m_currentOpacity(1.0f)
    , m_targetOpacity(1.0f)
    , m_fadeSpeed(0.0f)
    , m_fading(false) {
    DMME_LOG_DEBUG("OpacityController created (opacity=1.0)");
}

// ===================================================================
// Immediate Set
// ===================================================================

void OpacityController::SetOpacity(float value) {
    std::lock_guard<std::mutex> lock(m_mutex);

    float clamped = Clamp01(value);
    m_currentOpacity = clamped;
    m_targetOpacity  = clamped;
    m_fadeSpeed       = 0.0f;
    m_fading          = false;

    DMME_LOG_DEBUG("Opacity set immediately to {:.3f}", clamped);
}

// ===================================================================
// Animated Transitions
// ===================================================================

void OpacityController::FadeTo(float target, float durationSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);

    float clampedTarget = Clamp01(target);

    if (durationSeconds <= 0.0f) {
        // Zero or negative duration: apply immediately
        m_currentOpacity = clampedTarget;
        m_targetOpacity  = clampedTarget;
        m_fadeSpeed       = 0.0f;
        m_fading          = false;
        DMME_LOG_DEBUG("FadeTo instant (duration<=0): opacity={:.3f}", clampedTarget);
        return;
    }

    float diff = clampedTarget - m_currentOpacity;
    if (std::fabs(diff) < 0.001f) {
        // Already at target, nothing to do
        m_currentOpacity = clampedTarget;
        m_targetOpacity  = clampedTarget;
        m_fadeSpeed       = 0.0f;
        m_fading          = false;
        return;
    }

    m_targetOpacity = clampedTarget;
    m_fadeSpeed     = diff / durationSeconds;  // can be negative for fade-out
    m_fading        = true;

    DMME_LOG_DEBUG("FadeTo started: {:.3f} -> {:.3f} over {:.2f}s (speed={:.4f}/s)",
                   m_currentOpacity, clampedTarget, durationSeconds, m_fadeSpeed);
}

void OpacityController::FadeIn(float durationSeconds) {
    FadeTo(1.0f, durationSeconds);
}

void OpacityController::FadeOut(float durationSeconds) {
    FadeTo(0.0f, durationSeconds);
}

// ===================================================================
// Frame Update
// ===================================================================

void OpacityController::Update(float deltaSeconds) {
    FadeCompleteCallback completeCb;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_fading) {
            return;
        }

        if (deltaSeconds <= 0.0f) {
            return;
        }

        m_currentOpacity += m_fadeSpeed * deltaSeconds;

        // Check if we reached or passed the target
        bool reached = false;
        if (m_fadeSpeed > 0.0f) {
            // Fading in (increasing)
            if (m_currentOpacity >= m_targetOpacity) {
                m_currentOpacity = m_targetOpacity;
                reached = true;
            }
        } else {
            // Fading out (decreasing)
            if (m_currentOpacity <= m_targetOpacity) {
                m_currentOpacity = m_targetOpacity;
                reached = true;
            }
        }

        // Safety clamp
        m_currentOpacity = Clamp01(m_currentOpacity);

        if (reached) {
            m_fading    = false;
            m_fadeSpeed = 0.0f;
            DMME_LOG_DEBUG("Fade completed: opacity={:.3f}", m_currentOpacity);

            if (m_fadeCompleteCallback) {
                completeCb = m_fadeCompleteCallback;
            }
        }
    }

    // Fire callback outside lock to prevent deadlocks
    if (completeCb) {
        completeCb(m_currentOpacity);
    }
}

// ===================================================================
// Queries
// ===================================================================

float OpacityController::GetCurrentOpacity() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentOpacity;
}

uint8_t OpacityController::GetCurrentAlpha() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    float clamped = Clamp01(m_currentOpacity);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

float OpacityController::GetTargetOpacity() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_targetOpacity;
}

bool OpacityController::IsFading() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_fading;
}

// ===================================================================
// Callback
// ===================================================================

void OpacityController::SetFadeCompleteCallback(FadeCompleteCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fadeCompleteCallback = std::move(cb);
}

// ===================================================================
// Internal
// ===================================================================

float OpacityController::Clamp01(float v) const {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

} // namespace window
} // namespace core
} // namespace dmme