# Edge AI Smart Door Feed 

An ultra-lightweight, event-driven door camera system powered by an ESP32-CAM and Cloud Vision AI. The system stays in a low-power deep sleep until motion is detected, captures a still frame, and leverages multimodal cloud LLMs to generate real-time natural-language summaries of doorstep activity.

---

## 🛠️ Hardware Requirements

* **ESP32-CAM Board** (AI-Thinker Model with OV2640 Camera Module)
* **ESP32-CAM-MB** Micro-USB Programmer Base
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
[ ESP32-CAM (Wake) ] ──► [ Captures JPEG ] ──► [ HTTPS POST ] ──► [ Cloud Vision API ]
                                                                        │
[ Telegram / Notification ] ◄────────────── [ Structured JSON ] ◄────────┘