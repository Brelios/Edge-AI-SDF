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
- [Branch Architecture & Comparison](#-branch-architecture--comparison)

---

## ⚡ System Architecture

```text
┌─────────────────────── EDGE DEVICE (Door Frame) ────────────────────────┐
│                                                                          │
│   [ OV2640 Camera ]  ←──── Always On, Wi-Fi Connected ─────             │
│         │                                                                │
│         ├── Surveillance Mode: QQVGA (160×120) Grayscale @ 300ms        │
│         │   Frame Diff: |CurrentFrame − PrevFrame| > threshold?         │
│         │                        │                                       │
│         │                        ▼ Motion Detected                       │
│         ├── Upgrade to HD (1280×720) JPEG → Encode Base64               │
│         │                                                                │
│         └── Telegram polling: /snap, /status, /motion on|off, /help     │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ HTTPS POST (TLS 1.2 — Insecure Client)
                                ▼
              ┌───────────────────────────────┐
              │ Google Gemini 3.8 Flash API   │
              │ Endpoint: /v1beta/models/...  │
              │ Body: Base64 JPEG + Prompt    │
              │ Returns: Structured JSON      │
              │ (category / urgency / counts) │
              └───────────────┬───────────────┘
                              │ false_alarm → suppressed
                              │ real event  → dispatch
                              ▼
              ┌───────────────────────────────┐
              │ Telegram Bot API              │
              │ /sendPhoto — Multipart JPEG   │
              │ /sendMessage — Commands/Status│
              └───────────────┬───────────────┘
                              │
                              ▼
              📱 Phone — Alerts & Remote Commands
```

---

## ✨ Key Features

* **👁️ Zero-Wiring Optical Motion Detection:**
  Replaces the physical PIR sensor entirely. The OV2640 camera samples ultra-lightweight **QQVGA (160×120) greyscale frames every 300ms** and computes mean absolute pixel difference between successive frames. When the changed pixel fraction exceeds `MOTION_SENSITIVITY` (default 6%), an HD capture + Gemini analysis is dispatched. No jumper wires, no HC-SR501, just the ESP32-CAM board plugged into USB.
* **🌐 Always-Connected Sentinel Mode:**
  Runs continuously on Wi-Fi with **Automatic Modem Sleep** (`WIFI_PS_MIN_MODEM`) enabled—the radio idles down between beacon intervals, reducing idle current by ~50% while the connection stays live for instant command response.
* **📱 Remote Telegram Command Engine:**
  Full two-way control from anywhere in the world:
  * **`/snap`** — Instantly capture and receive a live 720p HD photo (1–2 second response).
  * **`/status`** — System card with uptime, Wi-Fi RSSI, IP address, free heap, and PSRAM.
  * **`/motion off`** — Pause automatic AI alerts (e.g. while you're home gardening).
  * **`/motion on`** — Re-enable automatic AI alerts.
  * **`/help`** — List all commands.
  All commands are secured with `chat_id` validation — only your Telegram account can command the device.
* **🤖 Gemini 3.8 Flash Multimodal Reasoning:**
  Direct cloud multimodal analysis powered by `gemini-3.8-flash`. Enforces a strict OpenAPI response schema returning:
  * **Category:** Concrete classification (`delivery`, `resident_visitor`, `suspicious`, `threat`, `animal`, `false_alarm`).
  * **Summary:** Direct 1–2 sentence factual summary without conversational filler.
  * **Urgency:** Calibrated threat rating (`none`, `low`, `medium`, `high`).
  * **Entities:** Discrete counts for `people`, `packages`, and `vehicles`.
  * **Image Quality:** Automatic detection of lens occlusion, pitch darkness, or blur (`good`, `poor`).
  * **Actions:** Top 1–2 imperative recommendations (under 8 words each).
* **📸 Rich Telegram Status Cards:**
  Direct multipart/form-data upload dispatching the actual captured JPEG photo alongside an engineered status card:
  * 📦 **Delivery:** Courier identification, parcel counts, and pickup prompts.
  * 👤 **Visitor / Resident:** Normal arrivals, bell rings, and family entries.
  * 👁️ **Suspicious:** Loitering, face concealment, window peering, or hesitant night lurking.
  * 🚨 **Threat:** Active break-in attempt, lock manipulation, package theft, or emergent hazards.
  * 🐾 **Animal:** Pets or visiting wildlife.
  * 🍃 **Ambient Motion:** Wind-blown foliage, shadow transitions, or vehicle headlight reflections.
* **🎨 Hardware Image Optimization Pipeline:**
  Fine-tuned OV2640 ISP registers featuring hardware edge sharpening, auto white balance (AWB) gain, lens vignetting correction (`lenc`), black/white pixel correction, and a 4X gain ceiling to prevent slow-shutter motion smearing.
* **🛡️ Adaptive Dual-Memory Pipeline:**
  * **PSRAM Detected:** Automatically provisions **HD (1280×720)** frames with double DMA frame buffers in PSRAM at 20 MHz.
  * **No PSRAM (DRAM Fallback):** Automatically steps down to **HQVGA (240×176)** with a single DRAM buffer at 10 MHz to prevent buffer overruns (`FB-OVF`).
* **🔒 Safe Always-On Architecture:**
  Wi-Fi auto-reconnect with 30s retry intervals, early Base64 heap reclamation (~80KB freed before Telegram upload), cooldown enforcement between alerts, and a boot-time Telegram notification confirm live operation after every restart.

---

## 🛠️ Hardware & Bill of Materials

| # | Component | Model / Specs | Purpose |
| :-: | :--- | :--- | :--- |
| **1** | **ESP32-CAM Module** | AI-Thinker (ESP32-S + 4MB PSRAM) | Main edge computer and Wi-Fi transmission unit |
| **2** | **Camera Sensor** | OmniVision OV2640 (2-Megapixel) | Still frame capture with hardware JPEG compression |
| **3** | **PIR Motion Sensor** | HC-SR501 *(or AM312 miniature)* | Passive infrared human motion detection |
| **4** | **Power Supply** | 5V DC ≥ 2.0A Wall Adapter + Micro-USB | Powers board and sensor during 24/7 deployment |
| **5** | **Decoupling Capacitor** | 100 µF 16V Electrolytic Capacitor | Placed across 5V and GND to suppress Wi-Fi brownouts |
| **6** | **Programmer & Power Base** | ESP32-CAM-MB Base Shield | Dedicated USB-to-UART base with Micro-USB port and RST/IO0 buttons |
| **7** | **Jumper Wires** | 3x Female-to-Female DuPont Cables | Connects PIR sensor (5V, GND, IO13) to ESP32-CAM headers |

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

## 🔌 Hardware Setup & Wiring (ESP32-CAM + MB Base + PIR)

The system is designed around the **ESP32-CAM-MB Programmer Base**, which connects directly to the bottom female headers of the ESP32-CAM. This eliminates the need for separate FTDI bridges, voltage dividers, or manual jumper switches.

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

### 1. PIR Motion Sensor Wiring (Only 3 Wires)

| PIR Sensor Pin | ESP32-CAM Header Pin | Purpose |
| :--- | :--- | :--- |
| **VCC** | **5V** | 5V DC power rail to sensor regulator |
| **GND** | **GND** | System ground return |
| **OUT** | **IO13** | 3.3V logic motion trigger to RTC GPIO 13 |

### 2. Flashing & Running with ESP32-CAM-MB

1. **Flashing Firmware:**
   * Firmly seat the ESP32-CAM onto the MB base.
   * Connect a standard Micro-USB data cable from the MB base to your PC.
   * Run `pio run --target upload`. The onboard CH340 circuit automatically pulls `IO0` LOW and resets the ESP32 into bootloader mode.
2. **Normal Run Mode:**
   * Press the **RST** button on the MB base once flashing completes (or plug into any 5V ≥ 2A USB wall charger).
   * The camera boots, captures a test frame, verifies cloud APIs, and enters **Light Sleep** awaiting PIR motion.

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
├── docs/
│   ├── BUILD_PLAN.md               # Detailed engineering and deployment build plan
│   └── COMPONENTS.md               # Hardware specifications and component pinouts
├── include/
│   ├── secrets_example.h           # Template credentials file (safe to commit)
│   └── secrets.h                   # Active credentials file (git-ignored)
├── src/
│   ├── config.h                    # System settings, intervals, and endpoints
│   ├── camera_driver.h / .cpp      # OV2640 driver, DSP tuning & Base64 encoder
│   ├── gemini_client.h / .cpp      # Gemini 3.8 Flash Vision API client & parser
│   ├── telegram_client.h / .cpp    # Telegram Bot photo & message dispatcher
│   ├── power_manager.h / .cpp      # PIR interrupt, debouncing & Light/Deep sleep
│   ├── board_config.h              # Board selection header
│   ├── camera_pins.h               # Pin definitions for AI-Thinker ESP32-CAM
│   └── main.cpp                    # High-level orchestrator (~140 lines)
├── platformio.ini                  # PlatformIO environment, build flags, and dependencies
├── .gitignore                      # Git exclusion rules (protects secrets.h and binaries)
└── Readme.md                       # Project documentation
```

---

## 🔀 Branch Architecture & Evolution

The repository tracks the complete evolutionary lifecycle of the Edge AI Sentinel firmware across specialized branches. Following the merge of the Sentinel engine, `main` serves as the consolidated production release:

### Branch Status & Relationship Overview

| Branch | Tracking Ref | Status & Role | Key Capabilities | Merge Status |
| :--- | :--- | :---: | :--- | :---: |
| **`main`** | `origin/main` | 🟢 **Production Mainline** | • **Zero-Wiring Optical Sentinel Engine** (5 checks/sec, self-calibrating EMA baseline)<br>• **VGA (640×480)** high-clarity capture with 56 KB DRAM buffer (zero `FB-OVF` errors)<br>• **High-Power Flash LED** (GPIO 4) illumination on motion & `/snap`<br>• **Zero-Heap Direct Streaming** (streaming Base64 to Gemini & multipart to Telegram)<br>• **Interactive Telegram Remote Bot** (`/snap`, `/status`, `/motion on\|off`, `/help`)<br>• **Zero Post-Alert Blind Spot** (instant ~200ms surveillance re-arming, deferred Telegram polling)<br>• **Publication-Grade IEEE Research Paper** (`docs/IEEE_RESEARCH_PAPER.md`) | 🚀 **Active Production Release** |
| **`feature/optical-motion-telegram-remote`** | `origin/feature/...` | 🟢 **Merged Feature Branch** | • Development lineage for the optical surveillance engine and Telegram bot<br>• Tagged milestone **`v1.0.0-zero-lag-vga`** on commit `afa9a7c` | ✅ **Fully merged into `main`** |
| **`v1-working-firmware`** | `origin/v1-working-firmware` | ⚪ **Historical v1.0 Snapshot** | • Preserves initial working firmware documentation and baseline component pinout<br>• Pre-modular architecture baseline | 📦 **Archived (Historical Baseline)** |
| **`backup/main-recent`** | `origin/backup/...` | ⚪ **Legacy Hardware PIR Archive** | • Preserves initial physical PIR + Light Sleep implementation<br>• Documents RF feedback mitigation workarounds (`MAX_ALERT_FRAMES`, 15s cooldowns) | 📦 **Archived (Historical Reference)** |

---

### Detailed Technical Specification Matrix

| Feature / Metric | `main` & `feature/...` | `v1-working-firmware` | `backup/main-recent` |
| :--- | :---: | :---: | :---: |
| **Firmware Architecture** | **Clean Modular Drivers** (`camera`, `gemini`, `telegram`, `motion`, `metrics`) | Monolithic `main.cpp` + legacy HTTP web server files | Monolithic `main.cpp` + legacy HTTP web server files |
| **Surveillance Mechanism** | **In-Place Optical Motion Engine** (5 evals/s, zero external wiring) | Hardware PIR (HC-SR501) on GPIO 13 | Hardware PIR (HC-SR501) on GPIO 13 |
| **Operating State** | **Always-Connected Sentinel** (instant reaction, modem sleep) | Light Sleep wake loop | Light Sleep wake loop |
| **Camera Resolution** | **VGA (640×480)** @ 10 MHz XCLK (crisp, zero overflow) | QQVGA (160×120) or unconstrained HD | HD (1280×720) (prone to `FB-OVF` in DRAM) |
| **DRAM Buffer Allocation** | **56 KB dedicated frame buffer** (~105 KB free DRAM maintained) | Default buffer (frequent heap exhaustion on non-PSRAM) | Unbuffered (crashes without external PSRAM) |
| **Night Illumination** | **High-Power Flash LED** (GPIO 4) auto-pulse (120ms) | None / Disabled | Status indicator blinks only |
| **Telegram Capability** | **Full Two-Way Remote Bot** (`/snap`, `/status`, `/motion`, `/help`) | One-way alert photos only | One-way alert photos only |
| **Gemini AI Pipeline** | **Multi-Model Dynamic Failover** + 60s 429 quota circuit breaker | Single static model string | Basic 3-attempt retry loop |
| **Base64 Encoding** | **Zero-Heap Stream Encoding** directly over TLS socket | Large heap string allocations (causes OOM panic) | Heap allocation |
| **Re-Arming Latency** | **~200 ms** (instant optical re-seed, deferred Telegram polling) | Several seconds (Wi-Fi reconnect + light sleep delay) | 15s forced cooldown (PIR RF feedback guard) |
| **Documentation & Research** | **Publication-Grade IEEE Paper** + reference template included | Basic Markdown specs | Early build plan |

---

## 📄 License
This project is open-source and licensed under the [MIT License](LICENSE).