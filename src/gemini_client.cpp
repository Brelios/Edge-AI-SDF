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

    // Advanced security guard system prompt — plain ASCII, single quotes only
    static const char* systemPrompt =
        "You are an autonomous AI doorstep security assistant for a home entrance camera. "
        "Analyze the provided single still frame from the front door camera. "
        "Provide a factual, objective, and accurate assessment. Never assume intent or invent details not clearly visible. "
        "Do not use conversational filler (such as 'In this image' or 'We see'); start directly with the subject. "
        "CATEGORIZATION: "
        "- delivery: couriers with uniforms, safety vests, scanner devices, packages, mailers, or parcel drop-offs. "
        "- resident_visitor: people approaching normally, knocking, ringing the bell, or entering/exiting peacefully. "
        "- suspicious: loitering without knocking/ringing, face deliberately concealed (hood/mask/gaiter), window peering, or hesitant night lingering. "
        "- threat: forced entry, tampering with door locks/handles, visible weapons or tools, aggressive actions, or package theft in progress. "
        "- animal: pets (dogs, cats) or wildlife in the field of view. "
        "- false_alarm: empty doorstep, tree branches swaying in wind, moving shadows, vehicle headlights, glare, or insects/rain on the lens. "
        "URGENCY: "
        "- none: false alarms, empty scene, harmless wildlife, or ambient motion. "
        "- low: routine parcel delivery, resident arrival/departure, or familiar visitor. "
        "- medium: unfamiliar person loitering, peering in windows, or concealing identity. "
        "- high: active break-in attempt, lock manipulation, package theft, or immediate hazard. "
        "SUMMARY: Exactly 1 to 2 clear, concise sentences. "
        "IMAGE QUALITY: 'poor' if too dark, blurry, or blocked; otherwise 'good'. "
        "ACTIONS: 1 to 2 imperative suggestions under 8 words each (e.g., 'Retrieve package', 'Verify door lock', 'None required').";

    // responseSchema forces Gemini 3.8 Flash to return strict JSON matching this structure
    static const char* schema = R"json({
      "type": "OBJECT",
      "properties": {
        "summary":       { "type": "STRING" },
        "category":      { "type": "STRING", "enum": ["delivery", "resident_visitor", "suspicious", "threat", "animal", "false_alarm"] },
        "urgency":       { "type": "STRING", "enum": ["none", "low", "medium", "high"] },
        "people":        { "type": "INTEGER" },
        "packages":      { "type": "INTEGER" },
        "vehicles":      { "type": "INTEGER" },
        "image_quality": { "type": "STRING", "enum": ["good", "poor"] },
        "actions":       { "type": "ARRAY", "items": { "type": "STRING" } }
      },
      "required": ["summary", "category", "urgency", "people", "packages", "vehicles", "image_quality", "actions"]
    })json";

    String body;
    body.reserve(base64Image.length() + 3500);

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

        // Retry once after 2 seconds if temporary 503 (high demand) or 429 (rate spike)
        if (httpCode == 503 || httpCode == 429) {
            Serial.printf("Gemini %d (busy/rate limit) — retrying once in 2s...\n", httpCode);
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

GeminiAnalysis parseGeminiResponse(const String& jsonResponse) {
    GeminiAnalysis analysis;

    if (jsonResponse.isEmpty()) {
        analysis.summary = "Motion detected at front door. (Gemini cloud AI temporarily busy or unavailable).";
        analysis.category = "suspicious";
        analysis.urgency = "low";
        analysis.image_quality = "good";
        analysis.success = false;
        return analysis;
    }

    // Filter outer envelope to minimize RAM usage in ArduinoJson
    JsonDocument filter;
    filter["candidates"][0]["content"]["parts"][0]["text"] = true;

    JsonDocument outer;
    DeserializationError err = deserializeJson(outer, jsonResponse,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.println("Gemini outer parse error: " + String(err.c_str()));
        analysis.summary = "Door activity captured (Failed to parse outer AI envelope).";
        analysis.category = "suspicious";
        analysis.urgency = "low";
        analysis.success = false;
        return analysis;
    }

    const char* innerText = outer["candidates"][0]["content"]["parts"][0]["text"];
    if (!innerText) {
        Serial.println("Gemini: unexpected envelope shape");
        analysis.summary = "Door activity captured (Empty AI response parts).";
        analysis.category = "suspicious";
        analysis.urgency = "low";
        analysis.success = false;
        return analysis;
    }

    // Parse structured JSON returned by Gemini
    JsonDocument result;
    err = deserializeJson(result, innerText);
    if (err) {
        Serial.println("Gemini inner parse error: " + String(err.c_str()));
        analysis.summary = String(innerText);
        analysis.category = "resident_visitor";
        analysis.urgency = "low";
        analysis.success = false;
        return analysis;
    }

    analysis.summary       = result["summary"] | "Activity detected at front door.";
    analysis.category      = result["category"] | "resident_visitor";
    analysis.urgency       = result["urgency"] | "none";
    analysis.people        = result["people"] | 0;
    analysis.packages      = result["packages"] | 0;
    analysis.vehicles      = result["vehicles"] | 0;
    analysis.image_quality = result["image_quality"] | "good";
    analysis.success       = true;

    JsonArray actionsArr = result["actions"].as<JsonArray>();
    for (JsonVariant a : actionsArr) {
        if (a.is<const char*>()) {
            analysis.actions.push_back(String(a.as<const char*>()));
        }
    }

    Serial.printf("Gemini parsed: category=%s  urgency=%s  people=%d  packages=%d  vehicles=%d  quality=%s\n",
                  analysis.category.c_str(),
                  analysis.urgency.c_str(),
                  analysis.people,
                  analysis.packages,
                  analysis.vehicles,
                  analysis.image_quality.c_str());

    for (const String& act : analysis.actions) {
        Serial.println("  action: " + act);
    }

    return analysis;
}

void parseGeminiResponse(const String& jsonResponse,
                         String& summary, String& urgency) {
    GeminiAnalysis a = parseGeminiResponse(jsonResponse);
    summary = a.summary;
    urgency = a.urgency;
}

String formatTelegramCaption(const GeminiAnalysis& analysis) {
    if (!analysis.success && analysis.summary.indexOf("temporarily busy") != -1) {
        return "⚠️ Door Motion Alert\n\n" + analysis.summary;
    }

    String headerIcon = "🔔";
    String catTitle = "Door Event";

    if (analysis.category == "threat") {
        headerIcon = "🚨";
        catTitle = "Threat";
    } else if (analysis.category == "suspicious") {
        headerIcon = "👁️";
        catTitle = "Suspicious Activity";
    } else if (analysis.category == "delivery") {
        headerIcon = "📦";
        catTitle = "Delivery";
    } else if (analysis.category == "resident_visitor") {
        headerIcon = "👤";
        catTitle = "Visitor / Resident";
    } else if (analysis.category == "animal") {
        headerIcon = "🐾";
        catTitle = "Animal";
    } else if (analysis.category == "false_alarm") {
        headerIcon = "🍃";
        catTitle = "Ambient Motion";
    }

    String urgencyUpper = analysis.urgency;
    urgencyUpper.toUpperCase();

    String caption = headerIcon + " Door Alert [" + urgencyUpper + "] • " + catTitle + "\n\n";
    caption += analysis.summary + "\n\n";

    // Entity counts line
    String entities = "";
    if (analysis.people > 0 || analysis.category == "resident_visitor" ||
        analysis.category == "suspicious" || analysis.category == "threat") {
        entities += "👥 People: " + String(analysis.people) + "  ";
    }
    if (analysis.packages > 0 || analysis.category == "delivery") {
        entities += "📦 Packages: " + String(analysis.packages) + "  ";
    }
    if (analysis.vehicles > 0) {
        entities += "🚗 Vehicles: " + String(analysis.vehicles);
    }
    if (!entities.isEmpty()) {
        caption += entities + "\n";
    }

    // Recommended actions
    if (!analysis.actions.empty()) {
        if (analysis.actions.size() == 1) {
            caption += "💡 Action: " + analysis.actions[0] + "\n";
        } else {
            caption += "💡 Actions:\n";
            for (size_t i = 0; i < analysis.actions.size(); i++) {
                caption += "• " + analysis.actions[i] + "\n";
            }
        }
    }

    if (analysis.image_quality == "poor") {
        caption += "\n⚠️ Image Quality: Poor (Check lighting/lens)";
    }

    // Keep well within Telegram's 1024-char photo caption limit
    if (caption.length() > 1000) {
        caption = caption.substring(0, 995) + "...";
    }

    return caption;
}

