#pragma once
#include <Arduino.h>
#include "esp_camera.h"
#include "gemini_client.h"

// Upload a JPEG frame to Telegram Bot with caption using multipart/form-data
bool sendTelegramPhoto(camera_fb_t* fb, const String& caption);

// Send a plain text message to Telegram Bot (supports Markdown formatting)
bool sendTelegramMessage(const String& text);

// ---------------------------------------------------------------------------
// Telegram Interactive Command Engine
//
// Polls Telegram getUpdates API for incoming commands and handles:
//   /snap        — Capture and send a live HD snapshot immediately
//   /status      — System health diagnostic card
//   /motion on   — Re-enable automatic motion detection alerts
//   /motion off  — Pause automatic motion detection alerts
//   /help        — List available commands
//
// Requires: motionEnabled (passed by reference so /motion on/off can toggle it)
// Returns:  true if a snap was processed (caller can use this to skip motion poll)
// ---------------------------------------------------------------------------
bool processTelegramCommands(bool& motionEnabled);
