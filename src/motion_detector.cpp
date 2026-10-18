#include "motion_detector.h"
#include "camera_driver.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Internal state — previous frame pixel buffer
// ---------------------------------------------------------------------------

// QQVGA = 160x120 = 19,200 pixels — tiny, fast to compare in RAM
static const int SURVEILLANCE_W = 160;
static const int SURVEILLANCE_H = 120;
static const int SURVEILLANCE_PIXELS = SURVEILLANCE_W * SURVEILLANCE_H;

// Allocated in PSRAM when available to avoid eating DRAM
static uint8_t* prevFrame = nullptr;
static bool prevFrameReady = false;

// ---------------------------------------------------------------------------
// initMotionDetector()
// ---------------------------------------------------------------------------
void initMotionDetector() {
    // Allocate previous-frame buffer once at startup
    if (prevFrame == nullptr) {
        prevFrame = psramFound()
            ? (uint8_t*)ps_malloc(SURVEILLANCE_PIXELS)
            : (uint8_t*)malloc(SURVEILLANCE_PIXELS);
        if (!prevFrame) {
            Serial.println("[Motion] ERROR: Failed to allocate prev frame buffer!");
        } else {
            memset(prevFrame, 0, SURVEILLANCE_PIXELS);
            Serial.printf("[Motion] Optical detector ready (%dx%d, sensitivity=%.2f%%)\n",
                          SURVEILLANCE_W, SURVEILLANCE_H, MOTION_SENSITIVITY * 100.0f);
        }
    }
    prevFrameReady = false;
}

// ---------------------------------------------------------------------------
// checkOpticalMotion()
// ---------------------------------------------------------------------------
bool checkOpticalMotion() {
    if (!prevFrame) return false;

    // Step 1: Capture lightweight QQVGA grayscale surveillance frame
    //         Temporarily reconfigure the camera to tiny QQVGA + GRAYSCALE.
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;

    s->set_framesize(s, FRAMESIZE_QQVGA);   // 160x120 — tiny and fast
    s->set_pixformat(s, PIXFORMAT_GRAYSCALE);

    // Discard one frame to flush the DMA pipeline after resolution switch
    camera_fb_t* flush = esp_camera_fb_get();
    if (flush) esp_camera_fb_return(flush);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        // Restore HD JPEG mode and bail
        s->set_framesize(s, FRAMESIZE_HD);
        s->set_pixformat(s, PIXFORMAT_JPEG);
        return false;
    }

    // Step 2: Compute mean absolute difference (MAD) between current and previous frame
    const uint8_t* cur = fb->buf;
    uint32_t accumulator = 0;

    for (int i = 0; i < SURVEILLANCE_PIXELS; i++) {
        int diff = (int)cur[i] - (int)prevFrame[i];
        accumulator += (uint32_t)(diff < 0 ? -diff : diff);
    }

    // Step 3: Save current frame as next reference, then release
    memcpy(prevFrame, cur, SURVEILLANCE_PIXELS);
    esp_camera_fb_return(fb);

    // Restore full HD JPEG mode for potential Gemini capture
    s->set_framesize(s, FRAMESIZE_HD);
    s->set_pixformat(s, PIXFORMAT_JPEG);

    // Step 4: Evaluate threshold
    float changedFraction = (float)accumulator / (float)(SURVEILLANCE_PIXELS * 255);

    if (!prevFrameReady) {
        // First call — seed the reference frame, never false-trigger on startup
        prevFrameReady = true;
        Serial.printf("[Motion] Reference frame seeded (diff=%.3f%% — startup ignored)\n",
                      changedFraction * 100.0f);
        return false;
    }

    bool triggered = changedFraction >= MOTION_SENSITIVITY;

    if (triggered) {
        Serial.printf("[Motion] TRIGGERED! Pixel delta: %.2f%% >= threshold %.2f%%\n",
                      changedFraction * 100.0f, MOTION_SENSITIVITY * 100.0f);
    }

    return triggered;
}
