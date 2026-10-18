/*
 * Edge AI Smart Door Feed — Main Firmware Orchestrator
 * ESP32-CAM (AI-Thinker) + Google Gemini 3.8 Flash + Telegram Bot
 *
 * Wake cycle:
 *   PIR on GPIO13 → wake (<10ms) → capture JPEG → Gemini AI → Telegram → sleep
 */

#include "camera_driver.h"
#include "config.h"
#include "gemini_client.h"
#include "power_manager.h"
#include "telegram_client.h"
#include <Arduino.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// Wi-Fi Connection
// ---------------------------------------------------------------------------
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println("\nWiFi timeout!");
      return false;
    }
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected — IP: " + WiFi.localIP().toString());
  return true;
}

// ---------------------------------------------------------------------------
// Active Motion Cycle — loops every 4s while PIR remains HIGH
// ---------------------------------------------------------------------------
void processMotionCycle() {
  Serial.println("\n>>> [MOTION DETECTED] Starting PIR Capture Cycle <<<");

  if (!connectWiFi()) {
    Serial.println("WiFi connection failed — exiting motion cycle");
    return;
  }

  int frameCount = 1;
  bool motionActive = true;

  while (motionActive) {
    unsigned long cycleStart = millis();
    Serial.printf("\n--- Capturing Motion Alert #%d ---\n", frameCount++);

    // 1. Capture snapshot immediately
    camera_fb_t *fb = nullptr;
    String base64Image = captureAndEncodeJPEG(&fb);
    if (!base64Image.isEmpty() && fb != nullptr) {
      Serial.printf("Captured frame: %u bytes (%dx%d) — Free heap: %u bytes\n",
                    fb->len, fb->width, fb->height, ESP.getFreeHeap());

      // 2. Call Gemini API + parse response
      String geminiResponse = callGeminiAPI(base64Image);
      // Free base64 string buffer (~80KB) immediately to reclaim heap before Telegram upload
      base64Image = "";

      GeminiAnalysis analysis = parseGeminiResponse(geminiResponse);
      Serial.println("Gemini summary: " + analysis.summary + " [" + analysis.urgency + " - " + analysis.category + "]");

      // 3. Format structured caption & send Telegram photo
      String caption = formatTelegramCaption(analysis);
      sendTelegramPhoto(fb, caption);

      // 4. Release frame buffer
      esp_camera_fb_return(fb);
    } else {
      Serial.println("Capture/encode failed");
      if (fb)
        esp_camera_fb_return(fb);
    }

    // 5. Wait for the remainder of interval while monitoring PIR
    Serial.printf("Waiting %d ms interval for next check...\n",
                  PIR_CAPTURE_INTERVAL_MS);
    while (millis() - cycleStart < PIR_CAPTURE_INTERVAL_MS) {
      delay(100);
    }

    // 6. Check if PIR is still HIGH
    if (digitalRead(PIR_GPIO) == HIGH) {
      Serial.printf("Checking PIR sensor (GPIO%d): HIGH (Motion ongoing)\n",
                    PIR_GPIO);
      continue;
    }

    Serial.println("PIR went LOW — verifying motion stopped (debounce)...");

    // Debounce verification to prevent false sleep during a brief pause
    delay(SLEEP_DEBOUNCE_MS);
    if (digitalRead(PIR_GPIO) == HIGH) {
      Serial.println(
          "PIR became active again during debounce — continuing motion loop");
      continue;
    }

    motionActive = false;
  }

  Serial.println("Motion cycle finished.");
}

// ---------------------------------------------------------------------------
// setup() — runs on boot and every cold wake
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Edge AI Door Feed — Woke Up ===");

  // Configure PIR pin and RTC internal pulldown
  initPowerManagement();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.printf("Wakeup cause: %d (PIR GPIO13 = %d)\n", cause,
                digitalRead(PIR_GPIO));
  Serial.printf("Initial free heap: %u bytes\n", ESP.getFreeHeap());

  // Init camera first (allocates contiguous DMA memory before heap fragments)
  if (!initCamera()) {
    Serial.println("Camera init failed — sleeping");
    goToSleep();
    return;
  }
  Serial.printf("Camera OK — Free heap: %u bytes\n", ESP.getFreeHeap());
  delay(150); // Sensor AGC/AWB settle delay

  bool isExt0Wake = (cause == ESP_SLEEP_WAKEUP_EXT0);
  bool isColdBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
  bool isPIRHigh = (digitalRead(PIR_GPIO) == HIGH);

  if (isExt0Wake || isPIRHigh || isColdBoot) {
    processMotionCycle();
  } else {
    Serial.println("PIR is LOW on wake — entering sleep mode");
  }
}

// ---------------------------------------------------------------------------
// loop() — sleeps when idle, resumes on PIR motion
// ---------------------------------------------------------------------------
void loop() {
  goToSleep();

#if USE_LIGHT_SLEEP
  // In Light Sleep mode, execution resumes right here after waking up
  Serial.println("\n=== Woke from Light Sleep via PIR Motion! ===");
  processMotionCycle();
#endif
}
