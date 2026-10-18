#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// Upload a JPEG frame to Telegram Bot with caption using multipart/form-data
bool sendTelegramPhoto(camera_fb_t* fb, const String& caption);

// Send text notification to Telegram Bot
bool sendTelegramMessage(const String& text);
