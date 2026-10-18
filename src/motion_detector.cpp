#include "motion_detector.h"
#include "camera_driver.h"
#include "config.h"
#include "system_metrics.h"

static float baselineLen = 0;
static bool baselineReady = false;
static size_t prevFrameLen = 0;
static int staticDriftCount = 0;

void initMotionDetector() {
    #if defined(PIR_GPIO)
    pinMode(PIR_GPIO, INPUT_PULLDOWN);
    #endif

    Serial.println("[Motion] Settling camera AEC/AGC registers...");
    // Flush initial frames so sensor auto-exposure and gain reach steady-state
    for (int i = 0; i < 8; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(60);
    }

    // Seed baseline with average of 3 settled frames
    float sum = 0;
    int count = 0;
    for (int i = 0; i < 3; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb && fb->len > 0) {
            sum += fb->len;
            count++;
            esp_camera_fb_return(fb);
        }
        delay(60);
    }

    if (count > 0) {
        baselineLen = sum / count;
        prevFrameLen = (size_t)baselineLen;
        baselineReady = true;
        Serial.printf("[Motion] Baseline seeded on settled room: %.0f bytes\n", baselineLen);
    } else {
        baselineLen = 0;
        baselineReady = false;
    }

    Serial.printf("[Motion] Optical threshold: %.1f%% (PIR GPIO=%d)\n",
                  MOTION_SENSITIVITY * 100.0f, PIR_GPIO);
}

void resetMotionBaseline() {
    // Settle 2 frames and re-seed baseline after an alert cycle
    float sum = 0;
    int count = 0;
    for (int i = 0; i < 2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb && fb->len > 0) {
            sum += fb->len;
            count++;
            esp_camera_fb_return(fb);
        }
        delay(50);
    }
    if (count > 0) {
        baselineLen = sum / count;
        prevFrameLen = (size_t)baselineLen;
        staticDriftCount = 0;
        Serial.printf("[Motion] Baseline re-seeded post-alert: %.0f bytes\n", baselineLen);
    }
}

void adaptMotionBaseline(size_t curLen) {
    if (curLen > 0 && baselineLen > 0) {
        baselineLen = (0.85f * baselineLen) + (0.15f * (float)curLen);
        prevFrameLen = curLen;
    }
}

bool checkOpticalMotion(camera_fb_t** fb_out) {
    if (fb_out) *fb_out = nullptr;
    unsigned long checkStart = millis();

    // 1. Check physical PIR sensor on GPIO13 first (instant hardware trigger if attached)
    #if defined(PIR_GPIO)
    if (digitalRead(PIR_GPIO) == HIGH) {
        Serial.println("[Motion] TRIGGERED by hardware PIR sensor (GPIO13 HIGH)!");
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            g_metrics.motionTriggersCount++;
            g_metrics.lastTriggerSource = "PIR GPIO13";
            g_metrics.lastTriggerDelta = 100.0f;
            g_metrics.lastMotionCheckMs = millis() - checkStart;
            if (fb_out) *fb_out = fb;
            return true;
        }
    }
    #endif

    // 2. Optical check: capture 1 frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
        if (fb) esp_camera_fb_return(fb);
        g_metrics.lastMotionCheckMs = millis() - checkStart;
        return false;
    }

    size_t curLen = fb->len;

    // Seed baseline if not ready
    if (!baselineReady || baselineLen <= 0) {
        baselineLen = (float)curLen;
        prevFrameLen = curLen;
        baselineReady = true;
        Serial.printf("[Motion] Baseline seeded: %u bytes\n", curLen);
        esp_camera_fb_return(fb);
        g_metrics.lastMotionCheckMs = millis() - checkStart;
        return false;
    }

    // Delta relative to ambient baseline
    float delta = (float)abs((int)curLen - (int)baselineLen) / baselineLen;

    // Relative frame-to-frame change (detects active motion vs static room light change)
    float consecutiveDelta = (prevFrameLen > 0) ? ((float)abs((int)curLen - (int)prevFrameLen) / (float)prevFrameLen) : 0.0f;
    prevFrameLen = curLen;

    bool exceedsThreshold = (delta >= MOTION_SENSITIVITY);

    if (exceedsThreshold) {
        // If scene is static (no consecutive change) for > 10 checks (~2s),
        // it means room ambient lighting permanently changed (e.g. lamp turned on).
        // Adapt baseline to the new ambient lighting instead of triggering false alarms!
        if (consecutiveDelta < 0.025f) {
            staticDriftCount++;
            if (staticDriftCount > 8) {
                baselineLen = (0.80f * baselineLen) + (0.20f * (float)curLen);
                esp_camera_fb_return(fb);
                g_metrics.lastMotionCheckMs = millis() - checkStart;
                return false;
            }
        } else {
            staticDriftCount = 0;
        }

        // Active dynamic motion!
        g_metrics.motionTriggersCount++;
        g_metrics.lastTriggerSource = "Optical";
        g_metrics.lastTriggerDelta = delta * 100.0f;
        g_metrics.lastMotionCheckMs = millis() - checkStart;

        Serial.printf("[Motion] TRIGGERED! Delta: %.1f%% (cur=%u, base=%.0f, step=%.1f%%, eval=%ums)\n",
                      delta * 100.0f, curLen, baselineLen, consecutiveDelta * 100.0f, g_metrics.lastMotionCheckMs);

        // Forward this exact triggering frame
        if (fb_out) {
            *fb_out = fb;
        } else {
            esp_camera_fb_return(fb);
        }
        return true;
    }

    // No motion detected: reset drift count and update baseline with smooth EMA
    staticDriftCount = 0;
    baselineLen = (0.95f * baselineLen) + (0.05f * (float)curLen);

    esp_camera_fb_return(fb);
    g_metrics.lastMotionCheckMs = millis() - checkStart;
    return false;
}
