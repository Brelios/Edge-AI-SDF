/*
 * Edge AI Smart Door Feed — Main Firmware
 * ESP32-CAM (AI-Thinker) + Google Gemini 2.0 Flash + Telegram Bot
 *
 * Wake cycle:
 *   PIR on GPIO13 → wake → capture JPEG → Gemini AI → Telegram → deep sleep
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "driver/rtc_io.h"

#include "board_config.h"   // sets CAMERA_MODEL_AI_THINKER → camera_pins.h

#if __has_include("secrets.h")
#include "secrets.h"        // WIFI_SSID, WIFI_PASSWORD, GEMINI_API_KEY, TELEGRAM_TOKEN, TELEGRAM_CHAT_ID
#elif __has_include("include/secrets.h")
#include "include/secrets.h"
#else
#error "secrets.h missing! Please copy 'include/secrets_example.h' to 'include/secrets.h' and fill in your credentials."
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define PIR_GPIO                  GPIO_NUM_13          // PIR OUT connected to IO13
#define PIR_CAPTURE_INTERVAL_MS   4000                 // 3-5s interval between captures while motion is active
#define SLEEP_DEBOUNCE_MS         1500                 // Wait after PIR goes LOW to confirm motion stopped
#define WIFI_TIMEOUT_MS           10000                // 10 s WiFi connect timeout

// =============================================================================
// Wakeup Minimization Mode:
// - Set USE_LIGHT_SLEEP = true for wall-adapter / USB power (RECOMMENDED in Build Plan):
//     * Wakes in < 10ms!
//     * Camera registers and RAM are kept intact (no reboot, no re-init delay)
//     * Current draw ~1.2mA (negligible on wall power)
// - Set USE_LIGHT_SLEEP = false for ultra-low-power battery setups:
//     * Deep sleep draws ~20µA, cold-reboots on PIR HIGH (~600ms wake-to-shutter)
// =============================================================================
#define USE_LIGHT_SLEEP           true

#define GEMINI_HOST      "generativelanguage.googleapis.com"
#define GEMINI_MODEL     "gemini-3.8-flash"
#define TELEGRAM_HOST    "api.telegram.org"

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
bool     initCamera();
bool     connectWiFi();
String   captureAndEncodeJPEG(camera_fb_t** fb_out);
String   callGeminiAPI(const String& base64Image);
void     parseGeminiResponse(const String& jsonResponse, String& summary, String& urgency);
bool     sendTelegramPhoto(camera_fb_t* fb, const String& caption);
bool     sendTelegramMessage(const String& text);
void     processMotionCycle();
void     goToSleep();

// ---------------------------------------------------------------------------
// setup() — runs on boot and every wake from deep sleep
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Edge AI Door Feed — Woke Up ===");

    // Configure PIR pin with internal pulldown (prevents floating line when disconnected)
    pinMode(PIR_GPIO, INPUT_PULLDOWN);
    rtc_gpio_pullup_dis(PIR_GPIO);
    rtc_gpio_pulldown_en(PIR_GPIO);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("Wakeup cause: %d (PIR GPIO13 = %d)\n", cause, digitalRead(PIR_GPIO));
    Serial.printf("Initial free heap: %u bytes\n", ESP.getFreeHeap());

    // Init camera FIRST (allocates contiguous DMA memory before heap fragments)
    if (!initCamera()) {
        Serial.println("Camera init failed — sleeping");
        goToSleep();
        return;
    }
    Serial.printf("Camera OK — Free heap: %u bytes\n", ESP.getFreeHeap());
    delay(150); // Fast sensor AGC/AWB settle (minimized for rapid wakeup)

    // Check motion status:
    // Wakes on ext0 PIR trigger, PIR HIGH, or if booted manually via Reset button for immediate test
    bool isExt0Wake = (cause == ESP_SLEEP_WAKEUP_EXT0);
    bool isColdBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
    bool isPIRHigh  = (digitalRead(PIR_GPIO) == HIGH);

    if (isExt0Wake || isPIRHigh || isColdBoot) {
        processMotionCycle();
    } else {
        Serial.println("PIR is LOW on wake — entering sleep mode");
    }
}

// ---------------------------------------------------------------------------
// Active Motion Cycle — loops every 3-5 seconds while PIR remains HIGH
// ---------------------------------------------------------------------------
void processMotionCycle() {
    Serial.println("\n>>> [MOTION DETECTED] Starting PIR Capture Cycle <<<");

    // Connect to WiFi once and hold the connection while motion is active
    if (!connectWiFi()) {
        Serial.println("WiFi connection failed — exiting motion cycle");
        return;
    }

    int frameCount = 1;
    bool motionActive = true;

    while (motionActive) {
        unsigned long cycleStart = millis();
        Serial.printf("\n--- Capturing Motion Alert #%d ---\n", frameCount++);

        // 1. Capture snapshot immediately (instant shutter)
        camera_fb_t* fb = nullptr;
        String base64Image = captureAndEncodeJPEG(&fb);
        if (!base64Image.isEmpty() && fb != nullptr) {
            Serial.printf("Captured frame: %u bytes (%dx%d) — Free heap: %u bytes\n",
                          fb->len, fb->width, fb->height, ESP.getFreeHeap());

            // 2. Call Gemini API + parse response
            String geminiResponse = callGeminiAPI(base64Image);
            String summary, urgency;
            parseGeminiResponse(geminiResponse, summary, urgency);
            Serial.println("Gemini summary: " + summary + " [" + urgency + "]");

            // 3. Send Telegram photo with unified caption
            String emoji = "🟢";
            if      (urgency == "low")    emoji = "🟡";
            else if (urgency == "medium") emoji = "🟠";
            else if (urgency == "high")   emoji = "🔴";

            String caption = emoji + " Door Alert [" + urgency + "]\n\n" + summary;
            sendTelegramPhoto(fb, caption);

            // 4. Release frame buffer
            esp_camera_fb_return(fb);
        } else {
            Serial.println("Capture/encode failed");
            if (fb) esp_camera_fb_return(fb);
        }

        // 5. Wait for the remainder of PIR_CAPTURE_INTERVAL_MS while checking PIR
        Serial.printf("Waiting %d ms interval for next check...\n", PIR_CAPTURE_INTERVAL_MS);
        while (millis() - cycleStart < PIR_CAPTURE_INTERVAL_MS) {
            delay(100);
        }

        // 6. Check if PIR is still HIGH
        if (digitalRead(PIR_GPIO) == HIGH) {
            Serial.printf("Checking PIR sensor (GPIO%d): HIGH (Motion ongoing)\n", PIR_GPIO);
            continue;
        }

        Serial.println("PIR went LOW — verifying motion stopped (debounce)...");

        // Debounce verification to prevent false sleep during a brief pause
        delay(SLEEP_DEBOUNCE_MS);
        if (digitalRead(PIR_GPIO) == HIGH) {
            Serial.println("PIR became active again during debounce — continuing motion loop");
            continue;
        }

        motionActive = false;
    }

    Serial.println("Motion cycle finished.");
}

void loop() {
    goToSleep();

#if USE_LIGHT_SLEEP
    // In Light Sleep mode, execution resumes right here after waking up!
    Serial.println("\n=== Woke from Light Sleep via PIR Motion! ===");
    processMotionCycle();
#endif
}

// ===========================================================================
// Camera Initialisation
// ===========================================================================
bool initCamera() {
    // Hardware reset the sensor via PWDN if available to clear hung I2C/SCCB state
    #if defined(PWDN_GPIO_NUM) && (PWDN_GPIO_NUM != -1)
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // Power off sensor
    delay(50);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // Power on sensor
    delay(50);
    #endif

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        Serial.println("PSRAM found — configuring HD (1280x720)");
        config.frame_size   = FRAMESIZE_HD;
        config.jpeg_quality = 10;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;
        config.xclk_freq_hz = 20000000;
        config.jpeg_buffer_size = 0;
    } else {
        Serial.println("No PSRAM — using DRAM with 10MHz clock, HQVGA, and 32KB buffer");
        config.frame_size   = FRAMESIZE_HQVGA;      // 240x176
        config.jpeg_quality = 20;                  // Quality 20: reliable size (~8-12KB)
        config.fb_count     = 1;                   // Single buffer saves precious DRAM
        config.fb_location  = CAMERA_FB_IN_DRAM;
        config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
        config.xclk_freq_hz = 10000000;             // 10 MHz prevents bus overrun
        config.jpeg_buffer_size = 32768;           // 32 KB buffer (prevents FB-OVF; default was only 8KB!)
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("esp_camera_init failed: 0x%x\n", err);
        return false;
    }

    // Enhance image clarity & prevent smearing on OV2640
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);     // -2 to 2 (slight lift for indoor/shadows)
        s->set_contrast(s, 1);       // -2 to 2 (clean edge contrast)
        s->set_saturation(s, 0);     // -2 to 2
        s->set_sharpness(s, 1);      // -2 to 2 (hardware edge sharpening)
        s->set_whitebal(s, 1);       // enable auto white balance
        s->set_awb_gain(s, 1);       // enable AWB gain
        s->set_wb_mode(s, 0);        // 0 = Auto AWB
        s->set_exposure_ctrl(s, 1);  // enable auto exposure
        s->set_aec2(s, 1);           // enable advanced AEC DSP
        s->set_ae_level(s, 0);       // target exposure level
        s->set_gain_ctrl(s, 1);      // enable auto gain
        s->set_gainceiling(s, (gainceiling_t)4); // 4X ceiling prevents slow shutter smearing in low light
        s->set_lenc(s, 1);           // Lens correction: eliminates corner vignette/edge blur
        s->set_bpc(s, 1);            // Black pixel correction
        s->set_wpc(s, 1);            // White pixel correction
        s->set_dcw(s, 1);            // Downsize/crop window (enhances clarity)
        s->set_raw_gma(s, 1);        // Raw gamma curve correction
    }
    return true;
}

// ===========================================================================
// WiFi Connection
// ===========================================================================
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

// ===========================================================================
// Capture JPEG + Base64 Encode
// ===========================================================================
String captureAndEncodeJPEG(camera_fb_t** fb_out) {
    Serial.println("Stabilizing sensor & capturing fresh frame...");

    // Discard initial frames to let AEC/AWB converge and clear stale DMA buffer (prevents smearing)
    for (int i = 0; i < 4; i++) {
        camera_fb_t* dummy = esp_camera_fb_get();
        if (dummy) esp_camera_fb_return(dummy);
        delay(40);
    }

    camera_fb_t* fb = nullptr;
    for (int attempt = 1; attempt <= 3; attempt++) {
        fb = esp_camera_fb_get();
        if (fb) break;
        Serial.printf("Attempt %d returned null, retrying in 200ms...\n", attempt);
        delay(200);
    }

    if (!fb) {
        Serial.println("Camera capture failed completely");
        return "";
    }
    *fb_out = fb;
    Serial.printf("Captured frame: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);

    // Calculate Base64 output size and allocate buffer in PSRAM if available
    size_t encodedLen = ((fb->len + 2) / 3) * 4 + 1;
    uint8_t* encodedBuf = psramFound() ? (uint8_t*)ps_malloc(encodedLen) : (uint8_t*)malloc(encodedLen);
    if (!encodedBuf) {
        Serial.println("Base64 malloc failed");
        return "";
    }

    size_t outLen = 0;
    int ret = mbedtls_base64_encode(encodedBuf, encodedLen, &outLen,
                                    fb->buf, fb->len);
    if (ret != 0) {
        Serial.printf("Base64 encode failed: %d\n", ret);
        free(encodedBuf);
        return "";
    }
    encodedBuf[outLen] = '\0';

    String encoded = String((char*)encodedBuf);
    free(encodedBuf);
    return encoded;
}

// ===========================================================================
// Gemini API Call
// ===========================================================================
String callGeminiAPI(const String& base64Image) {
    WiFiClientSecure client;
    client.setInsecure();   // Skip cert validation (no embedded CA)
    client.setTimeout(30);  // seconds — TLS handshake + large upload needs headroom

    HTTPClient http;
    String url = "https://" + String(GEMINI_HOST) +
                 "/v1beta/models/" + GEMINI_MODEL +
                 ":generateContent";  // key is in header, not URL

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", GEMINI_API_KEY);  // keeps key out of logs
    http.setTimeout(30000);

    // System prompt — plain ASCII, no embedded double-quotes, safe in raw JSON string
    static const char* systemPrompt =
        "You are a front-door security camera assistant. You receive one still image "
        "from an HD door camera (1280x720). "
        "Describe only what is clearly visible and never guess about anything you cannot make out. "
        "Do not identify anyone by name; use neutral descriptions such as clothing or what they carry. "
        "If nothing notable is happening, use the summary No activity at the door, urgency none, and an empty actions list. "
        "If the image is too dark, blurry, or blocked, set image_quality to poor and say so in the summary. "
        "Urgency guide: none = empty scene; low = a delivery or passerby; "
        "medium = someone lingering, trying the door, or covering their face; "
        "high = forced entry, weapons, or an apparent emergency. "
        "Give at most 3 actions, each under 10 words.";

    // responseSchema forces Gemini to return exactly this JSON shape — no free-form text
    static const char* schema = R"json({
      "type": "OBJECT",
      "properties": {
        "summary":       { "type": "STRING" },
        "people":        { "type": "INTEGER" },
        "packages":      { "type": "INTEGER" },
        "vehicles":      { "type": "INTEGER" },
        "urgency":       { "type": "STRING", "enum": ["none", "low", "medium", "high"] },
        "image_quality": { "type": "STRING", "enum": ["good", "poor"] },
        "actions":       { "type": "ARRAY", "items": { "type": "STRING" } }
      },
      "required": ["summary", "people", "packages", "vehicles", "urgency", "image_quality", "actions"]
    })json";

    // Build body manually — base64 image (50–200 KB) is too large for ArduinoJson.
    // reserve() up-front prevents repeated heap reallocations during +=.
    String body;
    body.reserve(base64Image.length() + 2500);

    body  = "{\"system_instruction\":{\"parts\":[{\"text\":\"";
    body += systemPrompt;
    body += "\"}]},";
    body += "\"contents\":[{\"parts\":[";
    body += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
    body += base64Image;
    body += "\"}},";
    body += "{\"text\":\"Analyze this frame.\"}";
    body += "]}],";
    body += "\"generationConfig\":{\"temperature\":0.2,";
    body += "\"responseMimeType\":\"application/json\",";
    body += "\"responseSchema\":";
    body += schema;
    body += "}}";

    Serial.printf("Sending to Gemini (%u bytes)...\n", body.length());
    int httpCode = http.POST(body);

    if (httpCode != 200) {
        Serial.printf("Gemini HTTP error: %d\n", httpCode);
        Serial.println(http.getString().substring(0, 300));
        http.end();

        // If temporary 503 high demand spike, retry once after 2 seconds
        if (httpCode == 503) {
            Serial.println("Gemini 503 (high demand) — retrying once in 2s...");
            delay(2000);
            http.begin(client, url);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("x-goog-api-key", GEMINI_API_KEY);
            http.setTimeout(30000);
            httpCode = http.POST(body);
            if (httpCode == 200) {
                String response = http.getString();
                http.end();
                Serial.println("Gemini retry responded OK");
                return response;
            }
            Serial.printf("Gemini retry also failed: %d\n", httpCode);
            http.end();
        }
        return "";
    }

    String response = http.getString();
    http.end();
    Serial.println("Gemini responded OK");
    return response;
}

// ===========================================================================
// Parse Gemini Response → fills summary and urgency by reference
// Uses a JsonDocument filter so only the inner text field is parsed,
// keeping peak RAM use low on the large Gemini envelope.
// ===========================================================================
void parseGeminiResponse(const String& jsonResponse,
                         String& summary, String& urgency) {
    summary = "Door activity captured. (Gemini cloud AI was temporarily busy under high demand).";
    urgency = "low";
    if (jsonResponse.isEmpty()) return;

    // Step 1 — filter-parse the outer Gemini envelope, keeping only the text field
    JsonDocument filter;
    filter["candidates"][0]["content"]["parts"][0]["text"] = true;

    JsonDocument outer;
    DeserializationError err = deserializeJson(outer, jsonResponse,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.println("Gemini outer parse error: " + String(err.c_str()));
        return;
    }

    const char* innerText = outer["candidates"][0]["content"]["parts"][0]["text"];
    if (!innerText) {
        Serial.println("Gemini: unexpected envelope shape");
        return;
    }

    // Step 2 — parse the structured JSON the model returned
    JsonDocument result;
    err = deserializeJson(result, innerText);
    if (err) {
        Serial.println("Gemini inner parse error: " + String(err.c_str()));
        summary = String(innerText);  // fallback: use raw text
        return;
    }

    summary = result["summary"] | "No summary provided.";
    urgency = result["urgency"] | "none";

    // Log all fields for debugging over Serial
    Serial.printf("  people=%d  packages=%d  vehicles=%d  quality=%s\n",
                  (int)result["people"],
                  (int)result["packages"],
                  (int)result["vehicles"],
                  (const char*)result["image_quality"]);

    JsonArray actions = result["actions"].as<JsonArray>();
    for (JsonVariant a : actions) {
        Serial.println("  action: " + String(a.as<const char*>()));
    }
}

// ===========================================================================
// Telegram — Send Photo
// ===========================================================================
bool sendTelegramPhoto(camera_fb_t* fb, const String& caption) {
    if (!fb) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://" + String(TELEGRAM_HOST) +
                 "/bot" + TELEGRAM_TOKEN + "/sendPhoto";

    http.begin(client, url);
    http.setTimeout(15000);

    // Build multipart/form-data manually
    String boundary = "----ESP32Boundary";
    String bodyHead =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
        String(TELEGRAM_CHAT_ID) + "\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
        caption + "\r\n"
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"door.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String bodyTail = "\r\n--" + boundary + "--\r\n";

    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    // Assemble the full multipart body in one heap buffer:
    //   [bodyHead bytes] + [raw JPEG bytes] + [bodyTail bytes]
    size_t headLen = bodyHead.length();
    size_t tailLen = bodyTail.length();
    size_t totalLen = headLen + fb->len + tailLen;

    uint8_t* bodyBuf = psramFound() ? (uint8_t*)ps_malloc(totalLen) : (uint8_t*)malloc(totalLen);
    if (!bodyBuf) {
        Serial.println("Telegram photo: malloc failed for body buffer");
        http.end();
        return false;
    }
    memcpy(bodyBuf,                    bodyHead.c_str(), headLen);
    memcpy(bodyBuf + headLen,          fb->buf,          fb->len);
    memcpy(bodyBuf + headLen + fb->len, bodyTail.c_str(), tailLen);

    int httpCode = http.sendRequest("POST", bodyBuf, totalLen);
    free(bodyBuf);

    if (httpCode != 200) {
        Serial.printf("Telegram photo HTTP error: %d\n", httpCode);
        Serial.println(http.getString().substring(0, 200));
        http.end();
        return false;
    }

    Serial.println("Telegram photo sent OK");
    http.end();
    return true;
}

// ===========================================================================
// Telegram — Send Text Message
// ===========================================================================
bool sendTelegramMessage(const String& text) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://" + String(TELEGRAM_HOST) +
                 "/bot" + TELEGRAM_TOKEN + "/sendMessage";

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    JsonDocument doc;
    doc["chat_id"]    = TELEGRAM_CHAT_ID;
    doc["text"]       = text;
    doc["parse_mode"] = "Markdown";

    String body;
    serializeJson(doc, body);

    int httpCode = http.POST(body);
    if (httpCode != 200) {
        Serial.printf("Telegram message HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    Serial.println("Telegram message sent OK");
    http.end();
    return true;
}

// ===========================================================================
// Sleep Mode (PIR-Armed: Wakes on GPIO13 HIGH)
// ===========================================================================
void goToSleep() {
    Serial.println("\n=== Entering Sleep Mode ===");
    Serial.println("Powering down WiFi radio...");

    // Disconnect WiFi to eliminate idle power consumption
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Ensure PIR signal is truly LOW before arming interrupt
    // (Prevents instant re-trigger with a 10-second safety timeout)
    unsigned long waitLowStart = millis();
    while (digitalRead(PIR_GPIO) == HIGH) {
        if (millis() - waitLowStart > 10000) {
            Serial.println("PIR remained HIGH for 10s — proceeding to arm wakeup");
            break;
        }
        Serial.println("PIR still HIGH, waiting for sensor to clear...");
        delay(300);
    }

    // Arm PIR on GPIO13 as ext0 wakeup source
    rtc_gpio_pullup_dis(PIR_GPIO);
    rtc_gpio_pulldown_en(PIR_GPIO);
    esp_sleep_enable_ext0_wakeup(PIR_GPIO, HIGH);

#if USE_LIGHT_SLEEP
    Serial.println("Entering Light Sleep (Instant wake <10ms, RAM & camera preserved)...");
    Serial.flush();
    esp_light_sleep_start();
#else
    Serial.println("Entering Deep Sleep (wakes on PIR HIGH or RST button)...");
    Serial.flush();
    esp_deep_sleep_start();
#endif
}
