#include "telegram_client.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
