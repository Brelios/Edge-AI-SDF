#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Initialize the OV2640 camera sensor and apply image DSP tuning
bool initCamera();

// Capture a JPEG frame, Base64 encode it in PSRAM/DRAM, and pass back the camera_fb_t pointer
String captureAndEncodeJPEG(camera_fb_t** fb_out);
