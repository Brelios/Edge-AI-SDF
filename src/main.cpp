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
#define PIR_GPIO         GPIO_NUM_13       // PIR OUT connected to IO13
#define WIFI_TIMEOUT_MS  10000            // 10 s WiFi connect timeout
#define COOLDOWN_US      (30ULL * 1000000ULL) // 30 s deep-sleep cooldown

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
void     goToSleep();

// ---------------------------------------------------------------------------
// setup() — runs on every wake from deep sleep
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Edge AI Door Feed — Woke Up ===");

    // ── 1. Wakeup reason ───────────────────────────────────────────────────
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("Wakeup cause: %d (Running immediately — Direct Test Mode)\n", cause);

    Serial.printf("Initial free heap: %u bytes\n", ESP.getFreeHeap());

    // ── 2. Init camera FIRST (allocates contiguous DMA memory before heap fragments) ──
    if (!initCamera()) {
        Serial.println("Camera init failed — sleeping");
        goToSleep();
        return;
    }
    Serial.printf("Camera OK — Free heap: %u bytes\n", ESP.getFreeHeap());
    delay(500); // Allow sensor AGC and AEC to stabilize before capturing

    // ── 3. Capture JPEG + Base64 encode immediately (instant shutter) ───────
    camera_fb_t* fb = nullptr;
    String base64Image = captureAndEncodeJPEG(&fb);
    if (base64Image.isEmpty() || fb == nullptr) {
        Serial.println("Capture/encode failed — sleeping");
        if (fb) esp_camera_fb_return(fb);
        goToSleep();
        return;
    }
    Serial.printf("JPEG captured: %u bytes → Base64: %u chars — Free heap: %u bytes\n",
                  fb->len, base64Image.length(), ESP.getFreeHeap());

    // ── 4. Connect to WiFi ──────────────────────────────────────────────────
    if (!connectWiFi()) {
        Serial.println("WiFi failed — sleeping");
        if (fb) esp_camera_fb_return(fb);
        goToSleep();
        return;
    }
    Serial.printf("WiFi connected — Free heap: %u bytes\n", ESP.getFreeHeap());


    // ── 6 & 7. Call Gemini API + parse response ─────────────────────────────
    String geminiResponse = callGeminiAPI(base64Image);
    String summary, urgency;
    parseGeminiResponse(geminiResponse, summary, urgency);
    Serial.println("Gemini summary: " + summary + " [" + urgency + "]");

    // ── 8. Send Telegram photo + message ────────────────────────────────────
    // Urgency emoji map
    String emoji = "🟢";
    if      (urgency == "low")    emoji = "🟡";
    else if (urgency == "medium") emoji = "🟠";
    else if (urgency == "high")   emoji = "🔴";

    sendTelegramPhoto(fb, summary);
    sendTelegramMessage(emoji + " *Door Alert* [" + urgency + "]\n" + summary);

    // ── 9. Release frame buffer ─────────────────────────────────────────────
    esp_camera_fb_return(fb);

    // ── 10. Deep sleep ──────────────────────────────────────────────────────
    Serial.println("Done — entering deep sleep (30s cooldown)");
    goToSleep();
}

void loop() {
    // Never reached — everything runs in setup() each wake cycle
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
        Serial.println("PSRAM found — configuring VGA (640x480)");
        config.frame_size   = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
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

    // Improve image quality on OV2640
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
    }
    return true;
}

// ===========================================================================
// WiFi Connection
// ===========================================================================
bool connectWiFi() {
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
    Serial.println("Capturing camera frame...");

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

    // Calculate Base64 output size and allocate buffer
    size_t encodedLen = ((fb->len + 2) / 3) * 4 + 1;
    uint8_t* encodedBuf = (uint8_t*)malloc(encodedLen);
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
        "from a low-resolution camera (about 640x480) that may be dark or grainy. "
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
    summary = "No response from Gemini.";
    urgency = "none";
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

    uint8_t* bodyBuf = (uint8_t*)malloc(totalLen);
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
// Deep Sleep
// ===========================================================================
void goToSleep() {
    Serial.println("\n=== Cycle Complete ===");
    Serial.println("Configuring wakeup: PIR (GPIO13) + 15-second auto-wakeup timer...");
    
    // 1. Arm PIR on GPIO13 (if attached)
    rtc_gpio_pullup_dis(PIR_GPIO);
    rtc_gpio_pulldown_en(PIR_GPIO);
    esp_sleep_enable_ext0_wakeup(PIR_GPIO, HIGH);

    // 2. Arm 15s timer wakeup (allows hands-free testing without PIR!)
    esp_sleep_enable_timer_wakeup(15ULL * 1000000ULL);

    Serial.println("Entering deep sleep (wakes in 15s or on PIR / RST button)...");
    delay(1000);
    Serial.flush();
    esp_deep_sleep_start();
}
