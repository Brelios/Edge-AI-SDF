#pragma once
#include <Arduino.h>

// Initialize Wi-Fi in STA mode with Automatic Modem Sleep enabled.
// Call once in setup() before any cloud calls.
bool connectWiFi();

// Ensure Wi-Fi is still connected; silently reconnect if dropped.
// Call this periodically in loop() to maintain continuous uptime.
void maintainWiFi();
