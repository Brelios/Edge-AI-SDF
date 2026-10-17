# 🚪 Edge AI Smart Door Feed

> **High-speed, intelligent doorstep surveillance powered by ESP32-CAM, Google Gemini 3.8 Flash Vision AI, and Telegram.**

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio)](https://platformio.org/)
[![Hardware](https://img.shields.io/badge/Board-AI--Thinker%20ESP32--CAM-blue)](docs/COMPONENTS.md)
[![Model](https://img.shields.io/badge/AI%20Model-Google%20Gemini%203.8%20Flash-green?logo=google)](https://aistudio.google.com/)
[![Notifications](https://img.shields.io/badge/Alerts-Telegram%20Bot%20API-blue?logo=telegram)](https://core.telegram.org/bots/api)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

An ultra-responsive, event-driven smart door camera that sleeps at near-zero power, wakes instantly upon passive infrared (PIR) motion detection, captures high-clarity 720p HD frames, and uses **Google Gemini 3.8 Flash** multimodal AI to analyze doorstep events in real time. Visual snapshots paired with structured natural-language descriptions and urgency rankings are dispatched straight to your phone via Telegram.

---

## 📑 Table of Contents

- [System Architecture](#-system-architecture)
- [Key Features](#-key-features)
- [Hardware & Bill of Materials](#-hardware--bill-of-materials)
- [HC-SR501 PIR Sensor Calibration](#-hc-sr501-pir-sensor-calibration)
- [Wiring & Flashing Schematics](#-wiring--flashing-schematics)
- [Getting Started](#-getting-started)
  - [1. Clone Repository](#1-clone-repository)
  - [2. Configure Secrets](#2-configure-secrets)
  - [3. Google Gemini API Key](#3-google-gemini-api-key)
  - [4. Telegram Bot Setup](#4-telegram-bot-setup)
  - [5. Build and Flash](#5-build-and-flash)
- [Firmware Configuration Options](#-firmware-configuration-options)
- [Troubleshooting Reference](#-troubleshooting-reference)
- [Repository Structure](#-repository-structure)

---

## ⚡ System Architecture

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

---

## ✨ Key Features

* **⚡ Ultra-Rapid Wakeup:** 
  Supports **Light Sleep mode** (`USE_LIGHT_SLEEP = true`) waking in **< 10 ms** upon PIR trigger. System RAM, camera registers, and DMA configuration are preserved without requiring a cold reboot.
* **🔄 Active Motion Cycle:**
  Loops dynamically while motion is active (`PIR_CAPTURE_INTERVAL_MS = 4000`), sending progressive updates. Verifies a **1.5s debounce period** before re-entering sleep to prevent false alarms or missed activity.
* **🤖 Gemini 3.8 Flash Multimodal Reasoning:**
  Direct cloud multimodal analysis powered by `gemini-3.8-flash`. Enforces a strict JSON response schema returning:
  * **Summary:** Clear, neutral natural-language description of visible events.
  * **Entities:** Counts for `people`, `packages`, and `vehicles`.
  * **Urgency:** Categorized urgency level (`none`, `low`, `medium`, `high`).
  * **Actions:** Concrete suggested response actions.
* **📸 Rich Telegram Alerts:**
  Direct multipart/form-data upload dispatching the actual captured JPEG photo along with a formatted status caption and color-coded urgency indicators:
  * 🟢 **Low:** Expected activity, standard deliveries, or passersby.
  * 🟡 **Moderate:** Prolonged lingering or unidentified individuals.
  * 🟠 **Medium:** Loitering, face concealment, or door handle testing.
  * 🔴 **High:** Forced entry, weapons, or emergent hazards.
* **🎨 Hardware Image Optimization Pipeline:**
  Fine-tuned OV2640 ISP registers featuring hardware edge sharpening, auto white balance (AWB) gain, lens vignetting correction (`lenc`), black/white pixel correction, and a 4X gain ceiling to prevent slow-shutter motion smearing.
* **🛡️ Adaptive Dual-Memory Pipeline:**
  * **PSRAM Detected:** Automatically provisions **HD (1280×720)** frames with double DMA frame buffers in PSRAM at 20 MHz.
  * **No PSRAM (DRAM Fallback):** Automatically steps down to **HQVGA (240×176)** with a single DRAM buffer at 10 MHz to prevent buffer overruns (`FB-OVF`).
* **🔒 Safe Architecture:**
  Internal digital pull-down on `GPIO13`, 10-second sleep-arm safety timeout, and zero recursive call-stack allocations guarantee leak-free 24/7 stability.

---

## 🛠️ Hardware & Bill of Materials

| # | Component | Model / Specs | Purpose |
| :-: | :--- | :--- | :--- |
| **1** | **ESP32-CAM Module** | AI-Thinker (ESP32-S + 4MB PSRAM) | Main edge computer and Wi-Fi transmission unit |
| **2** | **Camera Sensor** | OmniVision OV2640 (2-Megapixel) | Still frame capture with hardware JPEG compression |
| **3** | **PIR Motion Sensor** | HC-SR501 *(or AM312 miniature)* | Passive infrared human motion detection |
| **4** | **Power Supply** | 5V DC ≥ 2.0A Wall Adapter + Micro-USB | Powers board and sensor during 24/7 deployment |
| **5** | **Decoupling Capacitor** | 100 µF 16V Electrolytic Capacitor | Placed across 5V and GND to suppress Wi-Fi brownouts |
| **6** | **Programmer** | ESP32-CAM-MB Base *or* ESP32 DevKit | Flashes firmware over UART into ESP32-CAM |
| **7** | **Jumper Wires** | Female-to-Female DuPont Cables | Interconnecting PIR, capacitor, and programmer |

> 📖 **Full Hardware Deep-Dive:** See [docs/COMPONENTS.md](docs/COMPONENTS.md) for pin specifications, voltage tolerance charts, and component datasheets.

---

## 🎛️ HC-SR501 PIR Sensor Calibration

If utilizing an **HC-SR501** module, set the onboard controls as follows for optimal performance:

```
            ┌───────────────────────────────┐
            │         Fresnel Lens          │
            ├───────────────────────────────┤
            │  [Sx] Sensitivity Adjust      │
            │  [Tx] Time Delay Adjust       │
            │  [::: ] Jumper (L / H Mode)   │
            └───────────────────────────────┘
                │       │       │
               VCC     OUT     GND
              (5V)    (IO13)  (GND)
```

1. **Jumper Mode: Set to `H` (Repeatable Trigger — Essential)**
   * **`H` Position:** Output stays `HIGH` continuously as long as motion is detected. This allows the firmware's active loop to track continuous visitors.
   * `L` Position: Triggers once and drops low even if motion is ongoing. *Avoid `L` mode.*
2. **Time Delay Trimpot (`Tx`): Turn fully Counter-Clockwise**
   * Minimizes the signal hold delay to ~**2.5 – 3 seconds**, matching the firmware's 4-second capture interval and 1.5-second debounce.
3. **Sensitivity Trimpot (`Sx`): Set to Middle Position**
   * Counter-clockwise reduces range (~3 meters) to eliminate street false alarms; clockwise extends detection up to ~7 meters.
4. **Initial Warm-up Period:**
   * The HC-SR501 requires **30 to 60 seconds** after power-up to stabilize its infrared background baseline. Ignore initial erratic triggers during the first minute.

---

## 🔌 Wiring & Flashing Schematics

### 1. Deployment Wiring (24/7 Door-Mounted Operation)

```text
 5V 2A Wall Supply
        ║
 ┌──────╨──────┐
 │  Micro-USB  │
 └──────┬──────┘
        │
  5V ───┼────────────────────────────────────────┐
        │                                        │
 GND ───┼───────────────┐                        │
        │               │                        │
        │      ┌───[ 100µF Cap ]───┐             │
        │      │   +           –   │             │
        ▼      ▼   │           │   ▼             │
 ┌─────────────────┴───────────┴───────────┐     │
 │            ESP32-CAM (AI-Thinker)       │     │
 │                                         │     │
 │ 5V   [5V Power Input]                   │◄────┘
 │ GND  [System Ground]                    │◄──── Common Ground
 │ IO13 [PIR Motion Signal with Pull-Down] │◄──── OUT (3.3V Logic)
 └─────────────────────────────────────────┘      │
                                                  │
 ┌─────────────────────────────────────────┐      │
 │          HC-SR501 PIR Sensor            │      │
 │                                         │      │
 │ VCC  (5V Input)                         │◄─────┘ (From 5V Rail)
 │ OUT  (3.3V Signal Output)               │─────── To IO13
 │ GND  (Common Ground)                    │◄────── To GND Rail
 └─────────────────────────────────────────┘
```

---

### 2. Flashing Wiring (via ESP32-WROOM-32 DevKit as Programmer)

If you do not have an ESP32-CAM-MB micro-USB base, you can use any standard ESP32 DevKit as a USB-to-UART bridge:

```text
 ESP32-WROOM-32 DevKit                      ESP32-CAM (AI-Thinker)
 ┌──────────────────────┐                   ┌──────────────────────┐
 │                      │                   │                      │
 │   5V / VIN ──────────┼──────────────────►│ 5V                   │
 │   GND ───────────────┼──────────────────►│ GND                  │
 │   TX0 (GPIO 1) ──────┼──────────────────►│ IO1 (U0TXD)          │
 │   RX0 (GPIO 3) ──────┼──────────────────►│ IO3 (U0RXD)          │
 │                      │                   │                      │
 │   EN ────┐           │                   │ IO0 ────┐            │
 │          │ (strap)   │                   │         │ (flash)    │
 │   GND ───┘           │                   │ GND ────┘            │
 └──────────────────────┘                   └──────────────────────┘
```

* **DevKit EN → GND:** Holds the DevKit's onboard chip in reset so its CP2102/CH340 chip acts purely as a USB-to-UART bridge.
* **ESP32-CAM IO0 → GND:** Places the ESP32-CAM into serial bootloader download mode. Remove this jumper wire after flashing and press RESET.

---

## 🚀 Getting Started

### 1. Clone Repository
```bash
git clone https://github.com/Brelios/Edge-AI-SDF.git
cd Edge-AI-SDF
```

### 2. Configure Secrets
Copy the template header file into `include/secrets.h`:
```bash
cp include/secrets_example.h include/secrets.h
```

Open `include/secrets.h` in your editor and enter your local network and API credentials:
```cpp
#pragma once

#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#define GEMINI_API_KEY   "YOUR_GEMINI_API_KEY"
#define TELEGRAM_TOKEN   "123456789:ABCdefGHIjklMNOpqrSTUvwxYZ"
#define TELEGRAM_CHAT_ID "123456789"
```

> 🛡️ **Security Guarantee:** `include/secrets.h` is pre-configured in `.gitignore` and will never be tracked or committed.

### 3. Google Gemini API Key
1. Visit [Google AI Studio](https://aistudio.google.com/).
2. Click **Get API key** → **Create key**.
3. Copy your API key and paste it into `secrets.h` as `GEMINI_API_KEY`.
*(Gemini 3.8 Flash includes a generous free tier ideal for home surveillance).*

### 4. Telegram Bot Setup
1. Open Telegram and search for [@BotFather](https://t.me/BotFather).
2. Send `/newbot`, then follow the prompts to choose a bot name and username.
3. BotFather will provide an HTTP API token (e.g., `8986757664:AAErsaX...`). Paste this as `TELEGRAM_TOKEN`.
4. Start a chat with your newly created bot (press **Start**).
5. Open your web browser and navigate to:
   ```
   https://api.telegram.org/bot<YOUR_TELEGRAM_TOKEN>/getUpdates
   ```
6. Locate `"chat":{"id": 123456789}` in the JSON response. Paste that number as `TELEGRAM_CHAT_ID`.

### 5. Build and Flash
1. Open the project in **VS Code** with the **PlatformIO IDE** extension installed.
2. Connect your ESP32-CAM via USB.
3. Click the **PlatformIO: Build** checkmark (✓) in the bottom status bar (or run `pio run`).
4. Ensure `IO0` is tied to `GND`, then click **PlatformIO: Upload** (→) (or run `pio run --target upload`).
5. Disconnect `IO0` from `GND`, open the Serial Monitor at **115200 baud**, and press the onboard **RST** button.

---

## ⚙️ Firmware Configuration Options

You can fine-tune operating parameters at the top of [src/main.cpp](src/main.cpp):

| Parameter | Default Value | Description |
| :--- | :---: | :--- |
| `USE_LIGHT_SLEEP` | `true` | When `true`, uses Light Sleep (<10ms wake, RAM preserved). Set to `false` for deep sleep (~20µA battery operation). |
| `PIR_CAPTURE_INTERVAL_MS`| `4000` | Capture rate in milliseconds while continuous motion is detected. |
| `SLEEP_DEBOUNCE_MS` | `1500` | Wait duration after PIR drops LOW to confirm motion has fully stopped before sleeping. |
| `WIFI_TIMEOUT_MS` | `10000` | Maximum timeout before aborting Wi-Fi connection and sleeping. |
| `GEMINI_MODEL` | `"gemini-3.8-flash"` | Multimodal vision model identifier. |

---

## 🔍 Troubleshooting Reference

| Symptom | Likely Cause | Solution |
| :--- | :--- | :--- |
| `Connecting....____` in PlatformIO | `IO0` not connected to `GND` during boot | Connect `IO0` to `GND` and press the board's RST button before initiating upload. |
| `Brownout detector was triggered` / bootloop | Insufficient power supply current | Use a 5V ≥ 2.0A power supply; install a 100µF capacitor directly across 5V and GND. |
| `Camera init failed: 0x...` | Loose camera ribbon cable | Open the FPC connector latch, re-seat the OV2640 ribbon cable flat, and lock the latch. |
| Endless `"PIR still HIGH..."` on sleep | PIR dwell time too high or sensor floating | Turn the HC-SR501 time trimpot fully counter-clockwise; verify `INPUT_PULLDOWN` is enabled. |
| Rapid repeated notifications | Sensor jumper set to `L` instead of `H` | Move the yellow jumper on the HC-SR501 to the `H` (repeatable trigger) position. |
| Blank or black images | Shutter triggered before sensor AEC settled | The firmware includes 4 dummy frame flushes and a 150ms delay to allow AWB/AEC convergence. |
| Gemini HTTP `403` error | Invalid or expired API key | Verify key in [Google AI Studio](https://aistudio.google.com/) and update `secrets.h`. |
| Gemini HTTP `503` error | High cloud demand spike | The firmware includes an automatic retry backoff after 2 seconds. |

---

## 📂 Repository Structure

```text
Edge-AI-SDF/
├── Building/
│   ├── BUILD_PLAN.md               # Detailed engineering and deployment build plan
│   └── ESP32-CAM_Programming_...   # Guide for using NodeMCU / DevKit programmers
├── docs/
│   └── COMPONENTS.md               # Hardware specifications and component pinouts
├── include/
│   ├── README                      # Include directory guidelines
│   ├── secrets_example.h           # Template credentials file (safe to commit)
│   └── secrets.h                   # Active credentials file (git-ignored)
├── src/
│   ├── board_config.h              # Board selection header
│   ├── camera_pins.h               # Pin definitions for AI-Thinker and camera models
│   └── main.cpp                    # Primary Edge AI firmware
├── platformio.ini                  # PlatformIO environment, build flags, and dependencies
├── .gitignore                      # Git exclusion rules (protects secrets.h and binaries)
└── Readme.md                       # Project documentation
```

---

## 📄 License
This project is open-source and licensed under the [MIT License](LICENSE).