# Edge AI Smart Door Feed — Hardware Components Specification

This document provides complete, exhaustive technical specifications, pin maps, electrical requirements, and connection diagrams for the hardware stack used in the **Edge AI Smart Door Feed (v1.0)**: **ESP32-CAM**, **ESP32-CAM-MB Programmer Base**, and **PIR Sensor**.

---

## 1. System Bill of Materials (BOM)

| Component | Identifier / Part # | Qty | Operating Voltage | Logic Level | Primary Function |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Edge Compute & Vision** | AI-Thinker ESP32-CAM | 1 | 5.0 V DC (input) | 3.3 V | Dual-core Wi-Fi/BT MCU, camera controller, memory host |
| **Image Sensor** | OmniVision OV2640 | 1 | 3.3 V (from board) | 3.3 V DVP | 2MP CMOS sensor with hardware JPEG compression engine |
| **PIR Motion Sensor** | HC-SR501 *(or AM312)* | 1 | 4.5 V – 12 V (HC-SR501) <br> 2.7 V – 3.3 V (AM312) | 3.3 V TTL | Pyroelectric passive infrared motion detection |
| **Programmer & Power Base** | ESP32-CAM-MB Base Shield | 1 | 5.0 V DC (Micro-USB) | 3.3 V UART | Plug-and-play USB-to-UART programmer (CH340 chip, RST & IO0 buttons) |
| **Power Decoupling** | 100 µF 16V Electrolytic | 1 | Up to 16 V DC | N/A | Buffers voltage rail against 800 mA Wi-Fi transmission bursts |
| **Power Supply** | 5V ≥2A Micro-USB Supply | 1 | 100–240 V AC in | 5.0 V DC out | Stable power source for 24/7 continuous operation |
| **Wiring** | DuPont Jumper Wires | 3 | 300 V rated | N/A | Female-to-Female wires connecting PIR (VCC, GND, OUT) to ESP32-CAM headers |

---

## 2. Microcontroller: AI-Thinker ESP32-CAM

The AI-Thinker ESP32-CAM integrates an **ESP32-S** module with an onboard **OV2640 camera ribbon connector**, a **MicroSD card slot**, a **high-brightness white flash LED**, and **4 MB of external pseudo-static RAM (PSRAM)**.

```
                      ┌──────────────────────────────┐
                      │        [ OV2640 Camera ]      │
                      │         (ribbon cable)        │
                      │                              │
                ┌─────┴──────────────────────────────┴─────┐
                │  ○ GND         [ESP32-S module]    3.3V ○ │
                │  ○ 5V                              GND  ○ │
                │  ○ IO12  ← SD card D2              IO1  ○ │← U0TXD (UART TX)
                │  ○ IO13  ← PIR Input (SD D3)       IO3  ○ │← U0RXD (UART RX)
                │  ○ IO15  ← SD card CMD             IO0  ○ │← Boot Strapping (GND=Flash)
                │  ○ IO14  ← SD card CLK             GND  ○ │
                │  ○ IO2   ← SD card D0              IO4  ○ │← Flash LED / SD D1
                │  ○ IO16  ← PSRAM clock/CS (Do Not Use)   │
                │                                           │
                │            [MicroSD Card Slot]            │
                └───────────────────────────────────────────┘
```

### 2.1 Technical Specifications
* **Processor:** Tensilica Xtensa Dual-Core 32-bit LX6 microprocessor running at 240 MHz (up to 600 DMIPS).
* **Internal RAM:** 520 KB SRAM (approx. 320 KB available to application heap).
* **External PSRAM:** 4 MB APMemory SPI PSRAM (detected automatically by firmware).
* **Flash Memory:** 4 MB SPI Flash.
* **Wi-Fi:** 802.11 b/g/n (up to 150 Mbps), 2.4 GHz band with integrated PCB antenna.
* **Deep Sleep Current:** ~20 µA (bare module); Light Sleep current: ~1.2 mA to 6 mA.

### 2.2 Board Pinout Map & Firmware Allocation

| Pin Label | GPIO Num | Primary ESP32 Function | Firmware Allocation | Notes & Constraints |
| :--- | :---: | :--- | :--- | :--- |
| **5V** | — | Voltage Input | 5V Power In | Connect 5V ≥2A power adapter here or via MB Base. |
| **3.3V** | — | 3.3V LDO Output | Sensor Power (AM312 only) | Max output ~150 mA. Do not power HC-SR501 from this pin. |
| **GND** | — | Ground Rail | Ground | Connect common ground with programmer & sensor. |
| **IO1** | GPIO 1 | U0TXD | Serial Monitor / Flashing | Directly routed through MB Base CH340 TX. |
| **IO3** | GPIO 3 | U0RXD | Flashing Input | Directly routed through MB Base CH340 RX. |
| **IO0** | GPIO 0 | Strapping / Boot | Flash Mode (GND=Flash) | Controlled automatically by MB Base or IO0 button. |
| **IO13** | GPIO 13 | SD Data 3 | **PIR Motion Signal Input** | RTC_GPIO14; armed as `ext0` wake source with pull-down. |
| **IO4** | GPIO 4 | SD Data 1 | Onboard Flash LED | Active HIGH. Kept OFF in firmware. |
| **IO12** | GPIO 12 | SD Data 2 | Unused | Strapping pin. Must NOT be pulled HIGH during boot. |
| **IO2** | GPIO 2 | SD Data 0 | Unused | Strapping pin. Must be LOW during flashing. |
| **IO14** | GPIO 14 | SD Clock | Camera Y4 | Connected to internal camera DVP bus. |
| **IO15** | GPIO 15 | SD Command | Camera Y5 | Connected to internal camera DVP bus. |
| **IO16** | GPIO 16 | CS / CLK for PSRAM | Dedicated to PSRAM | **Never connect external wires to IO16.** |

---

## 3. Image Sensor: OmniVision OV2640

The OV2640 is a low-power, single-chip CMOS image sensor capable of outputting pre-compressed JPEG images directly across an 8-bit digital video port (DVP).

### 3.1 Sensor Characteristics
* **Active Array Size:** 1600 × 1200 (UXGA, 2.0 Megapixels).
* **Optical Format:** 1/4" sensor format.
* **Output Capabilities:** Hardware JPEG compression, YUV422/420, RGB565/555, 8-bit RAW RGB.
* **Firmware Capture Formats:**
  * **PSRAM Mode (Default):** `FRAMESIZE_HD` (1280 × 720), JPEG Quality 10, Double DMA Buffers.
  * **DRAM Fallback (No PSRAM):** `FRAMESIZE_HQVGA` (240 × 176), JPEG Quality 20, 32 KB Buffer.
* **Control Interface:** SCCB (Serial Camera Control Bus, I²C compatible) running on GPIO 26 (SDA) and GPIO 27 (SCL).
* **Hardware Reset (PWDN):** Connected to GPIO 32. Toggled HIGH-LOW during boot to clear hung bus states.

### 3.2 Integrated Image Signal Processor (ISP) Tuning
The firmware configures internal digital image processing registers on the OV2640:
* **Brightness / Contrast:** Set to `+1` / `+1` for balanced contrast and shadow detail in door frame conditions.
* **Edge Sharpness:** Set to `+1` hardware sharpening for crisp facial and object recognition.
* **Auto White Balance (AWB):** Advanced AWB enabled with auto gain (`set_whitebal=1`, `set_awb_gain=1`).
* **Auto Exposure DSP:** Advanced AEC DSP enabled (`set_aec2=1`, target exposure level `0`).
* **Gain Ceiling:** Limited to `4X` to eliminate slow-shutter smearing and severe motion blur in ambient light.
* **Lens Correction (`set_lenc=1`):** Eliminates corner vignette and edge darkening caused by wide lenses.

---

## 4. PIR Motion Sensor: HC-SR501 vs. AM312

### 4.1 HC-SR501 Technical Breakdown

```
             ┌───────────────────────────────┐
             │         Fresnel Lens          │
             │                               │
             ├───────────────────────────────┤
             │  [Sx] Sensitivity Adjust      │
             │  [Tx] Time Delay Adjust       │
             │  [::: ] Jumper (L / H Mode)   │
             └───────────────────────────────┘
                 │       │       │
                VCC     OUT     GND
               (5V)    (IO13)  (GND)
```

* **Operating Principle:** Dual pyroelectric sensor detecting differential changes in 8–14 µm infrared radiation.
* **Input Voltage (VCC):** **4.5 V – 12 V DC** (Must be connected to the ESP32-CAM **5V** rail). The module incorporates an onboard 7133-1 LDO voltage regulator that steps down the supply to 3.3V.
* **Output Signal (OUT):** **3.3 V TTL Logic** (HIGH = Motion Detected, LOW = Clear). Directly compatible with ESP32 GPIO13.
* **Quiescent Current:** ~50–65 µA.
* **Detection Angle:** < 100° conical angle.
* **Detection Range:** 3 m to 7 m (adjustable via potentiometer `Sx`).
* **Trigger Mode Jumper:**
  * **`H` (Repeatable Trigger — REQUIRED):** The output remains `HIGH` continuously as long as motion persists within the detection cone. This allows `processMotionCycle()` in firmware to continue taking photos every 4s until the person leaves.
  * **`L` (Single Trigger):** Fires a single pulse of fixed duration and returns `LOW` regardless of continuous motion. **Do not use `L` mode.**
* **Time Delay Potentiometer (`Tx`):**
  * Turn **fully counter-clockwise** to achieve the minimum hold duration (~2.5 to 3 seconds).
* **Warm-up Time:** **30 to 60 seconds.** On initial power-up, the sensor self-calibrates to ambient room radiation and may toggle randomly. Allow 1 minute to settle.

### 4.2 AM312 Miniature PIR Breakdown (Alternative)
* **Input Voltage:** 2.7 V – 3.3 V DC (Can connect to the ESP32-CAM 3.3V pin or 5V rail).
* **Output Signal:** 3.3 V TTL active HIGH.
* **Hold Time:** Fixed internal delay (~2–3 seconds). No potentiometers or jumpers.
* **Form Factor:** Ultra-compact (12 mm diameter), ideal for 3D-printed miniature enclosures.

---

## 5. Hardware Setup & Wiring (ESP32-CAM + ESP32-CAM-MB Base + PIR)

The system utilizes the dedicated **ESP32-CAM-MB programmer base**, which plugs directly onto the bottom pin headers of the ESP32-CAM. This provides a plug-and-play Micro-USB connection for both flashing and 24/7 power, with zero external wiring bridges.

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

### 5.1 PIR Sensor Connections (Only 3 Wires)

| PIR Sensor Pin | ESP32-CAM Header Pin | Wire Color (Typical) | Purpose |
| :--- | :--- | :---: | :--- |
| **VCC** | **5V** | Red | 5V DC power rail to sensor regulator |
| **GND** | **GND** | Black | System ground return |
| **OUT** | **IO13** | Yellow / Blue | 3.3V logic motion trigger to RTC GPIO 13 |

### 5.2 ESP32-CAM-MB Base Functionality

* **Onboard USB-to-UART:** CH340/CH340C bridge providing high-speed serial flashing and live 115200 baud debugging over a single Micro-USB cable.
* **Auto-Flash Circuitry:** Automatically asserts DTR/RTS to pull `IO0` LOW and resets the ESP32 into ROM download mode when `pio run --target upload` runs in PlatformIO.
* **Buttons:**
  * **RST:** Pulls `EN` to GND, performing a hardware restart.
  * **IO0:** Pulls `GPIO0` to GND, manually invoking bootloader mode if auto-download is not supported by a clone board.

---

## 6. Power Consumption Profiles

| State | Typical Current @ 5V | Duration / Trigger |
| :--- | :---: | :--- |
| **Light Sleep (`USE_LIGHT_SLEEP = true`)** | ~1.2 mA – 6 mA | Idle (waiting for motion on GPIO13) |
| **Deep Sleep (`USE_LIGHT_SLEEP = false`)** | ~20 µA – 2 mA | Idle (depends on board regulator/LED) |
| **PIR Sensor Idle (HC-SR501)** | ~50 µA | Continuous |
| **Camera Boot & Sensor Settling** | ~140 mA | ~150 ms |
| **Wi-Fi Connection & TLS Handshake** | ~180 mA – 260 mA | ~600 ms – 1500 ms |
| **Active Wi-Fi Transmit Burst (Gemini POST)** | ~350 mA – 500 mA (peaks ~800 mA) | ~800 ms – 1500 ms |
| **Telegram Photo POST** | ~220 mA – 320 mA | ~600 ms – 1200 ms |
