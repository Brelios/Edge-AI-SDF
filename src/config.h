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
#define WIFI_TIMEOUT_MS            10000               // Wi-Fi connection timeout (ms)
#define WIFI_RECONNECT_INTERVAL_MS 30000               // How often to retry Wi-Fi if dropped (ms)
#define PIR_GPIO                   GPIO_NUM_13         // Optional hardware PIR pin (auto-fallback)

// ---------------------------------------------------------------------------
// Motion Detection Configuration (Optical JPEG + Hardware PIR)
// ---------------------------------------------------------------------------
// Combined score threshold (0.0–1.0) that must be exceeded to trigger an optical alert.
// 0.07 = 7% combined size & entropy change (sensitive to hand gestures, doorway entries)
#define MOTION_SENSITIVITY         0.07f

// Minimum milliseconds between successive Gemini/Telegram alerts.
#define MOTION_COOLDOWN_MS         4000               // 4 seconds between alert cycles (snappy response, prevents duplicate bursts)

// Milliseconds between surveillance frames (200ms = 5 checks per second for fast reaction)
#define MOTION_POLL_INTERVAL_MS    200

// If true: Gemini-classified false_alarm events are silently suppressed.
// If false: false_alarm events still send a low-priority Telegram notification.
#define SUPPRESS_FALSE_ALARMS      true

// ---------------------------------------------------------------------------
// Flash LED Configuration
// ---------------------------------------------------------------------------
#define FLASH_LED_PIN              4                   // Onboard high-power white flash LED (GPIO 4)
#define ENABLE_FLASH_ON_CAPTURE    true                // Always flash when clicking a picture (/snap & motion)
#define FLASH_DURATION_MS          120                 // Duration flash remains active to expose frame (ms)

// ---------------------------------------------------------------------------
// Telegram Remote Command Polling
// ---------------------------------------------------------------------------
// Check Telegram for incoming commands (/snap, /status, etc.) every 3.5s
// so camera motion loop runs with high frame rate and zero stutter.
#define TELEGRAM_POLL_INTERVAL_MS  3500

// ---------------------------------------------------------------------------
// Cloud API Endpoints & Models
// ---------------------------------------------------------------------------
#define GEMINI_HOST                "generativelanguage.googleapis.com"
#define GEMINI_MODEL               "gemini-3.5-flash"  // Working model with active quota
#define TELEGRAM_HOST              "api.telegram.org"
