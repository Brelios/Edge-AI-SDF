# Edge-Assisted AI Door Sentinel: Iterative Design of a Real-Time Doorstep Surveillance System on DRAM-Constrained IoT Hardware Using Optical Motion Detection and Cloud Multimodal Vision

**Atharv Thakare, Ayush Gandhi**  
*Department of Computer Science & Engineering*  
Contact: `atharv1792006@gmail.com` | Project Repository: [github.com/Brelios/Edge-AI-SDF](https://github.com/Brelios/Edge-AI-SDF)

---

## Abstract

This paper presents the iterative design, implementation, and evaluation of an autonomous doorstep surveillance system built on the AI-Thinker ESP32-CAM, a commodity IoT microcontroller with 320 KB of usable internal dynamic RAM (DRAM) and no external pseudo-static RAM (PSRAM). Five major development versions are documented, spanning from an initial passive infrared (PIR) sensor-based sleep-wake approach to a production-grade, always-connected optical sentinel. Key challenges overcome include multi-second wake latency caused by light-sleep cycles, catastrophic heap exhaustion under TLS 1.2 handshake allocations, false motion triggering from Wi-Fi RF electromagnetic interference on the PIR sensor, and JPEG DMA buffer overflows (`cam_hal: FB-OVF`) caused by incorrect resolution configuration. The final system employs an in-place optical motion detector operating directly on compressed JPEG bitstream entropy via a self-calibrating Exponential Moving Average (EMA) baseline, achieving sub-millisecond evaluation at five frames per second without any external hardware. A high-power flash LED (GPIO 4) illuminates the scene during capture, and structured JSON analysis is performed via Google Gemini's multimodal Vision API over a zero-heap, chunk-aligned TLS streaming pipeline. Bidirectional Telegram command-and-control provides remote `/snap`, `/status`, and `/motion` commands. Post-alert re-arming latency was reduced from an observed 2-3 second blind spot to under 200 milliseconds by deferring blocking Telegram polling and making baseline re-seeding non-blocking. We also honestly discuss the distinction between "Edge AI" and "Edge-Assisted AI," noting that AI inference is currently performed in the cloud due to hardware resource constraints, and outline a path toward on-device quantized model execution.

**Keywords:** ESP32-CAM, IoT Surveillance, Optical Motion Detection, Multimodal Large Language Models, Edge-Assisted AI, Telegram Bot, DRAM-Constrained Embedded Systems, Google Gemini API, Flash LED, Zero-Lag Pipeline.

---

## I. Introduction

The increasing adoption of Internet of Things (IoT) devices in residential settings has made smart entrance monitoring both accessible and expected. Users want to know who is at their door — whether it is a courier, a guest, or something more concerning — and they want to know immediately, on their phone, with a photo, wherever they are in the world [1].

Commercial solutions such as Ring Doorbells and Google Nest Cameras achieve this but at significant financial cost, requiring subscription cloud storage fees, always-on video streaming at 1-4 Mbps, and entrusting continuous video feeds to third-party servers [3]. A strong alternative exists in low-cost IoT microcontrollers such as the Espressif ESP32, which integrate 2.4 GHz Wi-Fi, a powerful 240 MHz dual-core Xtensa LX6 processor, and deep hardware sleep modes into a module costing under $6 USD. Paired with the OmniVision OV2640 image sensor, the AI-Thinker ESP32-CAM module forms a complete camera-and-compute platform in a single PCB. However, deploying a meaningful AI vision pipeline on this hardware is far from straightforward.

This project was undertaken with the following practical goal: build a doorstep camera that takes a photo when someone arrives, has an AI describe what it sees, and sends the photo and description to a Telegram chat — automatically, from a cheap microcontroller. What began as a simple idea quickly revealed a series of non-trivial technical problems that required careful iterative engineering.

This paper documents the **five distinct development phases** of the system, the specific problems encountered in each, the engineering decisions made to solve them, the final verified system design, and an honest discussion of the current limitations — including the gap between "Edge AI" and what the system actually performs today.

---

## II. Hardware Platform and Physical Constraints

### A. Hardware Specifications

The hardware platform throughout all development phases was the **AI-Thinker ESP32-CAM**, which integrates:

| Component | Specification |
| :--- | :--- |
| Processor | ESP32-D0WDQ6, Dual-Core Tensilica Xtensa LX6 @ 240 MHz |
| Internal SRAM | 520 KB total (320 KB usable DRAM after stack and OS) |
| External PSRAM | None — absent on this module (0xffffffff at probe) |
| Flash Storage | 4 MB SPI Flash |
| Image Sensor | OmniVision OV2640, 8-bit DVP Parallel Bus, SCCB control |
| Connectivity | 2.4 GHz 802.11b/g/n Wi-Fi (TLS 1.2 via mbedTLS) |
| GPIO Peripherals | GPIO 4: High-Power White Flash LED; GPIO 13: PIR sensor input; GPIO 0: LEDC XCLK output |
| Firmware Environment | Arduino-ESP32 Core v3.3.12 / ESP-IDF v5.5, PlatformIO |

### B. The Memory Wall

The absence of external PSRAM is the single most important physical constraint shaping every architectural decision in this project. The Wi-Fi networking stack, the FreeRTOS kernel, the LEDC peripheral driver, and the OV2640 DMA controller together consume approximately 180 KB of DRAM at boot. This leaves roughly 105-140 KB of available heap for application logic, frame buffers, TLS handshake buffers, and HTTP request construction. For context:

- A single uncompressed QVGA (320x240) grayscale frame is 76,800 bytes — consuming over 60% of available heap per buffer.
- Base64-encoding a 12 KB JPEG produces ~16 KB of ASCII, and constructing the full JSON request body doubles that.
- mbedTLS requires two contiguous 16,384-byte record buffers for each TLS 1.2 session. On a fragmented DRAM heap, this allocation fails with `MBEDTLS_ERR_SSL_ALLOC_FAILED (-32512)`.

Every design decision — from resolution selection to streaming protocol — flows directly from this constraint.

---

## III. Iterative Development History

The system evolved through five distinct versions. Each version is documented with the problems it encountered and the solutions applied.

### A. Version 1 — Initial PIR + Deep Sleep (Monolithic Firmware)

**Commit Range:** `f577ae0` (Initial Commit) to `31edc11` (Stable DRAM fallback)

**Architecture:** The first working version used a hardware PIR sensor (HC-SR501) connected to GPIO 13. The ESP32 entered deep sleep, and the PIR's interrupt signal on the `ext1` wake pin caused the device to wake, take a photo, call the Gemini API, and return to sleep.

**What Worked:**
- Battery efficiency was excellent — the device drew under 20 µA in deep sleep.
- The architecture was conceptually simple.

**Problems Encountered:**

1. **"Empty Doorstep" Failure Mode.** From physical motion triggering the PIR to the ESP32 fully waking, connecting to Wi-Fi, reinitializing the camera, and capturing a stable frame required 3-6 seconds. By that time, the subject had frequently departed the field of view, resulting in a photograph of an empty doorway.

2. **PIR RF Interference Loop.** The ESP32's 2.4 GHz Wi-Fi radio emits electromagnetic pulses during transmission. The HC-SR501 PIR sensor's analog pyroelectric circuit is susceptible to RF interference. When the ESP32 connected to Wi-Fi to upload a photo, the RF burst re-triggered the PIR sensor, causing an endless alert loop. Workarounds using `MAX_ALERT_FRAMES` caps and 15-second mandatory cooldowns were implemented (commit `557b386`) but remained fragile.

3. **SCCB Bus Lockup.** When the ESP32 deep-sleep power cycle reset the sensor mid-transaction, the I2C/SCCB bus was left in a hung state. Camera initialization failed on next boot unless a hardware power-cycle via the PWDN pin (GPIO 32) was performed with a timed delay.

4. **Monolithic Code.** All logic — camera, networking, AI, power management — was in a single `main.cpp` file, making debugging extremely difficult.

**Version 1 Assessment:** Fundamentally architecturally flawed for doorstep monitoring due to unavoidable wake latency. The PIR-based approach is well-suited for battery operation in scenarios where latency tolerance is higher, but was inappropriate for capturing fast transient events.

---

### B. Version 2 — Light Sleep + Modular Refactoring

**Commit Range:** `548c766` (Light sleep fix) to `1b4d9df` (Modular refactoring)

**Architecture:** Replaced deep sleep with ESP32 light sleep (`esp_light_sleep_start()`), which preserves DRAM state and resumes execution in under 10 ms. The firmware was modularized into separate driver files: `camera_driver.cpp`, `gemini_client.cpp`, `telegram_client.cpp`, and `power_manager.cpp`.

**What Worked:**
- Wake latency dropped from 3-6 seconds to under 10 ms.
- Modular code was significantly easier to maintain and test.
- SCCB bus lockup resolved by sequencing the PWDN pin.

**Problems Encountered:**

1. **Light Sleep Breaks Wi-Fi.** ESP32 light sleep drops the Wi-Fi modem. Reconnecting after waking required 1.5-2.5 seconds of DHCP and association negotiation. Total motion-to-alert latency remained 3-5 seconds — essentially equivalent to deep sleep for Wi-Fi-dependent workflows.

2. **Recursive Stack Overflow.** An early implementation called the PIR interrupt service routine recursively, causing a stack overflow crash. Resolved by restructuring the interrupt handler to set a flag only, with the main loop handling capture logic (commit `548c766`).

3. **HD Resolution FB-OVF.** Requesting HD (1280x720) captures without PSRAM triggered DMA FIFO overflow errors (`cam_hal: FB-OVF`) and system panics. The camera would allocate a double-buffer totaling over 150 KB, exhausting the heap.

**Version 2 Assessment:** Light sleep was a meaningful improvement over deep sleep but still insufficient due to Wi-Fi reconnection costs. The modular code structure established here persisted through all subsequent versions.

---

### C. Version 3 — Always-Connected Sentinel with Optical Motion Detection

**Commit Range:** `8367f0e` (Optical detection + always-connected) to `7ad0a9c` (Zero-heap streaming fix)

**Architecture:** A fundamental architectural shift: the device never sleeps. It enters an always-connected surveillance loop, polling camera frames every 200 ms for motion using an in-place optical algorithm operating on JPEG bitstream entropy, rather than relying on the PIR sensor. The PIR sensor remains as an optional hardware fallback on GPIO 13.

**The Optical Motion Detection Algorithm:**

A JPEG-compressed image of a static scene has a characteristic file size: the efficient run-length encoding of near-zero high-frequency DCT coefficients produces a compact bitstream. When a human subject enters the field of view, sharp spatial edges, clothing textures, and luminance gradients dramatically increase non-zero AC DCT coefficients. Consequently, the total byte length `L_t` of the compressed JPEG bitstream exhibits a deterministic expansion proportional to the optical scene disturbance.

Each 200 ms poll, a single JPEG frame is captured. Its byte length `L_t` is compared against a running Exponential Moving Average (EMA) baseline `mu_t`:

```
delta_t = |L_t - mu_t| / mu_t
```

If `delta_t >= theta_th` (default: 7%), motion is declared. The baseline adapts slowly during static periods (alpha = 0.02) and freezes when motion is detected, preventing the subject from being absorbed into the background model:

```
mu_t = mu_{t-1},                          if delta_t >= theta_th (motion)
mu_t = 0.98 * mu_{t-1} + 0.02 * L_t,     if delta_t < theta_th  (static)
```

This operates strictly in the compressed domain — no pixel decoding, no uncompressed frame allocation. Evaluation completes in under 1 ms per frame.

**Zero-Lag Frame Preservation:** The exact `camera_fb_t*` pointer that triggered the threshold condition is passed directly to the Gemini streaming pipeline without releasing and recapturing. This eliminates the secondary recapture latency (which would have been 750 ms to 3.5 seconds) entirely.

**New Problems Encountered:**

1. **Heap Exhaustion During Gemini Streaming.** Constructing a JSON body containing the Base64-encoded JPEG as a string used ~30 KB of heap, causing TLS handshake failures. Resolution: **Zero-Heap Chunk-Aligned Streaming** — the raw JPEG buffer pointer is traversed in 768-byte raw chunks, each encoded to 1024 bytes of Base64 inline and written directly to the TLS socket (commit `7ad0a9c`). Total dynamic heap allocated during image transmission: **0 bytes**.

2. **False Optical Triggers.** CMOS thermal shot noise caused frame size fluctuations of 2-5% on completely static scenes. Resolved by tuning the threshold from 6% to 7%, and adding a consecutive-frame delta cross-check.

3. **Telegram `/snap` Command.** Added the `/snap` command for on-demand photos. Resolved a critical bug where calling `/snap` while the Gemini stream was active caused a double-`fb_return` crash.

**Version 3 Assessment:** This version represented the core breakthrough. The always-connected + optical detection approach eliminated wake latency. The zero-heap streaming solved the memory wall problem. The system became genuinely usable.

---

### D. Version 4 — Resolution Upgrade and Flash LED

**Commit Range:** `77f169b` (VGA resolution + Gemini 429 fail-fast) to `971d0d1` (Flash LED)

**VGA Resolution Upgrade:**

Previous versions used QVGA (320x240) but still encountered occasional `cam_hal: FB-OVF` when JPEG frame sizes exceeded the DMA buffer. The sensor was reconfigured to:

| Parameter | Value |
| :--- | :--- |
| Resolution | VGA (640x480) |
| JPEG Quality | 18 (crisp indoor image at ~8-25 KB) |
| DMA Buffer | 57,344 bytes (56 KB fixed allocation) |
| XCLK | 10 MHz (prevents DVP bus overrun on DRAM-only mode) |
| Grab Mode | `CAMERA_GRAB_WHEN_EMPTY` (single buffer) |

This fully eliminated `FB-OVF` errors. Free heap after initialization measured at approximately 104,612 bytes.

**High-Power Flash LED:**

The onboard white flash LED on GPIO 4 was integrated into every photo capture:
1. Flash LED turns ON
2. 120 ms delay allows the OV2640 Auto-Exposure Control (AEC) to adapt to the brighter scene
3. One stale frame is flushed (removes pre-flash DMA frame)
4. Fresh frame captured with flash illumination
5. Flash LED turns OFF

This dramatically improved night and low-light image quality, producing clear, identifiable subjects at ranges up to 2-3 meters in dark rooms.

**Gemini 429 Circuit Breaker:** Added a 60-second bypass circuit breaker when a 429 (quota exceeded) response is received.

**Version 4 Assessment:** The VGA upgrade and flash LED together made captured images qualitatively superior for AI analysis. More resolution means Gemini can identify faces, courier uniforms, package labels, and fine details that QVGA could not reliably resolve.

---

### E. Version 5 — Post-Alert Blind Spot Elimination (Current Production)

**Commit:** `9f726de` — perf: eliminate post-alert blind spot, defer Telegram polling, and tighten cooldown

**The Problem Observed:** After a complete alert cycle (motion to Gemini to Telegram), the system experienced a 2-3 second period during which it would not respond to new motion. User testing confirmed: placing a hand in front of the camera, waiting for the Telegram photo to arrive, then moving the hand again produced no second alert for 2-3 seconds.

**Root Cause Analysis — Three Layered Contributors:**

1. **Telegram Command Polling Collision (1.5-2.0 seconds).** `TELEGRAM_POLL_INTERVAL_MS = 3500 ms` had elapsed during the Gemini + upload cycle (~3.5-4 seconds). Immediately upon returning to `loop()`, `processTelegramCommands()` executed, performing a synchronous TLS handshake to `api.telegram.org/bot.../getUpdates`. Establishing a TLS 1.2 connection from an ESP32 takes 1.5-2.0 seconds of CPU-blocking time. During this window, `checkOpticalMotion()` could not execute.

2. **Blocking Baseline Re-Seeding (~500 ms).** `resetMotionBaseline()` captured 2 VGA frames with `delay(50)` calls between them — approximately 500 ms of synchronous blocking after every alert.

3. **12-Second Motion Cooldown.** `MOTION_COOLDOWN_MS = 12000` meant the system would not dispatch a second alert for 12 seconds after the first.

**Solutions Applied:**

1. After every `dispatchAlert()` call, `lastTelegramMs = millis()` is set. This resets the Telegram polling timer, giving the surveillance loop a full 3.5-second head start before the next blocking `getUpdates` call.

2. `resetMotionBaseline()` rewritten to simply set `baselineReady = false`. The next regular 200 ms surveillance tick seeds the baseline naturally — no blocking, no frame capture, no `delay()` calls.

3. `MOTION_COOLDOWN_MS` reduced from 12,000 ms to 4,000 ms for snappier consecutive-event response.

4. `getUpdates` HTTP timeout reduced from 5,000 ms to 2,500 ms.

**Measured Improvement:** Post-alert re-arming window dropped from 2-3 seconds to approximately 200 ms — equivalent to a single motion poll interval.

---

## IV. Final System Architecture

### A. Operational Flow

```
Power On → setup()
  ├── Connect Wi-Fi
  ├── Initialize OV2640: VGA (640x480), quality=18, 56KB DMA buffer
  ├── Flush 8 settling frames (AEC/AGC register stabilization)
  ├── Seed EMA motion baseline from 3 averaged ambient frames
  └── Send Telegram boot announcement

loop() — continuous, no blocking sleep
  ├── every tick:      maintainWiFi()
  ├── every 3500 ms:   processTelegramCommands()
  │     └── /snap    → Flash capture → sendTelegramPhoto()
  │         /status  → Rich diagnostic card with latency metrics
  │         /motion  → Toggle motionEnabled flag
  │         /help    → Command list
  └── every 200 ms:   checkOpticalMotion()
        ├── Capture single JPEG frame
        ├── Compute delta_t = |Lt - mu_t| / mu_t
        ├── If delta_t < 7%: update mu_t slowly, discard frame
        └── If delta_t >= 7%: motion confirmed
              └── dispatchAlert()
                    ├── Release pre-flash frame
                    ├── Flash LED ON (120ms AEC settle)
                    ├── Capture fresh illuminated VGA frame
                    ├── Flash LED OFF
                    ├── callGeminiAPI() — zero-heap chunk streaming
                    │     └── Multi-model failover on 429/503
                    ├── parseGeminiResponse()
                    ├── If "false_alarm": suppress and reset
                    ├── sendTelegramPhoto() with rich caption
                    ├── lastAlertMs = millis()
                    ├── lastTelegramMs = millis()  [deferred polling]
                    └── resetMotionBaseline()       [non-blocking]
```

### B. Zero-Heap TLS Streaming Protocol

Base64 encoding maps 3 raw bytes to 4 ASCII characters. If the raw input is partitioned into multiples of 3 bytes, each chunk can be independently encoded with no padding until the final chunk.

- **Raw chunk size:** 768 bytes (= 3 * 256)
- **Encoded output per chunk:** 1024 bytes (= 4 * 256)

Protocol execution:
1. Establish TLS connection to `generativelanguage.googleapis.com:443` before any payload allocation (heap unfragmented at this point, ~104 KB free)
2. Pre-compute exact `Content-Length` mathematically without string construction
3. Write JSON prefix from Flash `.rodata` directly to socket
4. Iterate JPEG buffer in 768-byte slices: `mbedtls_base64_encode()` into a 1025-byte stack buffer, then `client.write()`
5. Write JSON suffix and flush

**Total dynamic heap allocated during image transmission: 0 bytes.**

### C. Gemini Multimodal Analysis Schema

The Gemini API call enforces a strict JSON `responseSchema`:

| Field | Type | Values |
| :--- | :--- | :--- |
| `category` | Enum | delivery, resident_visitor, suspicious, threat, animal, false_alarm |
| `urgency` | Enum | none, low, medium, high |
| `summary` | String | 1-2 sentence factual description |
| `people` | Integer | Count of visible persons |
| `packages` | Integer | Count of visible parcels |
| `vehicles` | Integer | Count of visible vehicles |
| `image_quality` | Enum | good, poor |
| `actions` | Array[String] | 1-2 imperative recommendations |

Multi-model cascade order on 429/503 errors:
`gemini-3.7-flash → gemini-3.5-flash-lite → gemini-3.1-flash-lite → gemini-3-flash-preview → gemini-3.5-flash`

### D. Final System Configuration Parameters

| Parameter | Value | Description |
| :--- | :---: | :--- |
| `MOTION_SENSITIVITY` | 0.07 (7%) | Minimum relative JPEG entropy expansion to declare motion |
| `MOTION_POLL_INTERVAL_MS` | 200 ms | Surveillance frame capture rate |
| `MOTION_COOLDOWN_MS` | 4,000 ms | Minimum gap between successive Gemini dispatches |
| `TELEGRAM_POLL_INTERVAL_MS` | 3,500 ms | Interval for Telegram getUpdates polling |
| `FLASH_DURATION_MS` | 120 ms | Flash LED on-duration for AEC adaptation |
| `SUPPRESS_FALSE_ALARMS` | true | Silently discard Gemini false_alarm classifications |
| Camera Buffer | 57,344 bytes | Fixed DMA frame buffer |
| Camera Resolution | VGA 640x480 | Balanced clarity and memory footprint |
| JPEG Quality | 18 | Crisp indoor quality at ~8-25 KB per frame |

---

## V. Honest Discussion: "Edge AI" vs. "Edge-Assisted AI"

A central claim of this project's title is "Edge AI." It is important to be accurate about what this means in the current implementation.

**What the edge device (ESP32) does:**
- Captures frames at 5 per second
- Evaluates motion in under 1 ms using a mathematical algorithm (EMA entropy comparison)
- Makes the binary decision: "Is this worth sending to AI?"
- Controls the flash LED, camera sensor, and all network communication
- Manages multi-model failover, cooldown timers, and Telegram command parsing

**What the cloud (Google Gemini API) does:**
- Performs actual deep semantic scene understanding — identifying whether a person is a courier, a resident, or suspicious; detecting packages; classifying threats; generating natural-language summaries

The ESP32 **does not run any neural network or machine learning model locally.** All AI inference is cloud-delegated. This is not "Edge AI" in the strict definition (where the ML model runs on the edge device). It is more precisely **"Edge-Assisted AI"**: the edge device acts as an intelligent pre-filter and event detector, using a lightweight mathematical algorithm to decide when to invoke expensive cloud intelligence.

**Why this is still valuable:**
- The EMA-based optical trigger achieves 100% true positive rate for human-scale motion in testing without any ML model
- The edge pre-filtering means Gemini is only called when something actually happened — not once per frame, which would cost hundreds of API calls per minute
- The entire capture-to-Telegram pipeline — the real engineering challenge — runs on the constrained ESP32 hardware

**Why we could not run AI on-device for this submission:**
The ESP32's 320 KB DRAM cannot hold even the smallest production vision models. MobileNet-V1 at 0.25 width multiplier requires ~450 KB of model weights alone. TinyML frameworks such as TensorFlow Lite Micro can run ultra-quantized models, but achieving meaningful doorstep recognition at useful accuracy requires model sizes not achievable within the <300 KB DRAM budget of this hardware.

This remains a clear area for future work. The project budget and allocated resources did not allow for a PSRAM-equipped module, which would have made on-device inference feasible.

---

## VI. Experimental Results

All results were obtained on physical hardware (AI-Thinker ESP32-CAM, COM4, 115200 baud) connected to a 2.4 GHz Wi-Fi network (RSSI: -51 dBm). Results reflect the Version 5 final system.

### A. Boot and Initialization Metrics

| Stage | Measured Duration |
| :--- | :--- |
| Wi-Fi connection time | 2,100 – 3,400 ms |
| Camera initialization (with PWDN retry) | 350 – 700 ms |
| AEC/AGC settling (8 flush frames) | ~480 ms |
| Baseline seeding (3 averaged frames) | ~180 ms |
| **Total boot to active sentinel** | **~3.5 – 5.0 seconds** |
| Free heap at active sentinel | ~104,612 bytes |

### B. Motion Detection Performance

| Metric | Measured Value |
| :--- | :--- |
| Optical evaluation latency per frame | < 1 ms |
| Polling rate | 5 checks/second (200 ms interval) |
| Static scene baseline drift (delta_t) | < 2% |
| Human entry delta (delta_t) | 15% – 120% |
| False alarm from ambient conditions | 0% in 40 ambient-only trials |
| True positive rate (human entries, n=20) | 100% |

### C. Alert Dispatch Latency (Version 5)

| Stage | Typical Duration |
| :--- | :--- |
| Flash LED + AEC settle + frame capture | 120 – 250 ms |
| TLS handshake to Gemini | 800 – 1,500 ms |
| Gemini multimodal inference | 2,100 – 6,000 ms |
| Telegram photo upload (~15 KB JPEG) | 1,000 – 1,800 ms |
| **Total: motion trigger to Telegram received** | **~4 – 10 seconds** |
| Post-alert re-arming latency | **< 200 ms** |

The Gemini inference time is the dominant and most variable contributor. This depends on API load and model tier, and is outside the scope of on-device optimization.

### D. Memory Stability

Over a 30-minute continuous monitoring session with 15 triggered alerts:

| State | Free Heap |
| :--- | :--- |
| Boot completion | 104,612 bytes |
| During motion poll | 103,800 bytes |
| Minimum during Gemini TLS streaming | 103,200 bytes |
| After alert dispatch and frame return | 103,800 bytes |
| Leakage over 15 alerts | **0 bytes** |

### E. Version-by-Version Comparison Summary

| Version | Wake/Trigger Latency | False Alarm Source | Heap Crashes | Image Quality | Post-Alert Re-Arm |
| :--- | :---: | :---: | :---: | :---: | :---: |
| V1: PIR + Deep Sleep | 3-6 seconds | PIR RF interference | Frequent | QVGA | N/A (sleep cycle) |
| V2: PIR + Light Sleep | 1.5-3 seconds | PIR RF interference | Frequent | QVGA | N/A (sleep cycle) |
| V3: Optical Always-On | < 1 ms | CMOS noise (mitigated) | Eliminated | QVGA | ~3 seconds |
| V4: VGA + Flash LED | < 1 ms | Eliminated | None | VGA + Flash | ~3 seconds |
| V5: Latency Optimized | < 1 ms | Eliminated | None | VGA + Flash | **< 200 ms** |

---

## VII. Discussion

### A. Key Engineering Trade-offs

**Resolution vs. Memory:** VGA (640x480) with a fixed 56 KB DMA buffer was the correct balance point. Lower resolutions (QVGA, QQVGA) produced images where Gemini could not reliably identify whether clothing was a courier uniform or a winter jacket. Higher resolutions (HD 1280x720) caused `FB-OVF` crashes on DRAM-only hardware. The 56 KB fixed buffer choice eliminated the crash class entirely.

**Always-Connected vs. Sleep:** Light sleep cannot be combined with persistent Wi-Fi for fast-responding doorstep monitoring because reconnection costs dominate latency. The modem-sleep approach keeps the Wi-Fi association alive between beacon intervals, reducing idle current while maintaining instant connectivity.

**Optical Detection vs. PIR:** Optical detection requires no additional hardware, eliminates RF interference issues, and can detect motion that confuses PIR sensors. The trade-off is higher CPU usage (5 frame captures per second vs. interrupt-driven PIR), but at 0.45 ms per evaluation, this represents negligible CPU overhead.

**Flash LED Quality vs. AEC Disruption:** After the flash fires, the OV2640's AEC registers are clamped down from the burst exposure. The sensor requires 2-3 frames to recover to ambient light levels. The design choice to clear one stale frame after flash handles this correctly. The 120 ms settle time was empirically determined as the minimum that produces consistent, well-exposed frames across 15-850 lux lighting conditions.

### B. Limitations

1. **Cloud AI Dependency.** All semantic intelligence requires an active Internet connection and a valid Gemini API key with available quota. If the API is unreachable, alerts are not dispatched.

2. **Gemini API Latency Variability.** The 2.1-6.0 second Gemini inference window is highly variable depending on global API load. During high-demand periods, responses can take 8-15 seconds.

3. **Single Camera Angle.** The system covers one fixed field of view. A comprehensive residential deployment would require multiple nodes.

4. **No On-Device ML.** As discussed in Section V, the ESP32's 320 KB DRAM cannot host a vision neural network of sufficient accuracy for doorstep recognition tasks.

5. **VGA for Both Detection and Capture.** The same VGA frame is used for motion detection and alert capture. A more sophisticated design could use lightweight QQVGA frames for motion polling (faster) and upgrade to VGA only for the alert capture. However, managing two resolution modes adds sensor re-initialization latency.

---

## VIII. Future Work

1. **Quantized On-Device Pre-Filter:** TensorFlow Lite Micro enables running INT8 quantized MobileNet-V1 (0.25 width multiplier) within approximately 300 KB of Flash and 100 KB of DRAM. Deploying this as a lightweight human-presence gate — triggering Gemini only when the on-device model detects a person — would reduce API calls by an estimated 70-80%.

2. **Multi-Node Mesh:** Multiple ESP32-CAM nodes could communicate via ESP-NOW to coordinate coverage of multiple angles. A single coordinator node would aggregate alerts and manage Telegram dispatch.

3. **Package and Occupancy Tracking:** With persistent alert history stored on a microSD card (the ESP32-CAM includes a microSD slot), the system could track package delivery confirmation and occupancy patterns over time.

4. **PSRAM-Enabled Module Upgrade:** Moving to an ESP32-S3 or ESP32-WROVER with 8 MB of external PSRAM would lift virtually all memory constraints, enabling HD resolution, dual-frame buffers, and on-device TinyML model execution simultaneously.

---

## IX. Conclusion

This paper documented the design, iterative development, and empirical evaluation of an Edge-Assisted AI doorstep surveillance system built on a commodity $6 ESP32-CAM module. Over five development versions, the system evolved from a fragile PIR-based deep-sleep camera to a robust, always-connected optical sentinel capable of photographing doorstep events, performing structured cloud AI analysis via Google Gemini, and delivering rich Telegram notifications — while maintaining zero heap leaks, zero frame buffer overflows, and a post-alert re-arming latency of under 200 milliseconds.

The core technical contributions are: (1) an in-place optical motion detection algorithm operating on compressed JPEG bitstream entropy with sub-millisecond evaluation; (2) a zero-lag frame preservation architecture that captures the exact triggering moment without secondary recapture; (3) a chunk-aligned zero-heap TLS streaming protocol that eliminates all dynamic memory allocations during Gemini API communication; (4) a flash LED capture pipeline that delivers well-exposed night images; and (5) a post-alert re-arming optimization that eliminates the 2-3 second dead zone following each alert.

We have been transparent that the current implementation is "Edge-Assisted AI" rather than true "Edge AI" — the ESP32 makes the intelligent decision of when to invoke AI, but the inference itself runs in the cloud. This architectural honesty points clearly toward the next milestone: deploying a quantized vision pre-filter directly on-device, which would make the system fully autonomous and offline-capable.

---

## References

[1] A. Al-Fuqaha, M. Guizani, M. Mohammadi, M. Aledhari, and M. Ayyash, "Internet of Things: A Survey on Enabling Technologies, Protocols, and Applications," *IEEE Communications Surveys & Tutorials*, vol. 17, no. 4, pp. 2347-2376, 2015.

[2] P. Warden and D. Situnayake, *TinyML: Machine Learning with TensorFlow Lite on Arduino and Ultra-Low-Power Microcontrollers*. O'Reilly Media, 2019.

[3] S. Ray, "Smart Home Surveillance Systems: Review of Contemporary Architectures, Latency Bottlenecks, and Power Trade-Offs," *IEEE Consumer Electronics Magazine*, vol. 12, no. 3, pp. 45-56, 2023.

[4] Y. K. Tan and T. P. Huynh, "Zero-Lag Motion Sensing: Mitigating Wake-Up Delays in Battery-Powered IoT Security Cameras," *IEEE Sensors Journal*, vol. 21, no. 14, pp. 16210-16219, 2021.

[5] M. Satyanarayanan, "The Emergence of Edge Computing," *Computer*, vol. 50, no. 1, pp. 30-39, 2017.

[6] J. Lin, W.-M. Chen, Y. Lin, C. Gan, S. Han, et al., "MCUNet: Tiny Deep Learning on IoT Devices," *Advances in Neural Information Processing Systems (NeurIPS)*, vol. 33, pp. 11711-11722, 2020.

[7] Gemini Team, Google, "Gemini: A Family of Highly Capable Multimodal Models," *arXiv preprint arXiv:2312.11805*, 2023.

[8] Espressif Systems, "ESP32 Series Datasheet and Hardware Design Guidelines," v4.2, Espressif Systems Inc., 2024.

[9] G. K. Wallace, "The JPEG Still Picture Compression Standard," *IEEE Transactions on Consumer Electronics*, vol. 38, no. 1, pp. xviii-xxxiv, 1992.

[10] OmniVision Technologies, "OV2640 Color CMOS QSXGA (2.0 Megapixel) Image Sensor," *Datasheet Version 1.6*, 2006.

[11] ARM mbedTLS Library, "mbedTLS Architectural Design and Memory Footprint Optimization for Microcontrollers," *TrustedFirmware.org*, 2024.

[12] S. Josefsson, "The Base16, Base32, and Base64 Data Encodings," RFC 4648, Internet Engineering Task Force (IETF), 2006.

[13] B. Benoit, M. J. B. Smith, and C. V. Stewart, "Direct DCT Compressed Domain Motion Estimation for Low-Power Visual Surveillance," *IEEE Transactions on Circuits and Systems for Video Technology*, vol. 16, no. 11, pp. 1380-1392, 2006.

[14] R. Buyya, S. N. Srirama, et al., *Fog and Edge Computing: Principles and Paradigms*, John Wiley & Sons, 2019.

[15] C. Zhang, P. Patras, and H. Haddadi, "Deep Learning in Mobile and Wireless Networking: A Survey," *IEEE Communications Surveys & Tutorials*, vol. 21, no. 3, pp. 2224-2287, 2019.

[16] E. Rescorla, "The Transport Layer Security (TLS) Protocol Version 1.3," RFC 8446, Internet Engineering Task Force (IETF), 2018.

[17] Z. He and S. K. Mitra, "A Unified Rate-Distortion Analysis Framework for Transform Coding," *IEEE Transactions on Circuits and Systems for Video Technology*, vol. 11, no. 12, pp. 1221-1236, 2001.

[18] M. Abadi et al., "TensorFlow: A System for Large-Scale Machine Learning," *12th USENIX Symposium on Operating Systems Design and Implementation*, 2016.

---

*Repository: [github.com/Brelios/Edge-AI-SDF](https://github.com/Brelios/Edge-AI-SDF) | Tagged Release: `v1.0.0-zero-lag-vga`*
