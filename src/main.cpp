/*
 * Edge AI Smart Door Feed — Main Firmware Orchestrator
 * ESP32-CAM (AI-Thinker) + Google Gemini 3.8 Flash + Telegram Bot
 *
 * Operation mode: Always-connected sentinel
 *   Camera frame differencing → Optical motion trigger
 *   → Gemini 3.8 Flash Vision AI → Rich Telegram status card
 *   + On-demand Telegram commands: /snap, /status, /motion on|off, /help
 */

#include "camera_driver.h"
#include "config.h"
#include "gemini_client.h"
#include "motion_detector.h"
#include "power_manager.h"
#include "telegram_client.h"
#include <Arduino.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// Global State
// ---------------------------------------------------------------------------
static bool  motionEnabled  = true;   // can be toggled via /motion on|off
static unsigned long lastAlertMs    = 0;  // timestamp of last Gemini+Telegram dispatch
static unsigned long lastTelegramMs = 0;  // timestamp of last Telegram command poll
static unsigned long lastMotionMs   = 0;  // timestamp of last optical motion poll

// ---------------------------------------------------------------------------
// dispatchAlert() — Capture HD frame, call Gemini, send Telegram card
// ---------------------------------------------------------------------------
static void dispatchAlert(const char* trigger) {
    Serial.printf("\n>>> [ALERT] Triggered by: %s <<<\n", trigger);
    Serial.printf("    Free heap: %u bytes\n", ESP.getFreeHeap());

    camera_fb_t* fb      = nullptr;
    String       base64  = captureAndEncodeJPEG(&fb);

    if (base64.isEmpty() || fb == nullptr) {
        Serial.println("[Alert] Capture failed — skipping this cycle.");
        if (fb) esp_camera_fb_return(fb);
        return;
    }

    Serial.printf("[Alert] Frame captured: %u bytes (%dx%d)\n",
                  fb->len, fb->width, fb->height);

    // Call Gemini Vision API
    String geminiRaw = callGeminiAPI(base64);
    base64 = "";  // Immediately free ~80KB heap before Telegram upload

    GeminiAnalysis analysis = parseGeminiResponse(geminiRaw);
    Serial.printf("[Alert] Gemini → category=%s  urgency=%s  people=%d  packages=%d\n",
                  analysis.category.c_str(), analysis.urgency.c_str(),
                  analysis.people, analysis.packages);

    // Suppress false alarms if configured
    if (SUPPRESS_FALSE_ALARMS && analysis.category == "false_alarm") {
        Serial.println("[Alert] Suppressed — Gemini classified as false_alarm.");
        esp_camera_fb_return(fb);
        return;
    }

    // Send rich Telegram status card with the captured JPEG
    String caption = formatTelegramCaption(analysis);
    sendTelegramPhoto(fb, caption);
    esp_camera_fb_return(fb);

    lastAlertMs = millis();
    Serial.println("[Alert] Dispatch complete.");
}

// ---------------------------------------------------------------------------
// setup() — Runs once on power-on or hardware reset
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Edge AI Door Feed — Starting Up ===");
    Serial.printf("Chip: ESP32 rev %d  Flash: %u MB  PSRAM: %s\n",
                  ESP.getChipRevision(),
                  ESP.getFlashChipSize() / (1024 * 1024),
                  psramFound() ? "Detected" : "Not found");

    // Step 1: Connect to Wi-Fi (blocks until connected or timeout)
    if (!connectWiFi()) {
        Serial.println("[Setup] FATAL: Cannot connect to Wi-Fi. Halting.");
        while (true) { delay(5000); }
    }

    // Step 2: Initialize camera (must happen before motion detector allocation)
    if (!initCamera()) {
        Serial.println("[Setup] FATAL: Camera init failed. Halting.");
        while (true) { delay(5000); }
    }
    Serial.printf("[Setup] Camera OK — Free heap: %u bytes\n", ESP.getFreeHeap());

    // Step 3: Settle AGC/AWB registers before first frame
    delay(200);

    // Step 4: Initialize optical motion detector (allocates reference frame buffer)
    initMotionDetector();

    // Step 5: Announce readiness on Telegram
    String bootMsg =
        "🟢 *Edge AI Door Feed — Online*\n"
        "─────────────────────────\n"
        "📷 Camera: OV2640 HD (1280×720)\n"
        "🤖 AI: gemini-3.8-flash\n"
        "🛡 Optical motion detection: Active\n"
        "📶 Wi-Fi: " + WiFi.localIP().toString() + " (" + String(WiFi.RSSI()) + " dBm)\n"
        "💾 Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n\n"
        "Send /help to see available commands.";
    sendTelegramMessage(bootMsg);

    Serial.println("[Setup] Boot complete — entering main loop.");
}

// ---------------------------------------------------------------------------
// loop() — Always-connected event loop
// ---------------------------------------------------------------------------
void loop() {
    unsigned long now = millis();

    // -----------------------------------------------------------------------
    // 1. Maintain Wi-Fi (silently reconnect if dropped)
    // -----------------------------------------------------------------------
    maintainWiFi();

    // -----------------------------------------------------------------------
    // 2. Poll Telegram for incoming commands at TELEGRAM_POLL_INTERVAL_MS
    //    /snap, /status, /motion on|off, /help
    // -----------------------------------------------------------------------
    if (now - lastTelegramMs >= TELEGRAM_POLL_INTERVAL_MS) {
        lastTelegramMs = now;
        bool snapped = processTelegramCommands(motionEnabled);
        // If /snap just fired, yield without running the motion check this cycle
        // to avoid competing for camera hardware simultaneously.
        if (snapped) return;
    }

    // -----------------------------------------------------------------------
    // 3. Optical motion detection (when alerts are enabled and cooldown passed)
    // -----------------------------------------------------------------------
    if (motionEnabled && (now - lastMotionMs >= MOTION_POLL_INTERVAL_MS)) {
        lastMotionMs = now;

        bool motionDetected = checkOpticalMotion();

        if (motionDetected) {
            // Enforce cooldown to avoid spamming during sustained presence
            if (now - lastAlertMs >= MOTION_COOLDOWN_MS) {
                dispatchAlert("optical_motion");
            } else {
                Serial.printf("[Motion] Motion detected but cooling down (%lus remaining).\n",
                              (unsigned long)((MOTION_COOLDOWN_MS - (now - lastAlertMs)) / 1000));
            }
        }
    }
}
