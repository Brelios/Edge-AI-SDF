/*
 * Edge AI Smart Door Feed — Main Firmware Orchestrator
 * ESP32-CAM (AI-Thinker) + Google Gemini AI + Telegram Bot
 *
 * Operation mode: Always-connected sentinel
 *   Dual trigger: PIR (GPIO13) or Optical frame variance
 *   → Gemini Vision AI → Rich Telegram status card
 *   + On-demand Telegram commands: /snap, /status, /motion on|off, /help
 */

#include "camera_driver.h"
#include "config.h"
#include "gemini_client.h"
#include "motion_detector.h"
#include "power_manager.h"
#include "telegram_client.h"
#include "system_metrics.h"
#include <Arduino.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// Global State
// ---------------------------------------------------------------------------
static bool  motionEnabled  = true;   // can be toggled via /motion on|off
static unsigned long lastAlertMs    = 0;  // timestamp of last Gemini+Telegram dispatch
static unsigned long lastTelegramMs = 0;  // timestamp of last Telegram command poll
static unsigned long lastMotionMs   = 0;  // timestamp of last motion poll

// ---------------------------------------------------------------------------
// dispatchAlert() — Call Gemini, send Telegram card using the TRIGGER frame!
// Zero delay: uses the exact frame that detected the motion.
// ---------------------------------------------------------------------------
static void dispatchAlert(camera_fb_t* fb, const char* trigger) {
    if (!fb || fb->len == 0) return;

    Serial.printf("\n>>> [ALERT] Triggered by: %s <<<\n", trigger);

#if defined(ENABLE_FLASH_ON_CAPTURE) && ENABLE_FLASH_ON_CAPTURE
    // Flash capture: Illuminate the subject who triggered motion!
    esp_camera_fb_return(fb);  // release pre-flash motion detection frame
    fb = captureJPEG();        // capture crisp, brightly illuminated flash photo
    if (!fb || fb->len == 0) {
        Serial.println("[Alert] Flash capture returned null");
        return;
    }
#endif

    Serial.printf("    Alert frame: %u bytes (%dx%d)  Free heap: %u bytes\n",
                  fb->len, fb->width, fb->height, ESP.getFreeHeap());

    // Call Gemini Vision API directly streaming the frame (zero heap Base64 allocation)
    String geminiRaw = callGeminiAPI(fb);

    GeminiAnalysis analysis = parseGeminiResponse(geminiRaw);
    Serial.printf("[Alert] Gemini → category=%s  urgency=%s  people=%d  packages=%d (AI Latency: %ums)\n",
                  analysis.category.c_str(), analysis.urgency.c_str(),
                  analysis.people, analysis.packages, g_metrics.lastAiLatencyMs);

    // Suppress false alarms if configured
    if (SUPPRESS_FALSE_ALARMS && analysis.category == "false_alarm") {
        g_metrics.falseAlarmsSuppressed++;
        Serial.printf("[Alert] Suppressed — Gemini classified as false_alarm (AI: %ums).\n",
                      g_metrics.lastAiLatencyMs);
        esp_camera_fb_return(fb);
        return;
    }

    // Send rich Telegram status card with the exact triggered frame
    String caption = formatTelegramCaption(analysis);
    sendTelegramPhoto(fb, caption);
    g_metrics.alertsDispatchedCount++;
    esp_camera_fb_return(fb);

    lastAlertMs = millis();
    Serial.printf("[Alert] Dispatch complete! [Timing Breakdown] AI: %ums | Upload: %ums | Total: %ums\n",
                  g_metrics.lastAiLatencyMs, g_metrics.lastUploadLatencyMs,
                  g_metrics.lastAiLatencyMs + g_metrics.lastUploadLatencyMs);

    // Re-seed motion baseline with settled post-alert background
    resetMotionBaseline();
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
                  psramFound() ? "Detected" : "Not found (DRAM mode)");

    // Step 1: Connect to Wi-Fi (blocks until connected or timeout)
    if (!connectWiFi()) {
        Serial.println("[Setup] FATAL: Cannot connect to Wi-Fi. Halting.");
        while (true) { delay(5000); }
    }

    // Step 2: Initialize camera
    if (!initCamera()) {
        Serial.println("[Setup] FATAL: Camera init failed. Rebooting in 3s...");
        delay(3000);
        ESP.restart();
    }
    Serial.printf("[Setup] Camera OK — Free heap: %u bytes\n", ESP.getFreeHeap());

    // Step 3: Settle AGC/AWB registers before first frame
    delay(200);

    // Step 4: Initialize motion detector (PIR + optical)
    initMotionDetector();

    // Step 5: Announce readiness on Telegram
    String resStr = psramFound() ? "OV2640 HD (1280×720)" : "OV2640 VGA (640×480)";
    String bootMsg =
        "🟢 *Edge AI Door Feed — Online*\n"
        "─────────────────────────\n"
        "📷 Camera: " + resStr + "\n"
        "🤖 AI: " + String(GEMINI_MODEL) + "\n"
        "🛡 Motion detection: Active (Zero-Lag Trigger)\n"
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
        if (snapped) return;
    }

    // -----------------------------------------------------------------------
    // 3. High-speed motion detection (checked every 200ms)
    //    Zero delay: the frame where motion is detected is dispatched directly!
    // -----------------------------------------------------------------------
    if (motionEnabled && (now - lastMotionMs >= MOTION_POLL_INTERVAL_MS)) {
        lastMotionMs = now;

        camera_fb_t* motionFb = nullptr;
        bool motionDetected = checkOpticalMotion(&motionFb);

        if (motionDetected && motionFb != nullptr) {
            bool canAlert = (lastAlertMs == 0) || (now - lastAlertMs >= MOTION_COOLDOWN_MS);
            if (canAlert) {
                dispatchAlert(motionFb, "motion");
            } else {
                adaptMotionBaseline(motionFb->len);
                Serial.printf("[Motion] Cooling down (%lus remaining).\n",
                              (unsigned long)((MOTION_COOLDOWN_MS - (now - lastAlertMs)) / 1000));
                esp_camera_fb_return(motionFb);
            }
        }
    }
}
