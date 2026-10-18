#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Initialize motion detector (configures PIR GPIO13 if present + resets baseline metrics)
void initMotionDetector();

// Poll for motion:
// Checks PIR GPIO13 and optical JPEG frame variance.
// If motion is detected, *fb_out is assigned the EXACT frame that triggered the motion!
// The caller MUST call esp_camera_fb_return(*fb_out) when done.
// If motion is not detected, returns false and *fb_out is nullptr.
bool checkOpticalMotion(camera_fb_t** fb_out);

// Re-seed baseline after alert dispatch to prevent endless loop from post-motion scene changes
void resetMotionBaseline();

// Adaptively learn new baseline during cooldown
void adaptMotionBaseline(size_t curLen);
