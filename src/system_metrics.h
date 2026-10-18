#pragma once
#include <Arduino.h>

struct SystemMetrics {
    // Latencies (ms)
    uint32_t lastAiLatencyMs = 0;
    uint32_t lastUploadLatencyMs = 0;
    uint32_t lastSnapLatencyMs = 0;
    uint32_t lastMotionCheckMs = 0;

    // Operational statistics
    uint32_t motionTriggersCount = 0;
    uint32_t alertsDispatchedCount = 0;
    uint32_t falseAlarmsSuppressed = 0;
    uint32_t snapsServicedCount = 0;

    // Last trigger metadata
    float lastTriggerDelta = 0.0f;
    const char* lastTriggerSource = "None";
    const char* activeModel = "gemini-3.7-flash";
};

extern SystemMetrics g_metrics;
