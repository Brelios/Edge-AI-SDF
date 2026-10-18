#include "gemini_client.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

String callGeminiAPI(const String& base64Image) {
    WiFiClientSecure client;
    client.setInsecure();   // Skip cert validation (no embedded CA)
    client.setTimeout(30);  // seconds — TLS handshake + large upload headroom

    HTTPClient http;
    String url = "https://" + String(GEMINI_HOST) +
                 "/v1beta/models/" + GEMINI_MODEL +
                 ":generateContent";

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", GEMINI_API_KEY);
    http.setTimeout(30000);

    // System prompt — plain ASCII, no embedded double-quotes
    static const char* systemPrompt =
        "You are a front-door security camera assistant. You receive one still image "
        "from an HD door camera (1280x720). "
        "Describe only what is clearly visible and never guess about anything you cannot make out. "
        "Do not identify anyone by name; use neutral descriptions such as clothing or what they carry. "
        "If nothing notable is happening, use the summary No activity at the door, urgency none, and an empty actions list. "
        "If the image is too dark, blurry, or blocked, set image_quality to poor and say so in the summary. "
        "Urgency guide: none = empty scene; low = a delivery or passerby; "
        "medium = someone lingering, trying the door, or covering their face; "
        "high = forced entry, weapons, or an apparent emergency. "
        "Give at most 3 actions, each under 10 words.";

    // responseSchema forces Gemini to return strict JSON
    static const char* schema = R"json({
      "type": "OBJECT",
      "properties": {
        "summary":       { "type": "STRING" },
        "people":        { "type": "INTEGER" },
        "packages":      { "type": "INTEGER" },
        "vehicles":      { "type": "INTEGER" },
        "urgency":       { "type": "STRING", "enum": ["none", "low", "medium", "high"] },
        "image_quality": { "type": "STRING", "enum": ["good", "poor"] },
        "actions":       { "type": "ARRAY", "items": { "type": "STRING" } }
      },
      "required": ["summary", "people", "packages", "vehicles", "urgency", "image_quality", "actions"]
    })json";

    String body;
    body.reserve(base64Image.length() + 2500);

    body  = "{\"system_instruction\":{\"parts\":[{\"text\":\"";
    body += systemPrompt;
    body += "\"}]},";
    body += "\"contents\":[{\"parts\":[";
    body += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
    body += base64Image;
    body += "\"}},";
    body += "{\"text\":\"Analyze this frame.\"}";
    body += "]}],";
    body += "\"generationConfig\":{\"temperature\":0.2,";
    body += "\"responseMimeType\":\"application/json\",";
    body += "\"responseSchema\":";
    body += schema;
    body += "}}";

    Serial.printf("Sending to Gemini (%u bytes)...\n", body.length());
    int httpCode = http.POST(body);

    if (httpCode != 200) {
        Serial.printf("Gemini HTTP error: %d\n", httpCode);
        Serial.println(http.getString().substring(0, 300));
        http.end();

        // If temporary 503 high demand spike, retry once after 2 seconds
        if (httpCode == 503) {
            Serial.println("Gemini 503 (high demand) — retrying once in 2s...");
            delay(2000);
            http.begin(client, url);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("x-goog-api-key", GEMINI_API_KEY);
            http.setTimeout(30000);
            httpCode = http.POST(body);
            if (httpCode == 200) {
                String response = http.getString();
                http.end();
                Serial.println("Gemini retry responded OK");
                return response;
            }
            Serial.printf("Gemini retry also failed: %d\n", httpCode);
            http.end();
        }
        return "";
    }

    String response = http.getString();
    http.end();
    Serial.println("Gemini responded OK");
    return response;
}

void parseGeminiResponse(const String& jsonResponse,
                         String& summary, String& urgency) {
    summary = "Door activity captured. (Gemini cloud AI was temporarily busy under high demand).";
    urgency = "low";
    if (jsonResponse.isEmpty()) return;

    // Filter-parse outer Gemini envelope, keeping only inner text field
    JsonDocument filter;
    filter["candidates"][0]["content"]["parts"][0]["text"] = true;

    JsonDocument outer;
    DeserializationError err = deserializeJson(outer, jsonResponse,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.println("Gemini outer parse error: " + String(err.c_str()));
        return;
    }

    const char* innerText = outer["candidates"][0]["content"]["parts"][0]["text"];
    if (!innerText) {
        Serial.println("Gemini: unexpected envelope shape");
        return;
    }

    // Parse structured JSON returned by Gemini
    JsonDocument result;
    err = deserializeJson(result, innerText);
    if (err) {
        Serial.println("Gemini inner parse error: " + String(err.c_str()));
        summary = String(innerText);
        return;
    }

    summary = result["summary"] | "No summary provided.";
    urgency = result["urgency"] | "none";

    Serial.printf("  people=%d  packages=%d  vehicles=%d  quality=%s\n",
                  (int)result["people"],
                  (int)result["packages"],
                  (int)result["vehicles"],
                  (const char*)result["image_quality"]);

    JsonArray actions = result["actions"].as<JsonArray>();
    for (JsonVariant a : actions) {
        Serial.println("  action: " + String(a.as<const char*>()));
    }
}
