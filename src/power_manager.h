#pragma once
#include <Arduino.h>

// Initialize PIR input pin and configure RTC internal pulldown
void initPowerManagement();

// Shut down Wi-Fi, ensure PIR is low, arm ext0 wake-up interrupt, and enter sleep mode
void goToSleep();
