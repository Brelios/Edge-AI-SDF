#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// ---------------------------------------------------------------------------
// Optical Motion Detection via Camera Frame Differencing
//
// Works by capturing lightweight QQVGA (160x120) grayscale frames and
// computing the mean absolute pixel difference between successive frames.
// When the changed fraction exceeds MOTION_SENSITIVITY, motion is flagged.
// On trigger, the camera is upgraded to HD (1280x720) for Gemini analysis,
// then stepped back to surveillance mode.
// ---------------------------------------------------------------------------

// Initialize motion detector internal state.
// Must be called AFTER initCamera() succeeds in setup().
void initMotionDetector();

// Capture one surveillance frame and compare against the previous.
// Returns true if significant motion was detected.
// Call this in the main loop at MOTION_POLL_INTERVAL_MS cadence.
bool checkOpticalMotion();
