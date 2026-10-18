#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Initialize motion detector (configures PIR GPIO13 if present + resets baseline metrics)
void initMotionDetector();

// Poll for motion:
// Checks PIR GPIO13 (instant hardware trigger if sensor attached)
// AND checks optical JPEG frame variance (works without PIR sensor)
// Returns true if motion was detected.
bool checkOpticalMotion();
