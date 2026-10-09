# Edge AI Smart Door Feed — Comprehensive Build Plan

> **Hardware:** ESP32-CAM (AI-Thinker) · **Programmer:** ESP32-CAM-MB USB Base *or* ESP32-WROOM-32 DevKit (Intelitek kit) · **AI:** Google Gemini 2.0 Flash · **Notifications:** Telegram Bot API

---

## Table of Contents

1. [System Architecture](#1-system-architecture)
2. [Bill of Materials](#2-bill-of-materials)
3. [Board Pinouts](#3-board-pinouts)
4. [Wiring Diagrams](#4-wiring-diagrams)
   - [Phase A: Flashing the ESP32-CAM](#phase-a-flashing-wiring-one-time-setup)
   - [Phase B: Final Deployment](#phase-b-final-deployment-wiring)
5. [Toolchain Setup](#5-toolchain-setup)
6. [Firmware Architecture](#6-firmware-architecture)
7. [Cloud & Notification Setup](#7-cloud--notification-setup)
8. [Flash & Test Procedure](#8-flash--test-procedure)
9. [Enclosure & Deployment](#9-enclosure--deployment)
10. [Troubleshooting Reference](#10-troubleshooting-reference)

---

## 1. System Architecture

```
┌─────────────────── EDGE DEVICE (Door-mounted) ─────────────────────┐
│                                                                      │
│   [PIR Sensor]                                                       │
│       │ OUT HIGH (motion detected)                                   │
│       ▼                                                              │
│   [ESP32-CAM] ──── Deep Sleep ────► Wakes on PIR signal             │
│       │                                                              │
│       ├── Connects to Wi-Fi                                          │
│       ├── OV2640 captures JPEG (SVGA 800×600)                       │
│       └── HTTPS POST ──────────────────────────────────────────►    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
                                                │
                              ┌─────────────────▼──────────────────┐
                              │     Google Gemini 2.0 Flash API     │
                              │  Input : base64 JPEG + text prompt  │
                              │  Output: JSON { summary, actions[] }│
                              └─────────────────┬──────────────────┘
                                                │
                              ┌─────────────────▼──────────────────┐
                              │         Telegram Bot API            │
                              │  Sends: text summary + JPEG image  │
                              │  To:    your phone / chat ID        │
                              └────────────────────────────────────┘
```

**Event cycle timeline (wall adapter, no battery constraint):**

```
t=0ms    PIR triggers HIGH → ESP32-CAM wakes from deep sleep
t=800ms  Wi-Fi association complete
t=1200ms OV2640 captures JPEG
t=2500ms HTTPS POST to Gemini complete, JSON parsed
t=3500ms Telegram notification sent (text + image)
t=3600ms ESP32-CAM returns to deep sleep
         [30s cooldown before next trigger accepted]
```

---

## 2. Bill of Materials

| # | Component | Qty | Notes |
|---|-----------|-----|-------|
| 1 | ESP32-CAM (AI-Thinker, OV2640) | 1 | Your main device |
| 2a | ESP32-CAM-MB USB Base | 1 | **Easiest option:** plug-in USB programmer with auto-reset (CH340 chip) |
| 2b | ESP32-WROOM-32 DevKit (Intelitek) | 1 | **Alternative:** used as USB-UART programmer with manual wiring |
| 3 | PIR Motion Sensor | 1 | AM312 (3.3V) preferred; HC-SR501 (5V) needs level shift |
| 4 | 5V ≥2A Wall Adapter + Micro-USB cable | 1 | Powers ESP32-CAM in deployment |
| 5 | 100µF 16V Electrolytic Capacitor | 1 | Across 5V rail to prevent Wi-Fi brownout |
| 6 | Female-to-Female DuPont jumper wires | ~8 | For DevKit flashing + PIR connections |
| 7 | Breadboard | 1 | From Intelitek kit |

> **⚠️ HC-SR501 Warning:** Its output signal is 5V. The ESP32-CAM GPIO max is 3.3V.
> Use a voltage divider (10kΩ + 20kΩ) on the signal wire or use AM312 instead.

---

## 3. Board Pinouts

### 3.1 ESP32-CAM (AI-Thinker) — Full Pinout

```
                    ┌──────────────────────────────┐
                    │        [ OV2640 Camera ]      │
                    │         (ribbon cable)        │
                    │                               │
              ┌─────┴───────────────────────────────┴─────┐
              │  ○ GND         [ESP32-S module]    3.3V ○ │
              │  ○ 5V                              GND  ○ │
              │  ○ IO12  ← SD card D2              IO1  ○ │← TX  (U0TXD) ★ FLASH
              │  ○ IO13  ← SD card D3              IO3  ○ │← RX  (U0RXD) ★ FLASH
              │  ○ IO15  ← SD card CMD             IO0  ○ │← BOOT/FLASH (GND=flash mode) ★
              │  ○ IO14  ← SD card CLK             GND  ○ │
              │  ○ IO2   ← SD card D0              IO4  ○ │← SD card D1 / Flash LED (active HIGH)
              │  ○ IO16  ← PSRAM (limited use)            │
              │                                           │
              │            [MicroSD slot]                  │
              └───────────────────────────────────────────┘

★ = Pins used during flashing / boot configuration
```

**Key ESP32-CAM Pin Reference:**

| Pin | Label | Function |
|-----|-------|----------|
| 5V | VCC | Power input (connect 5V wall adapter here in deployment) |
| GND | GND | Ground |
| IO1 | U0TXD | UART TX — connects to programmer RX |
| IO3 | U0RXD | UART RX — connects to programmer TX |
| IO0 | BOOT | Pull LOW (GND) to enter flash mode; float/HIGH for normal boot. Also camera XCLK. |
| IO4 | LED / SD D1 | Onboard white flash LED — also shared with SD card D1 |
| IO13 | SD D3 | **Recommended PIR input** (safe GPIO when SD card not used, no boot conflict) |
| IO12 | SD D2 | ⚠️ Strapping pin — must be LOW at boot (selects flash voltage); avoid for PIR |
| IO2 | SD D0 | ⚠️ Strapping pin — must be LOW at boot |
| IO16 | PSRAM | Connected to PSRAM on most AI-Thinker boards — **do not use** for external I/O |

> **SD card vs PIR:** If you are NOT using the MicroSD slot, IO12–IO15 are all available.
> If you ARE using SD, use IO13 only carefully or use another free GPIO.
> **Recommended PIR pin: IO13** (only works reliably when SD card is disabled in firmware)

---

### 3.2 ESP32-WROOM-32 DevKit — Pinout (Used as Programmer)

```
                       USB Micro-B
                          ║
              ┌───────────╨───────────┐
         EN ──┤ EN              D23   ├── IO23
         VP ──┤ VP (IO36)       D22   ├── IO22  ← I2C SCL
         VN ──┤ VN (IO39)        TX   ├── IO1   ★ TX (U0TXD) ← USE THIS
        D34 ──┤ IO34             RX   ├── IO3   ★ RX (U0RXD) ← USE THIS
        D35 ──┤ IO35            D21   ├── IO21  ← I2C SDA
        D32 ──┤ IO32            D19   ├── IO19
        D33 ──┤ IO33            D18   ├── IO18
        D25 ──┤ IO25             D5   ├── IO5
        D26 ──┤ IO26            D17   ├── IO17
        D27 ──┤ IO27            D16   ├── IO16
        D14 ──┤ IO14             D4   ├── IO4
        D12 ──┤ IO12             D0   ├── IO0
        GND ──┤ GND             D2   ├── IO2
        D13 ──┤ IO13            D15   ├── IO15
        SD2 ──┤ SD2             SD1   ├── SD1
        SD3 ──┤ SD3             SD0   ├── SD0
        CMD ──┤ CMD             CLK   ├── CLK
        5V  ──┤ VIN             GND   ├── GND  ★ USE THIS
       3.3V ──┤ 3V3                   │        ★ USE THIS
              └───────────────────────┘

★ = Pins you will use when programming the ESP32-CAM
```

**Key DevKit Pins for Programming:**

| Pin Label | What to connect to |
|-----------|-------------------|
| `TX` (IO1) | → ESP32-CAM `IO3` (U0RXD) |
| `RX` (IO3) | → ESP32-CAM `IO1` (U0TXD) |
| `GND` | → ESP32-CAM `GND` |
| `3V3` | → ESP32-CAM `3.3V` |
| `EN` | → GND on DevKit (disables DevKit chip; board acts as pure USB-UART) |

---

## 4. Wiring Diagrams

### Phase A: Flashing Wiring (One-time setup)

> Choose **one** of the two methods below depending on which programmer you have.

#### Method 1: ESP32-CAM-MB USB Base (Recommended — zero wiring)

> The ESP32-CAM-MB is a plug-in USB base board with a CH340 USB-UART chip and auto-reset circuitry (DTR/RTS). No jumper wires needed.

```
1. Plug the ESP32-CAM directly into the MB base board
   (pins align — camera faces away from USB connector)
2. Connect MB base to PC via Micro-USB cable
3. In PlatformIO: select the COM port → click Upload
4. The MB auto-pulls IO0 LOW and resets for you — fully automatic
5. When done: firmware runs immediately, no wires to remove
```

> **⚠️ Note:** Some MB clones lack proper auto-reset. If upload fails with `Connecting....____`:
> hold the IO0/BOOT button on the MB base while pressing RST, then release both → retry upload.

#### Method 2: ESP32-WROOM-32 DevKit as Programmer (Manual wiring)

> This wiring lets you upload firmware from your PC to the ESP32-CAM via the DevKit's USB-UART chip.

```
PC (USB)
   │
   │ USB Cable
   ▼
┌──────────────────────────┐         ┌──────────────────────────┐
│   ESP32-WROOM-32 DevKit  │         │      ESP32-CAM           │
│   (Intelitek Kit)        │         │      (AI-Thinker)        │
│                          │         │                          │
│  EN ────────── GND  (★1) │         │                          │
│                          │         │                          │
│  5V  ───────────────────────────►  │ 5V                       │
│  GND ───────────────────────────►  │ GND                      │
│  TX  ───────────────────────────►  │ IO3 (U0RXD)              │
│  RX  ◄──────────────────────────   │ IO1 (U0TXD)              │
│                          │         │                          │
│                          │         │ IO0 ─────┐               │
│                          │         │          │ (★2)          │
│                          │         │ GND ─────┘               │
└──────────────────────────┘         └──────────────────────────┘

★1  Connect DevKit EN pin to DevKit GND — this holds the DevKit's own
    ESP32 chip in reset, so only the USB-UART bridge chip is active.
    The DevKit becomes a "dumb" USB-to-serial cable.

★2  Connect IO0 to GND on the ESP32-CAM — this tells the ESP32-CAM
    to enter bootloader (flash) mode instead of running normally.
    REMOVE this wire after flashing before pressing Reset.
```

> **⚠️ Power:** Use **5V** (not 3V3) from the DevKit to power the ESP32-CAM.
> The CAM board has its own 3.3V regulator and draws too much current for the DevKit's 3.3V output.

**Step-by-step flash procedure (Method 2 only):**

```
1. Wire everything as above (EN→GND on DevKit, IO0→GND on CAM)
2. Plug DevKit into PC via USB
3. In PlatformIO: select port, click Upload
4. PlatformIO uploads → you see "Connecting....____"
5. Press the RESET button on the ESP32-CAM once if stuck
6. Wait for "Done uploading"
7. REMOVE the IO0→GND wire
8. Press RESET on ESP32-CAM → firmware runs normally
```

---

### Phase B: Final Deployment Wiring

> Once firmware is flashed, disconnect the DevKit entirely.
> The ESP32-CAM runs standalone, powered by wall adapter.

```
                                    ┌──────────────────────────┐
Wall Adapter (5V 2A)                │      ESP32-CAM           │
    │                               │      (AI-Thinker)        │
    ├── 5V ────────────────────────►│ 5V                       │
    └── GND ───────────────────────►│ GND                      │
                                    │                          │
    ┌──── 100µF Capacitor ──────────┤ (across 5V and GND)      │
    │     (+ to 5V, – to GND)       │                          │
                                    │                          │
┌────────────────────┐              │                          │
│   PIR Sensor       │              │                          │
│   (AM312 / HC-SR501│              │                          │
│                    │              │                          │
│ VCC ───────────────────────────►  │ 3.3V  (AM312 only)       │
│     OR                            │ 5V    (HC-SR501 only)    │
│ GND ───────────────────────────►  │ GND                      │
│ OUT ───────────────────────────►  │ IO13  ← PIR Signal       │
└────────────────────┘              │                          │
                                    │ IO0  [leave floating]    │
                                    │      (NOT connected to GND)
                                    └──────────────────────────┘

NOTE (HC-SR501 users only):
  The HC-SR501 OUT pin is 5V — this WILL damage the ESP32-CAM.
  Add a voltage divider on the signal wire:
  
  PIR OUT ──┬── 10kΩ ──┬── IO13
            │          │
          (nothing)   20kΩ
                       │
                      GND
  This brings 5V → 3.3V safely.
```

---

## 5. Toolchain Setup

### 5.1 Install VS Code + PlatformIO

1. Download & install [VS Code](https://code.visualstudio.com/)
2. Open VS Code → Extensions (Ctrl+Shift+X) → search `PlatformIO IDE` → Install
3. Restart VS Code — PlatformIO initialises (~3 min)

### 5.2 Create New Project

1. Click the PlatformIO icon (alien head) in sidebar
2. **New Project**
   - Name: `EdgeAI-DoorFeed`
   - Board: `AI Thinker ESP32-CAM`
   - Framework: `Arduino`
3. Click Finish — PlatformIO downloads ESP32 toolchain automatically

### 5.3 Project Structure

```
EdgeAI-DoorFeed/
├── platformio.ini          ← board config, libraries
├── src/
│   └── main.cpp            ← your firmware
├── include/
│   └── secrets.h           ← WiFi credentials, API keys (git-ignored!)
└── .gitignore              ← must include secrets.h
```

### 5.4 platformio.ini

```ini
[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.partitions = huge_app.csv

lib_deps =
    esp32-camera
    knolleary/PubSubClient      ; optional MQTT
    bblanchon/ArduinoJson
```

---

## 6. Firmware Architecture

### 6.1 Main Flow (Single wake cycle)

```
setup() — runs every wake from deep sleep
│
├── 1. Read wakeup reason
│       └── If NOT PIR (ext0) wakeup → go back to sleep immediately
│
├── 2. Init OV2640 camera
│       └── Config: FRAMESIZE_SVGA (800×600), JPEG quality 12
│
├── 3. Connect to WiFi
│       ├── Attempt connection (timeout: 10s)
│       └── If failed → log error, deep sleep (save power)
│
├── 4. Capture JPEG frame
│       └── fb = esp_camera_fb_get()
│
├── 5. Encode JPEG as Base64
│
├── 6. HTTPS POST to Gemini API
│       ├── Endpoint: generativelanguage.googleapis.com
│       ├── Model: gemini-2.0-flash
│       ├── Prompt: "Describe what is happening at this door in 2 sentences.
│       │           Return JSON: {summary: string, actions: string[]}"
│       └── Body: { contents: [{ parts: [image, text] }] }
│
├── 7. Parse JSON response
│       └── Extract summary + actions[]
│
├── 8. Send Telegram notification
│       ├── POST image to sendPhoto endpoint
│       └── POST summary text to sendMessage endpoint
│
├── 9. Release camera frame buffer
│
└── 10. esp_deep_sleep_start()
        └── Wakeup source: ext0 on GPIO13 (HIGH = PIR triggered)
```

### 6.2 Deep Sleep Configuration

```cpp
// In setup(), at the end:
esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, HIGH); // wake when PIR goes HIGH
esp_deep_sleep_start();
```

### 6.3 Secrets File (never commit this)

```cpp
// include/secrets.h
#pragma once

#define WIFI_SSID       "your_wifi_name"
#define WIFI_PASSWORD   "your_wifi_password"
#define GEMINI_API_KEY  "AIza..."
#define TELEGRAM_TOKEN  "123456:ABC-..."
#define TELEGRAM_CHAT_ID "987654321"
```

### 6.4 Gemini API Prompt Design

```json
{
  "contents": [{
    "parts": [
      {
        "inline_data": {
          "mime_type": "image/jpeg",
          "data": "<base64_jpeg>"
        }
      },
      {
        "text": "You are a security camera AI. Describe what is happening at this front door in 1-2 sentences. Be specific about people, packages, or vehicles. Return ONLY valid JSON in this format: {\"summary\": \"...\", \"actions\": [\"...\", \"...\"]}"
      }
    ]
  }],
  "generationConfig": {
    "response_mime_type": "application/json"
  }
}
```

---

## 7. Cloud & Notification Setup

### 7.1 Google Gemini API

1. Go to [Google AI Studio](https://aistudio.google.com/)
2. Click **Get API key** → Create key
3. Copy key → paste into `secrets.h` as `GEMINI_API_KEY`
4. Free tier: 15 requests/minute, 1500/day — more than enough for a door cam

### 7.2 Telegram Bot

```
1. Open Telegram → search @BotFather
2. Send: /newbot
3. Follow prompts → choose name and username
4. BotFather gives you: 123456789:ABCdefGHI...  ← this is your BOT_TOKEN
5. Start a chat with your new bot (send it any message)
6. Visit in browser:
   https://api.telegram.org/bot<BOT_TOKEN>/getUpdates
7. Find "chat":{"id": 987654321}  ← this is your CHAT_ID
8. Paste both into secrets.h
```

### 7.3 Test the Bot (before flashing)

```bash
curl -X POST "https://api.telegram.org/bot<TOKEN>/sendMessage" \
  -d "chat_id=<CHAT_ID>&text=Hello from ESP32-CAM!"
```

---

## 8. Flash & Test Procedure

### Step 1 — Verify Serial Connection

```
Using ESP32-CAM-MB:
  1. Plug CAM into MB base → connect USB → open Serial Monitor (115200 baud)
  2. Press RST on MB → you should see bootloader messages

Using DevKit as programmer:
  1. Wire DevKit → CAM (Phase A Method 2 wiring, including IO0→GND)
  2. Plug DevKit into PC
  3. PlatformIO → Serial Monitor → check baud 115200
  4. Press RESET on CAM → you should see bootloader messages
```

### Step 2 — Flash Firmware

```
1. In PlatformIO: Upload button (→) or Ctrl+Alt+U
2. Watch terminal for: "Connecting...__" then "Writing..."
3. When done: remove IO0→GND wire → press RESET on CAM
4. Serial monitor shows: "Camera init OK", "Connecting to WiFi..."
```

### Step 3 — Functional Test Checklist

```
[ ] Camera captures JPEG without "camera init failed" error
[ ] WiFi connects in < 10 seconds
[ ] Base64 encoded JPEG sent to Gemini without HTTP 400/403
[ ] Gemini returns valid JSON with summary and actions[]
[ ] Telegram receives text message with summary
[ ] Telegram receives the JPEG image
[ ] ESP32-CAM enters deep sleep (serial goes quiet)
[ ] PIR trigger wakes board and restarts cycle
[ ] 30-second cooldown between triggers working
```

### Step 4 — Tune OV2640 Settings

```cpp
// In camera config, adjust these to balance quality vs speed:
config.frame_size = FRAMESIZE_SVGA;   // 800×600 — recommended start
config.jpeg_quality = 12;             // 0-63, lower = better quality
config.fb_count = 1;                  // 1 frame buffer (no PSRAM needed)

// Other options:
// FRAMESIZE_VGA   → 640×480  (faster upload)
// FRAMESIZE_XGA   → 1024×768 (sharper, slower)
// FRAMESIZE_UXGA  → 1600×1200 (max, slow on weak WiFi)
```

---

## 9. Enclosure & Deployment

### Placement Guidelines

```
Door frame (exterior)
        │
        │ ~1.8m height
        ▼
  ┌───────────┐
  │ Enclosure │ ← weatherproof box, angled 15° downward
  │  [CAM]   │    camera faces door/path
  │  [PIR]   │    PIR faces outward, 5-7m range
  └───────────┘
        │
        │ USB cable through door frame
        ▼
  5V Wall adapter (inside)
```

### Power Considerations

- Wall adapter: use **5V 2A minimum** — ESP32-CAM + WiFi + camera peaks at ~800mA
- Long USB cables cause voltage drop — keep cable under 1.5m or use higher gauge cable
- The 100µF capacitor must be placed **as close as possible** to the ESP32-CAM 5V and GND pins

### OTA (Over-the-Air) Updates — Optional but Requires Careful Handling

> **⚠️ Deep sleep + OTA conflict:** ArduinoOTA requires the device to be **awake and connected to WiFi** to receive updates. Since this firmware enters deep sleep immediately after each cycle, the OTA window is effectively zero.

**Workaround — GPIO-triggered OTA mode:**

Use a jumper or switch on a spare GPIO to keep the device awake in "OTA mode" when you need to update firmware remotely:

```cpp
#include <ArduinoOTA.h>

#define OTA_MODE_PIN 15  // Connect to GND via jumper to enable OTA mode

void setup() {
    pinMode(OTA_MODE_PIN, INPUT_PULLUP);

    if (digitalRead(OTA_MODE_PIN) == LOW) {
        // OTA MODE: stay awake, don't deep sleep
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED) delay(500);
        ArduinoOTA.begin();
        Serial.println("OTA mode active — waiting for update...");
        while (true) { ArduinoOTA.handle(); delay(10); }
    }

    // Normal operation continues below...
}
```

> **Usage:** Connect IO15 to GND with a jumper → reset the board → it stays awake for OTA.
> Upload new firmware via PlatformIO's "Upload OTA" option. Remove jumper when done.

---

## 10. Troubleshooting Reference

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `Connecting....____` never connects | IO0 not connected to GND | Check IO0→GND wire (DevKit method), or hold IO0/BOOT button on MB base |
| No COM port detected | Missing CH340 driver (MB base) | Install CH340 driver from [wch-ic.com](https://www.wch-ic.com/downloads/CH341SER_EXE.html) |
| `rst:0x10 (RTCWDT_RTC_RESET)` | Brownout — insufficient power | Add/increase capacitor; use better power supply |
| `Camera init failed` | Ribbon cable loose | Reseat OV2640 ribbon cable firmly |
| `HTTP 400 from Gemini` | Bad JSON or wrong model name | Check prompt format; model is `gemini-2.0-flash` |
| `HTTP 403 from Gemini` | Invalid API key | Regenerate key in AI Studio |
| PIR triggers constantly (false) | Sensitivity too high (HC-SR501) | Turn onboard sensitivity pot counter-clockwise |
| PIR never triggers | Signal not reaching GPIO13 | Check wiring; verify PIR LED blinks on motion |
| Green/corrupted images | 3.3V too low during capture | Power from 5V rail instead; check capacitor |
| Telegram sends but no image | JPEG pointer null | Add `if(!fb) return;` null check before encoding |

---

*Build plan authored for Edge AI Smart Door Feed project. See [Readme.md](./Readme.md) for project overview.*
