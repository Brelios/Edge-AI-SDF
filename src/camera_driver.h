#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Initialize the OV2640 camera sensor and apply image DSP tuning
bool initCamera();

// Capture a raw JPEG frame with no encoding — lightweight, no heap spike.
// Ensures camera is in HD JPEG mode before capture.
// Caller MUST call esp_camera_fb_return(fb) when done.
camera_fb_t* captureJPEG();

// Capture a JPEG frame, Base64 encode it in PSRAM/DRAM, and pass back the camera_fb_t pointer
// Used only for Gemini API calls which require base64 input.
String captureAndEncodeJPEG(camera_fb_t** fb_out);
