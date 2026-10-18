#pragma once
#include <Arduino.h>
#include <vector>

// ---------------------------------------------------------------------------
// Structured result from Gemini 3.8 Flash Vision AI
// ---------------------------------------------------------------------------
struct GeminiAnalysis {
    String summary;
    String category;              // "delivery", "resident_visitor", "suspicious", "threat", "animal", "false_alarm"
    String urgency;               // "none", "low", "medium", "high"
    int people = 0;
    int packages = 0;
    int vehicles = 0;
    String image_quality = "good"; // "good", "poor"
    std::vector<String> actions;
    bool success = false;
};

// Send Base64 JPEG to Gemini 3.8 Flash Vision API with structured JSON responseSchema
String callGeminiAPI(const String& base64Image);

// Parse structured JSON response into GeminiAnalysis
GeminiAnalysis parseGeminiResponse(const String& jsonResponse);

// Backward-compatible overload for legacy callers
void parseGeminiResponse(const String& jsonResponse, String& summary, String& urgency);

// Format human-friendly Telegram alert caption from Gemini analysis
String formatTelegramCaption(const GeminiAnalysis& analysis);
