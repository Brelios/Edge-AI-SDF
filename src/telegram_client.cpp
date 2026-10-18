#include "telegram_client.h"
#include "camera_driver.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

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

    size_t headLen = bodyHead.length();
    size_t tailLen = bodyTail.length();
    size_t totalLen = headLen + fb->len + tailLen;

    uint8_t* bodyBuf = psramFound() ? (uint8_t*)ps_malloc(totalLen) : (uint8_t*)malloc(totalLen);
    if (!bodyBuf) {
        Serial.println("Telegram photo: malloc failed for body buffer");
        http.end();
        return false;
    }
    memcpy(bodyBuf,                     bodyHead.c_str(), headLen);
    memcpy(bodyBuf + headLen,           fb->buf,          fb->len);
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
    client.setTimeout(8);  // short socket timeout to stay non-blocking

    HTTPClient http;

    // Persistent update offset to avoid re-processing old messages.
    // Survives across calls via static storage.
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
        const char* text    = update["message"]["text"] | "";
        long        chatId  = update["message"]["chat"]["id"] | 0L;
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
        // /snap — Instantly capture and send a live HD snapshot
        // ---------------------------------------------------------------
        if (cmd == "/snap") {
            sendTelegramMessage("📸 Capturing snapshot...");

            camera_fb_t* fb = nullptr;
            String base64 = captureAndEncodeJPEG(&fb);

            if (!base64.isEmpty() && fb != nullptr) {
                String caption =
                    "📸 *Live Snap* — On-Demand Snapshot\n\n"
                    "📶 Wi-Fi: " + String(WiFi.RSSI()) + " dBm\n"
                    "🧠 Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n"
                    "⏱ Uptime: " + String(millis() / 60000) + " min\n"
                    "🛡 Motion alerts: " + String(motionEnabled ? "Active" : "Paused");
                sendTelegramPhoto(fb, caption);
                esp_camera_fb_return(fb);
                snapped = true;
            } else {
                sendTelegramMessage("❌ Snapshot failed — camera unavailable.");
                if (fb) esp_camera_fb_return(fb);
            }
        }

        // ---------------------------------------------------------------
        // /status — System diagnostic card
        // ---------------------------------------------------------------
        else if (cmd == "/status") {
            uint32_t upSec  = millis() / 1000;
            uint32_t upHr   = upSec / 3600;
            uint32_t upMin  = (upSec % 3600) / 60;

            String msg =
                "🖥 *Edge AI Door Feed — System Status*\n"
                "─────────────────────────\n"
                "⏱ *Uptime:* " + String(upHr) + "h " + String(upMin) + "m\n"
                "📶 *Wi-Fi Signal:* " + String(WiFi.RSSI()) + " dBm\n"
                "🌐 *Local IP:* " + WiFi.localIP().toString() + "\n"
                "🛡 *Motion Alerts:* " + String(motionEnabled ? "✅ Active" : "⏸ Paused") + "\n"
                "🧠 *Free Heap:* " + String(ESP.getFreeHeap() / 1024) + " KB\n"
                "💾 *Free PSRAM:* " + (psramFound() ? String(ESP.getFreePsram() / 1024) + " KB" : "N/A") + "\n"
                "📷 *Camera:* OV2640 HD (1280×720)\n"
                "🤖 *AI Model:* gemini-3.8-flash";
            sendTelegramMessage(msg);
        }

        // ---------------------------------------------------------------
        // /motion off — Pause automatic Gemini + Telegram motion alerts
        // ---------------------------------------------------------------
        else if (cmd == "/motion off") {
            motionEnabled = false;
            sendTelegramMessage("⏸ *Motion alerts paused.*\nNo automatic alerts will be sent until you send /motion on.");
        }

        // ---------------------------------------------------------------
        // /motion on — Re-enable automatic Gemini + Telegram alerts
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
                "📸 /snap — Capture a live HD photo now\n"
                "🖥 /status — System health & diagnostics\n"
                "⏸ /motion off — Pause automatic AI alerts\n"
                "✅ /motion on — Resume automatic AI alerts\n"
                "❓ /help — Show this message";
            sendTelegramMessage(helpMsg);
        }

        else {
            // Unknown command
            sendTelegramMessage("❓ Unknown command. Send /help to see what's available.");
        }
    }

    return snapped;
}
