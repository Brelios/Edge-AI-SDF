#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Get the safe target frame size based on PSRAM availability
// (HD for boards with PSRAM, QVGA for boards with internal DRAM only)
framesize_t getTargetFrameSize();

// Initialize the OV2640 camera sensor and apply image DSP tuning
bool initCamera();

// Capture a raw JPEG frame with no encoding — lightweight, zero heap spike.
// Caller MUST call esp_camera_fb_return(fb) when done.
camera_fb_t* captureJPEG();

// Base64 encode an existing camera frame buffer
String encodeFBtoBase64(camera_fb_t* fb);

// Capture a JPEG frame, Base64 encode it in PSRAM/DRAM, and pass back the camera_fb_t pointer
// Used only for Gemini API calls which require base64 input.
String captureAndEncodeJPEG(camera_fb_t** fb_out);

// Flash LED control functions
void initFlash();
void triggerFlash(uint16_t durationMs = 120);
