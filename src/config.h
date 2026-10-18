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

// ---------------------------------------------------------------------------
// Optical Motion Detection Configuration
// ---------------------------------------------------------------------------
// Pixel-change fraction (0.0–1.0) that must be exceeded to trigger an alert.
// Lower = more sensitive (catches subtle movement); higher = requires major scene change.
// Recommended range: 0.04 – 0.10
#define MOTION_SENSITIVITY         0.06f              // 6% of pixels must change to trigger

// Minimum milliseconds between successive Gemini/Telegram alerts.
// Prevents flooding when a person stands at the door for an extended period.
#define MOTION_COOLDOWN_MS         12000              // 12 seconds between alert cycles

// Milliseconds between surveillance frames (lower = faster detection, slightly more CPU).
#define MOTION_POLL_INTERVAL_MS    300                // Check frames every 300ms

// If true: Gemini-classified false_alarm events are silently suppressed.
// If false: false_alarm events still send a low-priority Telegram notification.
#define SUPPRESS_FALSE_ALARMS      true

// ---------------------------------------------------------------------------
// Telegram Remote Command Polling
// ---------------------------------------------------------------------------
// How often the firmware checks Telegram for incoming commands (/snap, /status, etc.)
#define TELEGRAM_POLL_INTERVAL_MS  1500               // Poll every 1.5 seconds

// ---------------------------------------------------------------------------
// Cloud API Endpoints & Models
// ---------------------------------------------------------------------------
#define GEMINI_HOST                "generativelanguage.googleapis.com"
#define GEMINI_MODEL               "gemini-3.8-flash"
#define TELEGRAM_HOST              "api.telegram.org"
