# Edge AI Smart Door Feed 

An ultra-lightweight, event-driven door camera system powered by an ESP32-CAM and Cloud Vision AI. The system stays in a low-power deep sleep until motion is detected, captures a still frame, and leverages multimodal cloud LLMs to generate real-time natural-language summaries of doorstep activity.

---

## 🛠️ Hardware Requirements

* **ESP32-CAM Board** (AI-Thinker Model with OV2640 Camera Module)
* **ESP32-CAM-MB** Micro-USB Programmer Base *or* **ESP32-WROOM-32 DevKit** (Intelitek kit, used as USB-UART programmer)
* **PIR Motion Sensor** (AM312 or HC-SR501)
* **5V 2A Power Supply** with Micro-USB Cable
* **DuPont Jumper Wires** (Female-to-Female & Female-to-Male)
* **100µF 16V Electrolytic Capacitor** *(Recommended across 5V and GND to prevent Wi-Fi power brownout resets)*
* **Breadboard** *(For prototyping and power distribution)*

---

## ⚡ How It Works

```text
[ PIR Motion Sensor ]
         │ (Signal HIGH)
         ▼
[ ESP32-CAM (Wake) ] ──► [ Captures JPEG ] ──► [ HTTPS POST ] ──► [ Google Gemini 3.8 Flash ]
                                                                        │
[ Telegram / Notification ] ◄────────────── [ Structured JSON ] ◄────────┘
```

---

## 🚀 Quick Start & Setup

### 1. Clone the Repository
```bash
git clone https://github.com/Brelios/Edge-AI-SDF.git
cd Edge-AI-SDF
```

### 2. Configure Secrets (Safe from Git Leaks)
Copy the template file to create your local `secrets.h` (this file is git-ignored and will never be committed):
```bash
cp include/secrets_example.h include/secrets.h
```
Open `include/secrets.h` and enter your credentials:
```cpp
#define WIFI_SSID        "Your_WiFi_Network"
#define WIFI_PASSWORD    "Your_WiFi_Password"
#define GEMINI_API_KEY   "Your_Google_AI_Studio_Key"
#define TELEGRAM_TOKEN   "Your_Telegram_Bot_Token"
#define TELEGRAM_CHAT_ID "Your_Telegram_Chat_ID"
```

### 3. Build & Flash
Open the project folder in **VS Code** with the **PlatformIO** extension:
* Click the **Build** (✓) button.
* Plug in your ESP32-CAM (via ESP32-CAM-MB base or USB-UART programmer).
* Click the **Upload** (→) button.

---

## 🧠 Universal Hardware & Board Compatibility

* **Automatic PSRAM Detection:**
  * **Boards with PSRAM:** Captures at **VGA (640x480)** resolution with double frame buffering in external PSRAM.
  * **Boards without PSRAM / DRAM Fallback:** Seamlessly switches to **HQVGA (240x176)** with a 32 KB DMA buffer in internal DRAM at 10 MHz to avoid buffer overflows (`FB-OVF`).
* **Sensor Self-Healing:** Performs a hardware power-cycle reset on `PWDN` (GPIO 32) at boot to prevent hung I²C/SCCB bus conditions.
* **Dual Wakeup Trigger:**
  * **With PIR:** Wakes instantly on motion via `GPIO13` (active HIGH).
  * **Without PIR (Hands-Free Testing):** Features an automatic 15-second RTC timer wake loop and RST button trigger.