#pragma once
#include <Arduino.h>

// Send Base64 JPEG to Gemini 3.8 Flash Vision API with structured JSON responseSchema
String callGeminiAPI(const String& base64Image);

// Parse structured JSON response into summary and urgency
void parseGeminiResponse(const String& jsonResponse, String& summary, String& urgency);
