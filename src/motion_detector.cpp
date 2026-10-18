#include "motion_detector.h"
#include "camera_driver.h"
#include "config.h"
#include "system_metrics.h"

static float baselineLen = 0;
static bool baselineReady = false;

void initMotionDetector() {
    #if defined(PIR_GPIO)
    pinMode(PIR_GPIO, INPUT_PULLDOWN);
    #endif

    baselineLen = 0;
    baselineReady = false;
    Serial.printf("[Motion] Detector initialized (PIR=GPIO%d, optical threshold=%.1f%%)\n",
                  PIR_GPIO, MOTION_SENSITIVITY * 100.0f);
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

    // 2. Optical check: capture 1 frame in stable JPEG mode
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->len == 0) {
        if (fb) esp_camera_fb_return(fb);
        g_metrics.lastMotionCheckMs = millis() - checkStart;
        return false;
    }

    size_t curLen = fb->len;

    // Seed baseline on first boot frame
    if (!baselineReady || baselineLen <= 0) {
        baselineLen = (float)curLen;
        baselineReady = true;
        Serial.printf("[Motion] Baseline seeded (static frame size: %u bytes)\n", curLen);
        esp_camera_fb_return(fb);
        g_metrics.lastMotionCheckMs = millis() - checkStart;
        return false;
    }

    // Metric: Relative change in JPEG entropy compared to ambient baseline
    float delta = (float)abs((int)curLen - (int)baselineLen) / baselineLen;

    bool triggered = (delta >= MOTION_SENSITIVITY);

    if (triggered) {
        g_metrics.motionTriggersCount++;
        g_metrics.lastTriggerSource = "Optical";
        g_metrics.lastTriggerDelta = delta * 100.0f;
        g_metrics.lastMotionCheckMs = millis() - checkStart;

        Serial.printf("[Motion] TRIGGERED! Delta: %.2f%% >= threshold %.1f%% (cur=%u, base=%.0f, eval=%ums)\n",
                      delta * 100.0f, MOTION_SENSITIVITY * 100.0f, curLen, baselineLen, g_metrics.lastMotionCheckMs);
        // KEEP THIS EXACT FRAME — it has the moving person in it!
        if (fb_out) {
            *fb_out = fb;
        } else {
            esp_camera_fb_return(fb);
        }
        return true;
    }

    // No motion detected: gently update baseline with slow EMA (adapts to sunlight/clouds)
    baselineLen = (0.95f * baselineLen) + (0.05f * (float)curLen);

    // Release frame immediately
    esp_camera_fb_return(fb);
    g_metrics.lastMotionCheckMs = millis() - checkStart;
    return false;
}
