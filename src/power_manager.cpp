#include "power_manager.h"
#include "config.h"
#include <WiFi.h>
#include "esp_wifi.h"

// ---------------------------------------------------------------------------
// connectWiFi()
//
// Connects to Wi-Fi in STA mode and immediately enables Automatic Modem Sleep.
// With WIFI_PS_MIN_MODEM enabled, the radio idles down between beacon intervals
// (~100ms), reducing idle current by ~40–50% while keeping the connection live.
// ---------------------------------------------------------------------------
bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Enable Wi-Fi Automatic Modem Sleep
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("[WiFi] Connecting");
    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT_MS) {
            Serial.println("\n[WiFi] Connection timeout!");
            return false;
        }
        delay(300);
        Serial.print(".");
    }

    Serial.println("\n[WiFi] Connected — IP: " + WiFi.localIP().toString() +
                   "  RSSI: " + String(WiFi.RSSI()) + " dBm");
    return true;
}

// ---------------------------------------------------------------------------
// maintainWiFi()
//
// Silently reconnects Wi-Fi if the connection was lost (e.g. router restart).
// Rate-limited with WIFI_RECONNECT_INTERVAL_MS to avoid busy-looping.
// ---------------------------------------------------------------------------
void maintainWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt < WIFI_RECONNECT_INTERVAL_MS) return;
    lastAttempt = millis();

    Serial.println("[WiFi] Connection lost — attempting reconnect...");
    WiFi.disconnect(false);
    WiFi.reconnect();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Reconnected — IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("[WiFi] Reconnect failed — will retry later.");
    }
}
