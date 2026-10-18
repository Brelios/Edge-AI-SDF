#include "power_manager.h"
#include "config.h"
#include <WiFi.h>
#include "driver/rtc_io.h"

void initPowerManagement() {
    pinMode(PIR_GPIO, INPUT_PULLDOWN);
    rtc_gpio_pullup_dis(PIR_GPIO);
    rtc_gpio_pulldown_en(PIR_GPIO);
}

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
