# Edge AI Smart Door Feed — Comprehensive Engineering Build Plan

> **Hardware:** AI-Thinker ESP32-CAM (OV2640) · **Programmer:** ESP32-CAM-MB Base Shield · **Vision AI:** Google Gemini 3.8 Flash · **Alerts:** Telegram Bot API · **Firmware Release:** v1.0 (Production)

---

## Table of Contents

1. [System Architecture](#1-system-architecture)
2. [Bill of Materials (BOM)](#2-bill-of-materials-bom)
3. [Pinouts & Hardware Allocation](#3-pinouts--hardware-allocation)
4. [Hardware Setup & Wiring](#4-hardware-setup--wiring)
5. [Toolchain & Environment Setup](#5-toolchain--environment-setup)
6. [Firmware Architecture](#6-firmware-architecture)
   - [Sleep & Wake Lifecycle](#61-sleep--wake-lifecycle)
   - [Active Motion Loop & Debounce](#62-active-motion-loop--debounce)
   - [Adaptive Dual-Memory Camera Pipeline](#63-adaptive-dual-memory-camera-pipeline)
   - [Gemini 3.8 Flash Multimodal API](#64-gemini-38-flash-multimodal-api)
   - [Telegram Multipart Dispatch](#65-telegram-multipart-dispatch)
7. [Cloud & Notification Credentials](#7-cloud--notification-credentials)
8. [Flash & Verification Procedure](#8-flash--verification-procedure)
9. [Troubleshooting Reference](#9-troubleshooting-reference)

---

## 1. System Architecture

```text
┌───────────────────────────────── EDGE DEVICE (Door Frame) ──────────────────────────────────┐
│                                                                                              │
│   [ HC-SR501 / AM312 PIR ]                                                                  │
│            │ Signal HIGH (Motion Detected)                                                   │
│            ▼                                                                                 │
│   [ ESP32-CAM (AI-Thinker) ] ── Wake from Sleep (<10ms) ────────┐                            │
│            │                                                    │                            │
│            ├── Sensor Settle (150ms AWB / AGC)                  │ Dynamic Multi-Frame Loop   │
│            ├── Captures 720p HD JPEG                            │ (Re-captures every 4s      │
│            ├── Connects to Wi-Fi                                │  while motion persists)    │
│            └── Encodes Base64                                   ▼                            │
└────────────────────────────────────────────┬─────────────────────────────────────────────────┘
                                             │ HTTPS POST (TLS 1.2 Insecure Client)
                                             ▼
                             ┌───────────────────────────────┐
                             │ Google Gemini 3.8 Flash API   │
                             │ Endpoint: /v1beta/models/...  │
                             │ Body: Base64 JPEG + Prompt    │
                             │ Returns: Structured JSON      │
                             └───────────────┬───────────────┘
                                             │ Structured Response
                                             ▼
                             ┌───────────────────────────────┐
                             │ Telegram Bot API              │
                             │ Endpoint: /sendPhoto          │
                             │ Body: Multipart Form-Data     │
                             │ (720p JPEG + Urgency Caption) │
                             └───────────────┬───────────────┘
                                             │
                                             ▼
                                  📱 User Phone / Alert
```

### Event Cycle Timeline (Light Sleep Mode)
```text
t = 0 ms     PIR sensor triggers HIGH on GPIO 13
t = 8 ms     ESP32 wakes from Light Sleep (<10ms, RAM & camera state preserved)
t = 150 ms   Sensor AGC/AWB registers stabilize, clears stale DMA buffer
t = 300 ms   OV2640 captures fresh 720p HD frame into PSRAM
t = 450 ms   Base64 encoding completed in PSRAM buffer
t = 1200 ms  Wi-Fi connection verified / established
t = 2600 ms  Google Gemini 3.8 Flash POST complete; structured JSON parsed
t = 3400 ms  Telegram photo upload complete with urgency emoji & summary
t = 4000 ms  PIR re-checked:
             ├── If HIGH → Loop continues (next frame captured)
             └── If LOW  → Debounce verification (1500 ms) → Re-arms Light Sleep
```

---

## 2. Bill of Materials (BOM)

| # | Component | Qty | Description & Operating Notes |
| :-: | :--- | :-: | :--- |
| **1** | **ESP32-CAM Board** | 1 | AI-Thinker model with ESP32-S and 4MB external PSRAM. |
| **2** | **OV2640 Camera Sensor** | 1 | 2-Megapixel sensor with DVP ribbon cable. |
| **3** | **PIR Motion Sensor** | 1 | **HC-SR501** (powered from 5V rail; 3.3V TTL out) or **AM312** (3.3V). |
| **4** | **Power Supply** | 1 | 5V DC ≥ 2.0A Wall Adapter with high-gauge Micro-USB cable. |
| **5** | **Decoupling Capacitor** | 1 | **100 µF 16V electrolytic capacitor** across 5V and GND at the board. |
| **6** | **Programmer & Power Base** | 1 | **ESP32-CAM-MB Base Shield** (Micro-USB with CH340, RST & IO0 buttons). |
| **7** | **DuPont Wires** | 3 | Female-to-Female jumpers for PIR sensor connection. |

> 📖 **Full Hardware Specifications:** Consult [docs/COMPONENTS.md](../docs/COMPONENTS.md) for electrical ratings, pinout diagrams, and operating parameters.

---

## 3. Pinouts & Hardware Allocation

```text
                     ┌──────────────────────────────┐
                     │        [ OV2640 Camera ]      │
                     │         (ribbon cable)        │
                     │                              │
               ┌─────┴──────────────────────────────┴─────┐
               │  ○ GND         [ESP32-S module]    3.3V ○ │
               │  ○ 5V                              GND  ○ │
               │  ○ IO12  ← SD card D2              IO1  ○ │← U0TXD (UART TX)
               │  ○ IO13  ← PIR Motion Signal (IO13)IO3  ○ │← U0RXD (UART RX)
               │  ○ IO15  ← SD card CMD             IO0  ○ │← Boot Strapping (GND=Flash)
               │  ○ IO14  ← SD card CLK             GND  ○ │
               │  ○ IO2   ← SD card D0              IO4  ○ │← Flash LED / SD D1
               │  ○ IO16  ← PSRAM clock/CS (Do Not Use)   │
               │                                           │
               │            [MicroSD Card Slot]            │
               └───────────────────────────────────────────┘
```

| Pin | Function | Direction | Description |
| :--- | :--- | :---: | :--- |
| **5V** | Power In | PWR | Connect to 5V DC ≥ 2A supply rail. |
| **GND** | Ground | PWR | Common circuit ground. |
| **IO1** | U0TXD | OUT | Serial transmit to programmer RX (115200 baud). |
| **IO3** | U0RXD | IN | Serial receive from programmer TX. |
| **IO0** | Strapping | IN | Pull to **GND** during power-up to enter flash mode; leave floating for boot. |
| **IO13** | RTC_GPIO14 | IN | **PIR Signal Input.** Internal digital pull-down + RTC pull-down enabled. |
| **IO4** | Flash LED | OUT | Onboard high-power white LED (active HIGH). Kept OFF in firmware. |
| **IO16** | PSRAM CS | SYS | Dedicated to external SPI PSRAM. Never attach external circuitry. |

---

## 4. Hardware Setup & Wiring

The ESP32-CAM plugs directly onto the **ESP32-CAM-MB Base Shield**, providing a single Micro-USB port for programming and 24/7 power.

```text
       ┌────────────────────────────────────────────────────────┐
       │                  HC-SR501 PIR Sensor                   │
       │                                                        │
       │     [ VCC ]              [ OUT ]             [ GND ]   │
       └────────┬────────────────────┬───────────────────┬──────┘
                │ 5V                 │ IO13              │ GND
                │ (Power)            │ (Motion Signal)   │ (Ground)
                ▼                    ▼                   ▼
       ┌────────────────────────────────────────────────────────┐
       │                 ESP32-CAM (AI-Thinker)                 │
       │                                                        │
       │   [ 5V ]                     [ IO13 ]       [ GND ]    │
       │                                                        │
       │            [ OV2640 Lens ]        [ Flash LED ]        │
       ├────────────────────────────────────────────────────────┤
       │                ESP32-CAM-MB Base Shield                │
       │                                                        │
       │      [ RST Button ]                  [ IO0 Button ]    │
       │                                                        │
       │                  [ Micro-USB Port ]                    │
       └───────────────────────────┬────────────────────────────┘
                                   ║
                         Micro-USB Data Cable
                                   ║
                     PC (Flash) / 5V 2A Adapter (Run)
```

### 4.1 PIR Motion Sensor Wiring (Only 3 Wires)

| PIR Pin | ESP32-CAM Pin | Purpose |
| :--- | :--- | :--- |
| **VCC** | **5V** | 5V DC power rail to sensor regulator |
| **GND** | **GND** | Circuit ground return |
| **OUT** | **IO13** | 3.3V logic motion trigger to RTC GPIO 13 |

### 4.2 Flashing & Runtime Workflow

1. **Flashing Firmware:**
   * Seat the ESP32-CAM firmly onto the MB base.
   * Connect the Micro-USB cable to your PC.
   * Execute: `pio run --target upload`. The MB base's automatic DTR/RTS circuitry puts the ESP32 into bootloader mode without manual jumpers.
2. **Runtime Deployment:**
   * Press the **RST** button on the MB base (or plug into any 5V ≥ 2A USB wall charger).
   * The firmware executes immediately and enters **Light Sleep** awaiting PIR triggers.

---

## 5. Toolchain & Environment Setup

### 5.1 PlatformIO Configuration (`platformio.ini`)
The project utilizes the modern Arduino-ESP32 v3.x core with huge-app partitioning:

```ini
[env:esp32cam]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32cam
framework = arduino
monitor_speed = 115200
board_build.partitions = huge_app.csv
monitor_rts = 0
monitor_dtr = 0
upload_speed = 921600

; Exclude legacy web server implementation
build_src_filter = +<*> -<app_httpd.cpp>

lib_deps =
    bblanchon/ArduinoJson
```

---

## 6. Firmware Architecture

### 6.1 Sleep & Wake Lifecycle
The firmware supports two distinct operational modes configurable via `#define USE_LIGHT_SLEEP`:
* **Light Sleep Mode (`USE_LIGHT_SLEEP = true` — Recommended):**
  * Wakes in **< 10 ms**.
  * Dynamic RAM, frame buffer memory, and camera hardware configuration are held active.
  * Idle current drops to **~1.2 mA – 6 mA**.
  * Ideal for wall-adapter or USB-powered door installations.
* **Deep Sleep Mode (`USE_LIGHT_SLEEP = false`):**
  * Wakes in **~600 ms** via chip reset.
  * Current drops to **~20 µA – 2 mA**.
  * Ideal for solar/battery-backed installations.

### 6.2 Active Motion Loop & Debounce
Rather than firing an isolated snapshot and going blind for 30 seconds, the v1.0 firmware loops actively:
1. When PIR on `IO13` goes `HIGH`, `processMotionCycle()` initiates.
2. Captures frame, queries Gemini, and sends Telegram alert.
3. Pauses for the remainder of `PIR_CAPTURE_INTERVAL_MS` (4,000 ms).
4. If PIR is still `HIGH`, captures the next progress alert.
5. When PIR drops `LOW`, verifies stability for `SLEEP_DEBOUNCE_MS` (1,500 ms) before arming sleep.
6. Employs a 10-second safety timeout on sleep entry to prevent infinite hanging if the PIR line is held active.

### 6.3 Adaptive Dual-Memory Camera Pipeline
* **PSRAM Mode (Default):**
  * Resolution: `FRAMESIZE_HD` (1280 × 720).
  * JPEG Quality: `10` (High fidelity).
  * Buffers: 2 frame buffers allocated in PSRAM (`CAMERA_FB_IN_PSRAM`) with `CAMERA_GRAB_LATEST` at 20 MHz.
* **DRAM Fallback Mode (No PSRAM):**
  * Resolution: `FRAMESIZE_HQVGA` (240 × 176).
  * JPEG Quality: `20`.
  * Buffers: Single frame buffer in internal DRAM (`CAMERA_FB_IN_DRAM`) with 32 KB JPEG buffer at 10 MHz.

### 6.4 Gemini 3.8 Flash Multimodal API
* **Endpoint:** `https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:generateContent`
* **Security Prompt:** Instructs model to produce neutral, concise observations of people, packages, vehicles, and urgency.
* **Schema Enforcement:** Strict JSON schema guarantees machine-parsable properties:
  ```json
  {
    "summary": "Delivery courier in blue vest placing cardboard box on doorstep.",
    "people": 1,
    "packages": 1,
    "vehicles": 0,
    "urgency": "low",
    "image_quality": "good",
    "actions": ["Notify homeowner", "Log delivery event"]
  }
  ```
* **Resilience:** Automatic retry after 2 seconds on HTTP 503 high-demand conditions.

### 6.5 Telegram Multipart Dispatch
* Dispatches directly to Telegram Bot API `/sendPhoto` using standard `multipart/form-data`.
* Pre-allocates multipart payload in PSRAM buffer.
* Automatically selects urgency emojis based on parsed Gemini classification:
  * 🟢 `Door Alert [low]`
  * 🟡 `Door Alert [moderate]`
  * 🟠 `Door Alert [medium]`
  * 🔴 `Door Alert [high]`

---

## 7. Cloud & Notification Credentials

Credentials are stored securely in `include/secrets.h` (excluded from git):

```cpp
#pragma once

#define WIFI_SSID        "Your_SSID"
#define WIFI_PASSWORD    "Your_Password"
#define GEMINI_API_KEY   "Your_Google_AI_Studio_Key"
#define TELEGRAM_TOKEN   "123456789:ABCdefGHIjklMNO..."
#define TELEGRAM_CHAT_ID "987654321"
```

---

## 8. Flash & Verification Procedure

1. **Connect Hardware:** Seat the ESP32-CAM onto the ESP32-CAM-MB base and plug the Micro-USB cable into your PC.
2. **Compile:** In PlatformIO, run `pio run` to verify zero compile or link errors.
3. **Upload:** Run `pio run --target upload`. The MB base automatically controls `IO0` and `RST` to flash the chip.
4. **Boot:** Press the **RST** button on the MB base (or power cycle).
5. **Serial Inspection (115200 baud):**
   ```text
   === Edge AI Door Feed — Woke Up ===
   Wakeup cause: 0 (PIR GPIO13 = 1)
   Initial free heap: 184512 bytes
   PSRAM found — configuring HD (1280x720)
   Camera OK — Free heap: 168244 bytes
   >>> [MOTION DETECTED] Starting PIR Capture Cycle <<<
   Connecting to WiFi.....
   WiFi connected — IP: 192.168.1.145
   --- Capturing Motion Alert #1 ---
   Stabilizing sensor & capturing fresh frame...
   Captured frame: 78420 bytes (1280x720) — Free heap: 162100 bytes
   Sending to Gemini (107240 bytes)...
   Gemini responded OK
   Gemini summary: Delivery person placing package at door. [low]
   Telegram photo sent OK
   Waiting 4000 ms interval for next check...
   Checking PIR sensor (GPIO13): LOW (No motion)
   PIR went LOW — verifying motion stopped (debounce)...
   Motion cycle finished.
   === Entering Sleep Mode ===
   Powering down WiFi radio...
   Entering Light Sleep (Instant wake <10ms, RAM & camera preserved)...
   ```

---

## 9. Troubleshooting Reference

| Symptom | Root Cause | Remediation |
| :--- | :--- | :--- |
| `A fatal error occurred: Failed to connect to ESP32` | `IO0` not grounded or insufficient power | Ground `IO0` before powering on; verify 5V source provides ≥ 1.0A during flash. |
| `rst:0x10 (RTCWDT_RTC_RESET)` on Wi-Fi connection | Power sag brownout reset | Place 100µF capacitor across 5V and GND; use shorter, higher-gauge USB cable. |
| `esp_camera_init failed: 0x20001` | Loose camera ribbon cable | Re-seat OV2640 ribbon connector firmly into FPC latch. |
| Endless `"PIR still HIGH..."` | Sensor time potentiometer set too high | Turn the HC-SR501 `Tx` potentiometer fully counter-clockwise (~2.5s min delay). |
| Camera image blurry or greenish | Optical sensor out of focus or AWB unset | Manually rotate the OV2640 lens ring to adjust focal point; AWB is enabled in firmware. |
| Telegram returns HTTP `400` | Malformed chat ID or token | Verify chat ID is numeric; check bot token from [@BotFather](https://t.me/BotFather). |
| Gemini returns HTTP `403` | Unauthorized key | Confirm Gemini API key is enabled in [Google AI Studio](https://aistudio.google.com/). |
