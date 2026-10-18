#include "gemini_client.h"
#include "config.h"
#include "camera_driver.h"
#include "system_metrics.h"
#include "mbedtls/base64.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static const char* GEMINI_MODELS[] = {
    "gemini-3.7-flash",
    "gemini-3.5-flash-lite",
    "gemini-3.1-flash-lite",
    "gemini-3-flash-preview",
    "gemini-3.5-flash"
};
static const int NUM_MODELS = sizeof(GEMINI_MODELS) / sizeof(GEMINI_MODELS[0]);
static int currentModelIdx = 0;

static const char systemPrompt[] =
    "You are an autonomous AI doorstep security assistant for a home entrance camera. "
    "Analyze the provided single still frame from the front door camera. "
    "Provide a factual, objective, and accurate assessment. Never assume intent or invent details not clearly visible. "
    "Do not use conversational filler; start directly with the subject. "
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
    "ACTIONS: 1 to 2 imperative suggestions under 8 words each.";

static const char schemaJson[] =
    "{\"type\":\"OBJECT\",\"properties\":{"
    "\"summary\":{\"type\":\"STRING\"},"
    "\"category\":{\"type\":\"STRING\",\"enum\":[\"delivery\",\"resident_visitor\",\"suspicious\",\"threat\",\"animal\",\"false_alarm\"]},"
    "\"urgency\":{\"type\":\"STRING\",\"enum\":[\"none\",\"low\",\"medium\",\"high\"]},"
    "\"people\":{\"type\":\"INTEGER\"},"
    "\"packages\":{\"type\":\"INTEGER\"},"
    "\"vehicles\":{\"type\":\"INTEGER\"},"
    "\"image_quality\":{\"type\":\"STRING\",\"enum\":[\"good\",\"poor\"]},"
    "\"actions\":{\"type\":\"ARRAY\",\"items\":{\"type\":\"STRING\"}}"
    "},\"required\":[\"summary\",\"category\",\"urgency\",\"people\",\"packages\",\"vehicles\",\"image_quality\",\"actions\"]}";

static unsigned long lastQuotaExceededMs = 0;

String callGeminiAPI(camera_fb_t* fb) {
    if (!fb || !fb->buf || fb->len == 0) return "";

    // Fast-fail circuit breaker: if quota was exceeded, wait 60s before hammering API again
    if (lastQuotaExceededMs > 0 && (millis() - lastQuotaExceededMs < 60000)) {
        Serial.println("[Gemini] Quota cooldown active (60s circuit-breaker) — bypassing cloud call.");
        return "";
    }

    String jsonPre =
        "{\"system_instruction\":{\"parts\":[{\"text\":\"" + String(systemPrompt) + "\"}]},"
        "\"contents\":[{\"parts\":["
        "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";

    String jsonPost =
        "\"}},{\"text\":\"Analyze this frame.\"}]}],\"generationConfig\":{\"temperature\":0.2,\"responseMimeType\":\"application/json\",\"responseSchema\":" + String(schemaJson) + "}}";

    size_t b64Len = ((fb->len + 2) / 3) * 4;
    size_t totalBodyLen = jsonPre.length() + b64Len + jsonPost.length();

    for (int attempt = 0; attempt < NUM_MODELS; attempt++) {
        const char* modelName = GEMINI_MODELS[currentModelIdx];
        Serial.printf("[Gemini] Querying %s (payload %u bytes, frame %u bytes)...\n",
                      modelName, totalBodyLen, fb->len);

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(25);

        unsigned long aiStart = millis();

        if (!client.connect(GEMINI_HOST, 443)) {
            Serial.println("[Gemini] TLS connect failed");
            currentModelIdx = (currentModelIdx + 1) % NUM_MODELS;
            delay(500);
            continue;
        }

        // Send HTTP request headers
        client.print("POST /v1beta/models/" + String(modelName) + ":generateContent HTTP/1.1\r\n");
        client.print("Host: " + String(GEMINI_HOST) + "\r\n");
        client.print("x-goog-api-key: " + String(GEMINI_API_KEY) + "\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print("User-Agent: ESP32-CAM\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Length: " + String(totalBodyLen) + "\r\n\r\n");

        // Send JSON prefix
        client.print(jsonPre);

        // Stream Base64 image directly in 768-byte chunks (produces 1024 base64 chars)
        const size_t RAW_CHUNK = 768;
        uint8_t b64Chunk[1025];
        size_t offset = 0;
        while (offset < fb->len) {
            size_t chunk = (fb->len - offset < RAW_CHUNK) ? (fb->len - offset) : RAW_CHUNK;
            size_t outLen = 0;
            mbedtls_base64_encode(b64Chunk, sizeof(b64Chunk), &outLen, fb->buf + offset, chunk);
            client.write(b64Chunk, outLen);
            offset += chunk;
        }

        // Send JSON suffix
        client.print(jsonPost);
        client.flush();

        // Read HTTP status line
        int httpCode = 0;
        bool isChunked = false;
        unsigned long t0 = millis();
        while (client.connected() && (millis() - t0 < 25000)) {
            String line = client.readStringUntil('\n');
            if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
                httpCode = line.substring(9, 12).toInt();
                break;
            }
        }

        if (httpCode != 200) {
            Serial.printf("[Gemini] %s HTTP error: %d\n", modelName, httpCode);
            client.stop();
            if (httpCode == 429) {
                lastQuotaExceededMs = millis();
                g_metrics.lastAiLatencyMs = millis() - aiStart;
                Serial.println("[Gemini] Quota exceeded (HTTP 429) — aborting fallback cascade to prevent latency hang.");
                break; // Fast fail: all models in project share same quota, do NOT cascade!
            } else if (httpCode == 404 || httpCode == 503) {
                currentModelIdx = (currentModelIdx + 1) % NUM_MODELS;
                Serial.printf("[Gemini] Cascading to fallback model: %s\n", GEMINI_MODELS[currentModelIdx]);
                delay(200);
            } else {
                break;
            }
            continue;
        }

        // Read remainder of headers and check for Transfer-Encoding
        while (client.connected() && (millis() - t0 < 25000)) {
            String line = client.readStringUntil('\n');
            String lower = line;
            lower.toLowerCase();
            if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
                isChunked = true;
            }
            if (line == "\r" || line.length() == 0) {
                break; // End of headers
            }
        }

        // Read response body
        String response = "";
        if (!isChunked) {
            while ((client.connected() || client.available()) && (millis() - t0 < 25000)) {
                while (client.available()) {
                    response += (char)client.read();
                }
                delay(5);
            }
        } else {
            // Read chunked response
            while ((client.connected() || client.available()) && (millis() - t0 < 25000)) {
                String lenLine = client.readStringUntil('\n');
                lenLine.trim();
                if (lenLine.length() == 0) continue;
                long chunkSize = strtol(lenLine.c_str(), NULL, 16);
                if (chunkSize <= 0) break;

                while (chunkSize > 0 && (client.connected() || client.available()) && (millis() - t0 < 25000)) {
                    if (client.available()) {
                        response += (char)client.read();
                        chunkSize--;
                    } else {
                        delay(2);
                    }
                }
                client.readStringUntil('\n'); // Consume trailing \r\n
            }
        }

        client.stop();
        g_metrics.lastAiLatencyMs = millis() - aiStart;
        g_metrics.activeModel = modelName;
        Serial.printf("[Gemini] %s responded 200 OK (%u bytes, latency: %ums)\n",
                      modelName, response.length(), g_metrics.lastAiLatencyMs);
        return response;
    }

    return "";
}

String callGeminiAPI(const String& base64Image) {
    if (base64Image.isEmpty()) return "";

    String jsonPre =
        "{\"system_instruction\":{\"parts\":[{\"text\":\"" + String(systemPrompt) + "\"}]},"
        "\"contents\":[{\"parts\":["
        "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";

    String jsonPost =
        "\"}},{\"text\":\"Analyze this frame.\"}]}],\"generationConfig\":{\"temperature\":0.2,\"responseMimeType\":\"application/json\",\"responseSchema\":" + String(schemaJson) + "}}";

    size_t totalBodyLen = jsonPre.length() + base64Image.length() + jsonPost.length();

    for (int attempt = 0; attempt < NUM_MODELS; attempt++) {
        const char* modelName = GEMINI_MODELS[currentModelIdx];
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(25);

        if (!client.connect(GEMINI_HOST, 443)) {
            currentModelIdx = (currentModelIdx + 1) % NUM_MODELS;
            delay(500);
            continue;
        }

        client.print("POST /v1beta/models/" + String(modelName) + ":generateContent HTTP/1.1\r\n");
        client.print("Host: " + String(GEMINI_HOST) + "\r\n");
        client.print("x-goog-api-key: " + String(GEMINI_API_KEY) + "\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print("User-Agent: ESP32-CAM\r\n");
        client.print("Connection: close\r\n");
        client.print("Content-Length: " + String(totalBodyLen) + "\r\n\r\n");

        client.print(jsonPre);
        client.print(base64Image);
        client.print(jsonPost);
        client.flush();

        int httpCode = 0;
        bool isChunked = false;
        unsigned long t0 = millis();
        while (client.connected() && (millis() - t0 < 25000)) {
            String line = client.readStringUntil('\n');
            if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
                httpCode = line.substring(9, 12).toInt();
                break;
            }
        }

        if (httpCode != 200) {
            client.stop();
            if (httpCode == 429) {
                lastQuotaExceededMs = millis();
                break;
            } else if (httpCode == 404 || httpCode == 503) {
                currentModelIdx = (currentModelIdx + 1) % NUM_MODELS;
                delay(200);
            } else {
                break;
            }
            continue;
        }

        while (client.connected() && (millis() - t0 < 25000)) {
            String line = client.readStringUntil('\n');
            String lower = line;
            lower.toLowerCase();
            if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) isChunked = true;
            if (line == "\r" || line.length() == 0) break;
        }

        String response = "";
        if (!isChunked) {
            while ((client.connected() || client.available()) && (millis() - t0 < 25000)) {
                while (client.available()) response += (char)client.read();
                delay(5);
            }
        } else {
            while ((client.connected() || client.available()) && (millis() - t0 < 25000)) {
                String lenLine = client.readStringUntil('\n');
                lenLine.trim();
                if (lenLine.length() == 0) continue;
                long chunkSize = strtol(lenLine.c_str(), NULL, 16);
                if (chunkSize <= 0) break;
                while (chunkSize > 0 && (client.connected() || client.available()) && (millis() - t0 < 25000)) {
                    if (client.available()) {
                        response += (char)client.read();
                        chunkSize--;
                    } else delay(2);
                }
                client.readStringUntil('\n');
            }
        }
        client.stop();
        return response;
    }
    return "";
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

    if (g_metrics.lastAiLatencyMs > 0) {
        caption += "\n⏱️ *Latency:* " + String((float)g_metrics.lastAiLatencyMs / 1000.0f, 1) + "s (AI)";
        if (g_metrics.lastUploadLatencyMs > 0) {
            caption += " + " + String((float)g_metrics.lastUploadLatencyMs / 1000.0f, 1) + "s (Upload)";
        }
        if (g_metrics.lastTriggerDelta > 0.0f) {
            caption += "  •  Δ " + String(g_metrics.lastTriggerDelta, 1) + "%";
        }
    }

    // Keep well within Telegram's 1024-char photo caption limit
    if (caption.length() > 1000) {
        caption = caption.substring(0, 995) + "...";
    }

    return caption;
}

