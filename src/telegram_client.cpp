#include "telegram_client.h"
#include "camera_driver.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

bool sendTelegramPhoto(camera_fb_t* fb, const String& caption) {
    if (!fb || !fb->buf || fb->len == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    if (!client.connect(TELEGRAM_HOST, 443)) {
        Serial.println("[Telegram] Connection to api.telegram.org failed");
        return false;
    }

    String boundary = "----ESP32Boundary7MA4YWxkTrZu0gW";
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

    size_t totalLen = bodyHead.length() + fb->len + bodyTail.length();

    String request =
        "POST /bot" + String(TELEGRAM_TOKEN) + "/sendPhoto HTTP/1.1\r\n"
        "Host: " + String(TELEGRAM_HOST) + "\r\n"
        "User-Agent: ESP32-CAM\r\n"
        "Connection: close\r\n"
        "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
        "Content-Length: " + String(totalLen) + "\r\n\r\n";

    client.print(request);
    client.print(bodyHead);

    // Stream JPEG data directly in 1024-byte chunks — zero extra heap allocation
    const size_t chunkSize = 1024;
    size_t remaining = fb->len;
    uint8_t* ptr = fb->buf;
    while (remaining > 0) {
        size_t toWrite = (remaining < chunkSize) ? remaining : chunkSize;
        size_t written = client.write(ptr, toWrite);
        if (written == 0) {
            Serial.println("[Telegram] client.write stalled");
            break;
        }
        ptr += written;
        remaining -= written;
    }

    client.print(bodyTail);
    client.flush();

    // Read response status
    int httpCode = 0;
    unsigned long startMs = millis();
    while (client.connected() && (millis() - startMs < 10000)) {
        String line = client.readStringUntil('\n');
        if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
            httpCode = line.substring(9, 12).toInt();
            break;
        }
    }
    client.stop();

    if (httpCode == 200) {
        Serial.println("[Telegram] Photo sent successfully!");
        return true;
    } else {
        Serial.printf("[Telegram] sendPhoto error, HTTP code: %d\n", httpCode);
        return false;
    }
}

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
        Serial.printf("[Telegram] sendMessage HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    Serial.println("[Telegram] Message sent OK");
    http.end();
    return true;
}

// ---------------------------------------------------------------------------
// processTelegramCommands()
//
// Polls Telegram getUpdates for pending messages addressed to your bot.
// Only responds to the configured TELEGRAM_CHAT_ID for security.
// Returns true if a /snap was dispatched (caller may skip motion check that cycle).
// ---------------------------------------------------------------------------
bool processTelegramCommands(bool& motionEnabled) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8);

    HTTPClient http;

    // Persistent update offset to avoid re-processing old messages.
    static long updateOffset = 0;

    String url = "https://" + String(TELEGRAM_HOST) +
                 "/bot" + TELEGRAM_TOKEN +
                 "/getUpdates?timeout=0&limit=5&offset=" + String(updateOffset);

    http.begin(client, url);
    http.setTimeout(8000);
    int httpCode = http.GET();

    if (httpCode != 200) {
        Serial.printf("[Telegram] getUpdates error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.println("[Telegram] getUpdates parse error: " + String(err.c_str()));
        return false;
    }

    bool snapped = false;
    JsonArray results = doc["result"].as<JsonArray>();

    for (JsonObject update : results) {
        long updateId = update["update_id"] | 0L;
        updateOffset = updateId + 1;  // Advance past this update

        // Extract message fields
        const char* text     = update["message"]["text"] | "";
        long        chatId   = update["message"]["chat"]["id"] | 0L;
        const char* username = update["message"]["chat"]["username"] | "unknown";

        // Security check: ignore messages not from the owner's chat
        if (String(chatId) != String(TELEGRAM_CHAT_ID)) {
            Serial.printf("[Telegram] Ignored command from unauthorized chat_id: %ld\n", chatId);
            continue;
        }

        String cmd = String(text);
        cmd.trim();
        Serial.printf("[Telegram] Command from @%s: %s\n", username, cmd.c_str());

        // ---------------------------------------------------------------
        // /snap — Instantly capture and send a live photo (zero Gemini, zero base64)
        // ---------------------------------------------------------------
        if (cmd == "/snap") {
            camera_fb_t* fb = captureJPEG();
            if (fb) {
                String caption = "📸 *Live Snap*  •  " + String(fb->width) + "×" + String(fb->height) +
                                 "  |  📶 " + String(WiFi.RSSI()) + " dBm";
                sendTelegramPhoto(fb, caption);
                esp_camera_fb_return(fb);
                snapped = true;
            } else {
                sendTelegramMessage("❌ Snap failed — camera capture error.");
            }
        }

        // ---------------------------------------------------------------
        // /status — System diagnostic card
        // ---------------------------------------------------------------
        else if (cmd == "/status") {
            uint32_t upSec  = millis() / 1000;
            uint32_t upHr   = upSec / 3600;
            uint32_t upMin  = (upSec % 3600) / 60;

            String resStr = psramFound() ? "OV2640 HD (1280×720)" : "OV2640 QVGA (320×240)";
            String msg =
                "🖥 *Edge AI Door Feed — System Status*\n"
                "─────────────────────────\n"
                "⏱ *Uptime:* " + String(upHr) + "h " + String(upMin) + "m\n"
                "📶 *Wi-Fi Signal:* " + String(WiFi.RSSI()) + " dBm\n"
                "🌐 *Local IP:* " + WiFi.localIP().toString() + "\n"
                "🛡 *Motion Alerts:* " + String(motionEnabled ? "✅ Active" : "⏸ Paused") + "\n"
                "🧠 *Free Heap:* " + String(ESP.getFreeHeap() / 1024) + " KB\n"
                "💾 *PSRAM:* " + (psramFound() ? String(ESP.getFreePsram() / 1024) + " KB" : "Disabled (DRAM mode)") + "\n"
                "📷 *Camera:* " + resStr + "\n"
                "🤖 *AI Model:* " + String(GEMINI_MODEL);
            sendTelegramMessage(msg);
        }

        // ---------------------------------------------------------------
        // /motion off — Pause automatic motion alerts
        // ---------------------------------------------------------------
        else if (cmd == "/motion off") {
            motionEnabled = false;
            sendTelegramMessage("⏸ *Motion alerts paused.*\nNo automatic alerts will be sent until you send /motion on.");
        }

        // ---------------------------------------------------------------
        // /motion on — Re-enable automatic motion alerts
        // ---------------------------------------------------------------
        else if (cmd == "/motion on") {
            motionEnabled = true;
            sendTelegramMessage("✅ *Motion alerts re-enabled.*\nYou will now receive automatic door alerts.");
        }

        // ---------------------------------------------------------------
        // /help — List all commands
        // ---------------------------------------------------------------
        else if (cmd == "/help") {
            String helpMsg =
                "🤖 *Edge AI Door Feed — Commands*\n"
                "─────────────────────────\n"
                "📸 /snap — Capture a live photo now\n"
                "🖥 /status — System health & diagnostics\n"
                "⏸ /motion off — Pause automatic AI alerts\n"
                "✅ /motion on — Resume automatic AI alerts\n"
                "❓ /help — Show this message";
            sendTelegramMessage(helpMsg);
        }

        else {
            sendTelegramMessage("❓ Unknown command. Send /help to see what's available.");
        }
    }

    return snapped;
}
