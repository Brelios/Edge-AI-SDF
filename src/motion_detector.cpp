#include "motion_detector.h"
#include "camera_driver.h"
#include "config.h"

static size_t lastJpegLen = 0;
static const int SAMPLE_COUNT = 32;
static uint8_t lastSamples[SAMPLE_COUNT];
static bool baselineReady = false;

void initMotionDetector() {
    #if defined(PIR_GPIO)
    pinMode(PIR_GPIO, INPUT_PULLDOWN);
    #endif

    lastJpegLen = 0;
    memset(lastSamples, 0, sizeof(lastSamples));
    baselineReady = false;
    Serial.printf("[Motion] Detector initialized (PIR=GPIO%d, optical sensitivity=%.2f%%)\n",
                  PIR_GPIO, MOTION_SENSITIVITY * 100.0f);
}

bool checkOpticalMotion() {
    // 1. Check physical PIR sensor on GPIO13 first (if attached and HIGH)
    #if defined(PIR_GPIO)
    if (digitalRead(PIR_GPIO) == HIGH) {
        Serial.println("[Motion] TRIGGERED by hardware PIR sensor (GPIO13 HIGH)!");
        return true;
    }
    #endif

    // 2. Optical check: capture 1 frame using standard JPEG mode (NO resolution switch, NO grayscale switch!)
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
        if (fb) esp_camera_fb_return(fb);
        return false;
    }

    size_t curLen = fb->len;

    // Sample SAMPLE_COUNT bytes from the entropy-coded scan data (after JPEG headers)
    uint8_t curSamples[SAMPLE_COUNT];
    size_t startOffset = (curLen > 600) ? 600 : 0;
    size_t scanLength  = (curLen > startOffset + 50) ? (curLen - startOffset - 50) : 0;
    size_t step        = scanLength / SAMPLE_COUNT;

    if (step > 0) {
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            curSamples[i] = fb->buf[startOffset + (i * step)];
        }
    } else {
        memset(curSamples, 0, sizeof(curSamples));
    }

    // Always release the framebuffer immediately
    esp_camera_fb_return(fb);

    if (!baselineReady || lastJpegLen == 0) {
        lastJpegLen = curLen;
        memcpy(lastSamples, curSamples, SAMPLE_COUNT);
        baselineReady = true;
        Serial.printf("[Motion] Optical baseline seeded (frame size=%u bytes)\n", curLen);
        return false;
    }

    // Calculate metrics:
    // Metric A: File size delta (sudden changes in scene complexity alter JPEG entropy)
    int sizeDiff = abs((int)curLen - (int)lastJpegLen);
    float sizeDelta = (float)sizeDiff / (float)lastJpegLen;

    // Metric B: Sampled byte divergence across payload
    int sampleDiff = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sampleDiff += abs((int)curSamples[i] - (int)lastSamples[i]);
    }
    float sampleDelta = (float)sampleDiff / (float)(SAMPLE_COUNT * 255);

    // Weighted combined motion score
    float motionScore = (sizeDelta * 0.6f) + (sampleDelta * 0.4f);

    // Smooth update of baseline
    lastJpegLen = curLen;
    memcpy(lastSamples, curSamples, SAMPLE_COUNT);

    bool triggered = (motionScore >= MOTION_SENSITIVITY);
    if (triggered) {
        Serial.printf("[Motion] TRIGGERED! Score: %.2f%% >= threshold %.2f%% (sizeDiff=%d bytes, sampleDelta=%.2f%%)\n",
                      motionScore * 100.0f, MOTION_SENSITIVITY * 100.0f, sizeDiff, sampleDelta * 100.0f);
    }

    return triggered;
}
