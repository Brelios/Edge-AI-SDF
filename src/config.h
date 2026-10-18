#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Credentials & Secrets
// ---------------------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("include/secrets.h")
#include "include/secrets.h"
#else
#error "secrets.h missing! Please copy 'include/secrets_example.h' to 'include/secrets.h' and fill in your credentials."
#endif

// ---------------------------------------------------------------------------
// Hardware & Timing Configuration
// ---------------------------------------------------------------------------
#define PIR_GPIO                  GPIO_NUM_13          // PIR sensor OUT connected to IO13
#define PIR_CAPTURE_INTERVAL_MS   4000                 // Interval between captures while motion is active (ms)
#define SLEEP_DEBOUNCE_MS         1500                 // Debounce wait after PIR goes LOW (ms)
#define WIFI_TIMEOUT_MS           10000                // Wi-Fi connection timeout (ms)

// ===========================================================================
// Sleep Mode Selection:
// - USE_LIGHT_SLEEP = true (Recommended for USB / wall power):
//     * Wakes in < 10ms!
//     * Camera registers and RAM are kept intact (no reboot, no re-init delay)
//     * Current draw ~1.2mA
// - USE_LIGHT_SLEEP = false (For low-power battery setups):
//     * Deep sleep draws ~20µA, cold-reboots on PIR HIGH (~600ms wake-to-shutter)
// ===========================================================================
#define USE_LIGHT_SLEEP           true

// ---------------------------------------------------------------------------
// Cloud API Endpoints & Models
// ---------------------------------------------------------------------------
#define GEMINI_HOST               "generativelanguage.googleapis.com"
#define GEMINI_MODEL              "gemini-3.8-flash"
#define TELEGRAM_HOST             "api.telegram.org"
