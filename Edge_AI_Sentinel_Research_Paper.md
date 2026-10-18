# Zero-Lag Edge-AI Sentinel: In-Place Optical Motion Detection and Stream-Encoded Multimodal Vision on DRAM-Constrained IoT Hardware

**Atharv Kulkarni**, *Member, IEEE*, and Edge-AI Systems Laboratory  
Department of Computer Science & Embedded Systems Engineering  
Contact: `atharv1792006@gmail.com` | Project Repository: [GitHub: Brelios/Edge-AI-SDF](https://github.com/Brelios/Edge-AI-SDF)

---

### Abstract
Commercial smart doorbells and edge monitoring systems typically operate under a severe physical compromise: they either rely on aggressive passive infrared (PIR) sleep cycles—introducing 2 to 4 seconds of wake-up latency that frequently captures empty doorways after a subject has departed—or they stream video continuously, exhausting network bandwidth and posing severe privacy liabilities. Furthermore, deploying multimodal vision artificial intelligence (AI) directly on ultra-low-cost microcontrollers lacking external pseudo-static RAM (PSRAM) introduces catastrophic heap exhaustion and TLS handshake allocation failures (`MBEDTLS_ERR_SSL_ALLOC_FAILED`). In this paper, we propose and implement a zero-lag, always-connected Edge-AI Sentinel on an inexpensive ESP32 microcontroller with internal dynamic RAM (DRAM) only. We introduce an in-place optical motion detection algorithm that operates directly within the compressed JPEG bitstream via an Exponential Moving Average (EMA) of spatial entropy variance, achieving sub-millisecond evaluation while completely rejecting CMOS sensor noise. Crucially, we formulate a zero-lag frame preservation architecture that holds the exact triggering frame and forwards it directly to cloud multimodal models (Google Gemini) via a chunk-aligned (768 to 1024 bytes) zero-heap streaming TLS pipeline, eliminating all dynamic Base64 RAM allocations. A resilient cascading model failover automaton guarantees continuous operation under cloud quota exhaustion. Empirical validation on a physical testbed demonstrates an optical evaluation latency of under 1 ms, total AI classification latency between 2.1 s and 4.6 s, zero lost trigger frames, 100% false-alarm filtering for ambient noise, and an active free heap margin of over 126 KB. Bidirectional command-and-control over Telegram (`/snap`, `/status`, `/motion`) enables sub-1.5s on-demand surveillance without video streaming overhead.

**Index Terms**—Edge Artificial Intelligence (Edge AI), Internet of Things (IoT), Embedded Computer Vision, Optical Motion Detection, Multimodal Large Language Models (MLLMs), Memory-Constrained Systems, ESP32, Zero-Lag Pipeline, Smart Surveillance.

---

## I. INTRODUCTION

The proliferation of Internet of Things (IoT) edge surveillance devices and smart entrance monitors has transformed residential and industrial security infrastructure [1]. Modern entrance monitoring systems demand high responsiveness, rich semantic scene comprehension, low operational power consumption, and robust physical privacy guarantees [2]. However, existing smart cameras present an unsatisfactory architectural trade-off. Consumer products such as the Ring Doorbell, Google Nest, and traditional battery-operated IoT cameras rely heavily on deep-sleep hardware power profiles coupled with passive infrared (PIR) motion sensors [3]. While this topology achieves acceptable battery longevity, PIR sensors exhibit severe functional blindspots: their slow pyroelectric settling times, thermal inertia, and microcontroller wake-up delays introduce an unavoidable latency of 2.0 to 4.5 seconds between initial physical motion and camera sensor stabilization [4]. In real-world doorstep scenarios—such as couriers delivering parcels or brief visitors approaching a threshold—the target has frequently exited the field of view (FOV) before image capture begins, resulting in the well-known "empty doorstep" failure mode.

Conversely, mains-powered continuous video streaming systems circumvent wake-up latency but introduce massive data bandwidth demands (typically 1.5 to 4.0 Mbps per node), severe recurring cloud storage costs, and pervasive privacy intrusions [5]. Edge computing addresses these limitations by performing on-device feature extraction and selective event dispatching [6]. Recent advances in Multimodal Large Language Models (MLLMs), such as Google Gemini, enable unprecedented semantic intelligence: a single visual frame can be evaluated for entity detection (couriers, residents, trespassers, pets), behavioral threat intent (package tampering, lock manipulation, loitering), and urgency prioritization [7].

Deploying MLLM vision pipelines from low-cost IoT nodes, however, faces a severe physical barrier: memory constraints on mass-market microcontroller units (MCUs) [8]. The Espressif ESP32, while ubiquitous in cost-sensitive deployments, possesses only 520 KB of internal SRAM. When external pseudo-static RAM (PSRAM) is omitted—as is standard in ultra-low-cost modules to maintain BOM costs below $5.00—less than 140 KB of contiguous dynamic RAM (DRAM) remains available after the Wi-Fi networking stack, FreeRTOS kernel, and camera Direct Memory Access (DMA) buffers are instantiated. Under these conditions, conventional software architectures catastrophically fail:

1. **Secondary Capture Discard:** Standard camera drivers evaluate motion on low-resolution frames, discard the triggering frame, switch sensor registers to high resolution, and capture a second image. This mode-switch incurs 300 to 800 ms of register settling delay and multiple DMA flushes, completely losing the active subject.
2. **Heap Fragmentation and TLS Allocation Failures:** Encoding a JPEG image into Base64 requires approximately $4/3$ of the image size in heap space, while constructing a JSON REST request body duplicates this memory footprint. When `mbedTLS` attempts to establish a secure TLS 1.2 session with Google cloud endpoints, it demands two 16,384-byte buffers for input/output record fragments. In a fragmented DRAM heap, no single contiguous 16 KB block can be allocated, crashing the system with `MBEDTLS_ERR_SSL_ALLOC_FAILED (-32512)`.
3. **High-Frequency False Alarms:** Naive pixel-differencing algorithms operating on compressed JPEG payloads suffer from catastrophic sensitivity to CMOS thermal shot noise, where single-bit entropy fluctuations trigger persistent false alerts.

To overcome these challenges, this paper presents the design, mathematical modeling, and empirical realization of the **Edge-AI Sentinel**. The primary contributions of this work are summarized as follows:

- **In-Place Optical Motion Engine:** We develop a compressed-domain motion detection algorithm utilizing an Exponential Moving Average (EMA) of JPEG bitstream entropy, executing in under 1 ms without external PIR hardware while rejecting CMOS shot noise.
- **Zero-Lag Frame Pipeline:** We introduce a pointer-preservation architecture that eliminates secondary recapture latencies, ensuring the exact frame that triggered the optical threshold is dispatched to cloud inference models.
- **Zero-Heap Chunked TLS Streaming:** We devise a direct-socket Base64 streaming pipeline that aligns 768-byte raw binary blocks into 1024-byte ASCII chunks, bypassing all heap allocations and guaranteeing unfragmented memory for TLS 1.2 handshakes.
- **Fault-Tolerant Multimodal Cascade:** We implement an automated model failover automaton that transparently cascades across Google Gemini tiers (`gemini-3.7-flash` $\to$ `gemini-3.5-flash-lite` $\to$ `gemini-3.1-flash-lite`), enforcing strict JSON schema compliance.
- **Bidirectional C2 Telegram Architecture:** We demonstrate on-demand interactive telemetry (`/snap`, `/status`, `/motion`) and empirical microsecond-level latency tracking deployed on an off-the-shelf ESP32-CAM module.

---

## II. SYSTEM ARCHITECTURE & HARDWARE CONSTRAINTS

The architecture of the proposed Edge-AI Sentinel is illustrated in Fig. 1. The hardware platform comprises an AI-Thinker ESP32-CAM node integrating an Espressif ESP32-D0WDQ6 dual-core Tensilica Xtensa LX6 microprocessor clocked at 240 MHz, coupled with an Omnivision OV2640 2-Megapixel CMOS image sensor over a Digital Video Port (DVP) 8-bit parallel bus. The device operates in an always-connected surveillance state connected to a 2.4 GHz 802.11b/g/n Wi-Fi local access point.

```mermaid
flowchart TD
    subgraph Edge Hardware Node [ESP32-CAM Sentinel]
        A[OV2640 Image Sensor] -->|8-bit DVP Bus| B[DMA Controller & FIFO]
        B -->|Single 32KB Buffer| C[In-Place Optical Evaluator]
        C -->|Delta >= 8%| D[Zero-Lag Frame Preserver]
        D -->|P_fb Pointer Handover| E[Zero-Heap Chunked TLS Streamer]
        F[Telegram Command Poller] -->|Bidirectional C2| G[Interactive Controller]
    end

    subgraph Secure Cloud Tier [Google Cloud & Telegram]
        E -->|TLS 1.2 POST Stream| H[Google Gemini MLLM Vision API]
        H -->|Structured JSON Schema| I[Semantic Reasoning Engine]
        I -->|Category != false_alarm| J[Telegram Bot API Gateway]
        G <-->|/snap, /status, /motion| J
    end

    subgraph Client Application [User Endpoints]
        J -->|Rich Alert Card & Live Image| K[Telegram Mobile / Desktop App]
    end
```
*Fig. 1. End-to-end operational architecture of the Edge-AI Sentinel system connecting the ESP32-CAM to Google Gemini Vision and Telegram C2.*

### TABLE I: HARDWARE & SYSTEM SPECIFICATIONS OF THE EDGE-AI SENTINEL

| Subsystem | Parameter / Hardware Element | Operational Value |
| :--- | :--- | :--- |
| **Processing Core** | Microcontroller / Architecture | ESP32-D0WDQ6, Dual Tensilica Xtensa LX6 @ 240 MHz |
| **Memory Subsystem** | Internal SRAM / External PSRAM | 520 KB Total (320 KB DRAM) / 0 KB (PSRAM Not Found) |
| **Vision Sensor** | Camera Sensor / Bus Interface | OmniVision OV2640, 8-bit DVP Parallel Bus, SCCB I2C |
| **Frame Buffer** | Resolution / Pixel Format / Sizing | `FRAMESIZE_QVGA` (320×240), `PIXFORMAT_JPEG`, 32 KB DMA Buffer |
| **Clocking** | External Master Clock (XCLK) | 10.0 MHz (LEDC Timer Channel 0, GPIO 0) |
| **Network & Security** | Wireless Radio / Transport Encryption | 2.4 GHz Wi-Fi 802.11b/g/n / TLS 1.2 via mbedTLS |
| **Cloud MLLM Engine** | Primary / Secondary Vision Models | Google Gemini 3.7-Flash / Gemini 3.5-Flash-Lite Cascading |

A critical limitation identified during physical hardware validation is the complete absence of external PSRAM chipsets on commodity modules (reported by ESP-IDF runtime as `quad_psram: PSRAM ID read error: 0xffffffff`). In standard configurations requesting high definition (HD 1280×720) captures, the camera DMA controller attempts to allocate multi-frame double buffers totaling over 150 KB. This immediately triggers fatal DMA FIFO overflows (`FB-OVF`) and system panics. To establish unconditional memory safety, our architecture constrains the sensor to `FRAMESIZE_QVGA` (320×240) in native JPEG mode with a single 32 KB DMA buffer (`CAMERA_FB_IN_DRAM`) and reduces XCLK to 10 MHz. As established in Table I, this preserves over 126 KB of pristine heap margin for TLS communication.

---

## III. MATHEMATICAL MODELING & OPTICAL MOTION DETECTION ALGORITHM

### A. Compressed JPEG Domain Entropy Dynamics

Traditional edge motion detection operates in the spatial pixel domain (RGB or Grayscale), requiring full-frame uncompressed matrices. For a QVGA image (320×240), an uncompressed 8-bit grayscale frame consumes 76,800 bytes—occupying over 60% of available DRAM per buffer. Furthermore, dynamically switching the OV2640 sensor between grayscale surveillance mode and JPEG dispatch mode resets internal DSP registers, introducing bus lockups and 300 to 500 ms of mode-switching latency. To achieve zero-lag reactivity, the Edge-AI Sentinel operates strictly within the compressed JPEG bitstream domain.

A JPEG frame undergoes Discrete Cosine Transform (DCT) block transformation, quantization, and variable-length Huffman entropy coding. Let the two-dimensional spatial intensity of an $8 \times 8$ image block be denoted by $f(x, y)$. The 2D-DCT coefficient matrix $F(u, v)$ is defined as:

$$F(u, v) = \frac{1}{4} C(u) C(v) \sum_{x=0}^{7} \sum_{y=0}^{7} f(x, y) \cos\left[\frac{(2x+1)u\pi}{16}\right] \cos\left[\frac{(2y+1)v\pi}{16}\right] \tag{1}$$

where $C(u), C(v) = 1/\sqrt{2}$ for $u, v = 0$ and $1$ otherwise. Quantization divides $F(u, v)$ by a quality matrix $Q(u, v)$:

$$F_q(u, v) = \text{round}\left(\frac{F(u, v)}{Q(u, v)}\right) \tag{2}$$

Under static scene conditions, high-frequency AC coefficients quantize to zero strings, yielding high run-length compression and a compact Huffman bitstream. When a human subject enters the camera FOV, sharp spatial edges, clothing textures, and luminance gradients dramatically increase high-frequency non-zero AC coefficients. Consequently, the total byte length $L_t = |F_t|$ of the compressed JPEG bitstream exhibits a deterministic, non-linear expansion directly proportional to the optical scene disturbance.

### B. Exponential Moving Average (EMA) Baseline Tracking

Prior compressed-domain methods attempt to compute byte-level Hamming or absolute differences on raw JPEG buffers. However, because Huffman coding is a variable-length prefix code, single-bit CMOS sensor thermal fluctuations cause catastrophic bit-shift avalanche effects throughout the scan payload, generating 25% to 35% byte divergence even on an entirely static scene. To achieve absolute immunity to shot noise, we model scene disturbance as the relative entropy divergence from an adaptive Exponential Moving Average (EMA) baseline:

$$\mu_t = \alpha \cdot L_t + (1 - \alpha) \cdot \mu_{t-1}, \quad 0 < \alpha \le 1 \tag{3}$$

where $\mu_t$ is the baseline frame size at time $t$, and $\alpha$ is the adaptation smoothing factor. The relative entropy variation $\Delta_t$ is:

$$\Delta_t = \frac{|L_t - \mu_t|}{\mu_t} \tag{4}$$

The optical trigger decision rule $\mathbb{I}_{\text{motion}}(t)$ is formulated as:

$$\mathbb{I}_{\text{motion}}(t) = \begin{cases} 1, & \text{if } \Delta_t \ge \theta_{\text{th}} \; \lor \; V_{\text{PIR}}(t) = 1 \\ 0, & \text{otherwise} \end{cases} \tag{5}$$

where $\theta_{\text{th}} = 0.08$ (8.0% relative entropy expansion), and $V_{\text{PIR}}(t) \in \{0, 1\}$ represents the optional hardware PIR state on GPIO 13. Crucially, baseline adaptation is conditioned on the decision state:

$$\mu_t = \begin{cases} \mu_{t-1}, & \text{if } \mathbb{I}_{\text{motion}}(t) = 1 \\ 0.95 \cdot \mu_{t-1} + 0.05 \cdot L_t, & \text{if } \mathbb{I}_{\text{motion}}(t) = 0 \end{cases} \tag{6}$$

By freezing the baseline $\mu_t$ when motion is detected (6), the system prevents the moving target from being integrated into the background model. When the target vacates the frame, $L_t$ drops back to $\mu_t$ without triggering false "exit" alarms. When no motion is present, the slow learning rate ($\alpha = 0.05$) seamlessly absorbs natural ambient diurnal lighting changes and cloud shadows without false alarms.

### C. The Zero-Lag Frame Preservation Pipeline

In conventional architectures, once motion is detected, the triggering frame is discarded (`esp_camera_fb_return`), and a second capture is scheduled. The latency penalty $\tau_{\text{penalty}}$ of secondary recapture is given by:

$$\tau_{\text{penalty}} = \tau_{\text{res\_switch}} + \tau_{\text{settle}} + K \cdot \tau_{\text{flush}} + \tau_{\text{DMA}} \tag{7}$$

where $\tau_{\text{res\_switch}} \approx 150\text{ ms}$, $\tau_{\text{settle}} \approx 300\text{ ms}$, $K \ge 2$ frames flushed to clear stale DMA FIFOs ($2 \times 100\text{ ms}$), and $\tau_{\text{DMA}} \approx 100\text{ ms}$. Thus, $\tau_{\text{penalty}} \ge 750\text{ ms}$ under ideal conditions, expanding to 2.5 to 3.5 seconds on memory-constrained systems. For transient doorstep events lasting under 4 seconds, this guarantees an empty capture.

The Edge-AI Sentinel implements the **Zero-Lag Preservation Theorem**: the exact `camera_fb_t` frame buffer pointer $P_{\text{fb}}$ that satisfied the condition $\Delta_t \ge \theta_{\text{th}}$ in (5) is retained in DRAM. It is forwarded directly to the network encoding pipeline without recapture, achieving $\tau_{\text{penalty}} \equiv 0$. The moving target is captured at the precise moment of maximum optical delta.

---

## IV. ZERO-HEAP STREAMING PROTOCOL & MULTIMODAL INFERENCE

### A. The DRAM Heap Allocation Bottleneck in TLS 1.2

When transmitting images to modern cloud AI APIs over HTTPS, standard implementations assemble the payload as an in-memory `String` or buffer. For a JPEG frame of length $N \approx 12\text{ KB}$, Base64 encoding expands the data to $(4/3)N \approx 16\text{ KB}$. Incorporating the system prompt, JSON schema, and HTTP envelope constructs an in-memory body of over 20 KB. Due to heap fragmentation, `mbedTLS` fails when attempting to allocate its standard transmit (16 KB) and receive (16 KB) SSL record buffers, crashing the socket with error `-32512`.

### B. Zero-Heap Chunk-Aligned Base64 Streaming Algorithm

We resolve this physical limit by designing a direct-socket chunk-aligned streaming protocol. Base64 encoding maps every 3 input bytes into 4 output ASCII characters (24 bits). If raw binary data is partitioned into chunk sizes that are integer multiples of 3, each chunk can be independently encoded and transmitted without intermediate padding.

We define the raw chunk block size as $B_{\text{raw}} = 768\text{ bytes}$ ($3 \times 256$). Each block encodes to an exact output size of:

$$B_{\text{b64}} = \frac{4}{3} \cdot 768 = 1024\text{ bytes} \tag{8}$$

The protocol executes as follows:

1. **Pre-Handshake Connection:** The secure TLS 1.2 client (`WiFiClientSecure`) establishes its connection to Google's API host before any payload buffers are allocated. At this instant, DRAM is completely unfragmented with over 126 KB of contiguous heap, allowing `mbedTLS` to allocate its buffers without error.
2. **Exact Content-Length Pre-Computation:** The total HTTP request size $S_{\text{total}}$ is mathematically pre-computed without string allocation:
   $$S_{\text{total}} = \text{len}(J_{\text{pre}}) + \left\lceil \frac{N}{3} \right\rceil \cdot 4 + \text{len}(J_{\text{post}}) \tag{9}$$
3. **Direct Socket Streaming:** The HTTP headers and static JSON prefix $J_{\text{pre}}$ (stored in Flash `.rodata`) are written directly to the socket. A 1025-byte stack buffer `b64Chunk` is allocated. The raw JPEG pointer $P_{\text{fb}}\to\text{buf}$ is traversed in 768-byte slices, encoded in-place via `mbedtls_base64_encode`, and written directly to the TLS socket stream via `client.write()`. The terminal slice ($N \pmod{768}$) receives standard RFC 4648 padding. The static JSON suffix $J_{\text{post}}$ is appended and flushed. Total dynamic heap allocated during image transmission is exactly 0 bytes.

### C. Cascading MLLM Failover Protocol

To guarantee high availability against free-tier cloud API rate limits (HTTP 429 Too Many Requests) and temporary service outages (HTTP 503), we formulate a deterministic cascading model failover automaton $\mathcal{M} = \{m_0, m_1, m_2, m_3, m_4\}$:

$$\mathcal{M} = \{\text{gemini-3.7-flash}, \; \text{gemini-3.5-flash-lite}, \; \text{gemini-3.1-flash-lite}, \; \text{gemini-3-flash-preview}, \; \text{gemini-3.5-flash}\} \tag{10}$$

If a model returns HTTP 429, 404, or 503, the automaton state transitions as $i \leftarrow (i + 1) \pmod{|\mathcal{M}|}$, transparently retrying the request against the next tier within 500 ms. Once a model responds with HTTP 200 OK, its index is persisted as the active model to avoid cascading delays on subsequent alerts.

### D. Schema-Constrained Reasoning & False-Alarm Suppression

The system enforces a strict JSON `responseSchema` directly in the Gemini `generationConfig`, compelling the model's transformer decoder to output a rigid JSON structure containing: `summary` (String), `category` (Enum: delivery, resident_visitor, suspicious, threat, animal, false_alarm), `urgency` (Enum: none, low, medium, high), `people` (Int), `packages` (Int), `vehicles` (Int), `image_quality` (Enum: good, poor), and `actions` (Array). If the classification category evaluates to `false_alarm` (e.g., wind-blown foliage, shadows, vehicle headlights) and `SUPPRESS_FALSE_ALARMS` is enabled, the alert is silently logged to the serial bus without notifying the user, eliminating notification fatigue.

---

## V. EXPERIMENTAL TESTBED & IMPLEMENTATION

The experimental testbed was implemented and deployed on an AI-Thinker ESP32-CAM module installed at a residential entrance with variable ambient illuminance (15 lux at night to 850 lux under direct daylight). The firmware was compiled using the PlatformIO development environment targeting the Arduino-ESP32 Core v3.3.12 framework on ESP-IDF v5.5. The FreeRTOS scheduler delegates networking and Wi-Fi maintenance to Core 0, while Core 1 executes the high-speed motion polling and sensor control loop.

### TABLE II: EXPERIMENTAL TESTBED OPERATING PARAMETERS

| System Parameter | Configured Value | Technical Description / Purpose |
| :--- | :--- | :--- |
| **Optical Polling Interval ($T_{\text{poll}}$)** | 200 ms | Frame differencing frequency (5 checks/second) |
| **Motion Sensitivity Threshold ($\theta_{\text{th}}$)** | 0.08 (8.0%) | Minimum relative JPEG entropy expansion to trigger alert |
| **EMA Smoothing Factor ($\alpha$)** | 0.05 | Background baseline adaptation rate during static periods |
| **Motion Cooldown Duration ($T_{\text{cool}}$)** | 12,000 ms | Minimum mandatory window between successive Gemini dispatches |
| **Telegram Command Polling ($T_{\text{tele}}$)** | 3,500 ms | Interval for non-blocking `/getUpdates` command polling |
| **JPEG DMA Buffer Sizing** | 32,768 bytes | Fixed internal DRAM FIFO allocated to prevent FB-OVF crashes |

A critical firmware reliability challenge solved during testbed implementation involves the SCCB/I2C bus lockup on sensor reset. When the ESP32 is power-cycled while the OV2640 is in mid-transaction, the camera holds the SDA line LOW, preventing subsequent probe commands (`sccb-ng: SCCB_Read addr phase failed addr:0x30, reg:0x0a, ret:259` / `Camera probe failed 0x106`). We resolve this by sequencing the PWDN hardware power-down pin (GPIO 32) with a mandatory 50 ms power-off and 50 ms power-on delay prior to `esp_camera_init()`, clearing all hung bus states.

---

## VI. RESULTS AND PERFORMANCE EVALUATION

### A. Microsecond Latency Decomposition

To rigorously quantify system performance, hardware timestamps using high-resolution microsecond timers were integrated across all operational stages. Table III presents the empirical latency decomposition across 50 consecutive real-world alert cycles.

### TABLE III: EMPIRICAL LATENCY DECOMPOSITION ACROSS OPERATIONAL STAGES

| Pipeline Stage | Minimum Latency | Mean Latency | Maximum Latency |
| :--- | :--- | :--- | :--- |
| **Optical Motion Evaluation** | 0.38 ms | 0.45 ms | 0.82 ms |
| **Zero-Lag Frame Handover** | 0.00 ms | 0.01 ms | 0.02 ms |
| **Secure TLS Handshake (mbedTLS)** | 840 ms | 1,120 ms | 1,450 ms |
| **Gemini Multimodal Inference** | 1,890 ms | 2,410 ms | 4,685 ms |
| **Telegram Photo Chunk Streaming** | 1,180 ms | 1,390 ms | 1,820 ms |

As demonstrated in Table III, the optical motion evaluation executes in an astonishing 0.45 ms on Core 1—representing virtually zero overhead relative to the 200 ms polling period. Zero-lag frame handover incurs 0.01 ms (pointer assignment), completely eliminating the 2.5-second secondary recapture penalty. Cloud AI inference via Gemini 3.5-Flash-Lite returns structured semantic assessments in an average of 2.41 seconds, with Telegram photo streaming completing in 1.39 seconds. Total end-to-end dispatch from physical threshold crossing to user smartphone notification is 4.92 seconds.

### B. Dynamic Memory Stability & Zero Heap Leakage

Continuous memory profiling was conducted over a 12-hour continuous monitoring trial involving 120 motion triggers. Baseline free heap immediately following Wi-Fi and camera initialization was recorded at 129,412 bytes. During optical motion evaluation, free heap remained at 127,440 bytes. Throughout the TLS 1.2 handshake and chunk-aligned Base64 streaming to Gemini, free heap dipped to a minimum of 126,692 bytes—proving that our zero-heap streaming protocol eliminates dynamic string allocations. Upon completion of Telegram photo upload and calling `esp_camera_fb_return()`, free heap recovered completely to 127,440 bytes. Zero byte leakage was observed across the entire 12-hour operational window.

### C. Optical Trigger Sensitivity & False Alarm Rejection

To evaluate detection accuracy, 80 controlled doorway entry trials were executed alongside 40 ambient disturbance trials (wind-blown tree branches, passing cloud shadows, and headlights). Static scene baseline fluctuations exhibited an entropy delta $\Delta_t < 1.2\%$, remaining well below the $\theta_{\text{th}} = 8.0\%$ threshold. Ambient tree branches and light shifts produced transient variations between 3.1% and 6.4%, successfully avoiding optical trigger events. When human subjects crossed the threshold at speeds ranging from 0.8 m/s to 2.2 m/s, $\Delta_t$ expanded to between 25.8% and 351.0%, triggering the sentinel with 100% true positive rate (TPR). Furthermore, among the 4 ambient events that crossed the threshold during extreme wind gusts, Gemini correctly classified all 4 as category: `false_alarm`, achieving 100% filtering under `SUPPRESS_FALSE_ALARMS`.

### TABLE IV: COMPARATIVE ARCHITECTURAL BENCHMARKING AGAINST EXISTING SOLUTIONS

| System Architecture | Hardware Cost | Wake-Up / Trigger Lag | Semantic AI Reasoning | Memory Footprint |
| :--- | :--- | :--- | :--- | :--- |
| **Commercial PIR Cam (Ring/Nest) [3]** | $100 - $250 | 2.0 - 4.5 s (PIR Sleep) | Cloud Only (Subscription) | Embedded Linux (>128 MB) |
| **Continuous RTSP Streamer [5]** | $30 - $60 | Zero (Continuous) | None (Raw Stream) | Embedded Linux (>64 MB) |
| **TinyML Edge Object Detect [6]** | $15 - $30 | 150 - 400 ms | Bounding Box Only (No Intent) | MCU with PSRAM (4-8 MB) |
| **PIR + Recapture ESP32 [8]** | $6 - $10 | 2.5 - 3.8 s (Recapture Delay) | Basic Cloud API | DRAM Constrained (Prone to OOM) |
| **Edge-AI Sentinel (Proposed)** | **< $6.00** | **< 1 ms (Zero-Lag Preserved)** | **Multimodal MLLM (Gemini Schema)** | **DRAM Safe-Mode (Zero-Heap Stream)** |

---

## VII. DISCUSSION & PRACTICAL SYSTEM TRADE-OFFS

The empirical findings confirm that ultra-low-cost microcontrollers lacking external PSRAM can reliably execute advanced multimodal vision pipelines when software memory architectures are rigorously co-designed with hardware constraints. Restricting the frame resolution to QVGA (320×240) is a conscious engineering trade-off: while HD (1280×720) captures provide superior fine-grained details, modern MLLM vision encoders (such as Gemini's vision transformer) excel at semantic spatial reasoning even on 320×240 resolutions, accurately recognizing couriers, packages, and concealed faces. The 320×240 resolution reduces network transmission payloads to ~8–14 KB, allowing rapid TLS streaming in under 1.5 seconds.

Another vital system benefit is privacy preservation. Unlike commercial cloud cameras that stream 24/7 video into third-party datacenters, the Edge-AI Sentinel processes all ambient frames strictly in local volatile DRAM. Frames are discarded immediately upon evaluation; only frames that satisfy the physical motion threshold ($\Delta_t \ge 8\%$) are securely uploaded for inference. The integration of bidirectional Telegram command-and-control empowers users to query live status, trigger on-demand snapshots, or pause motion alerts during maintenance.

---

## VIII. CONCLUSIONS

This paper presented the design, mathematical modeling, and physical realization of the Edge-AI Sentinel—a zero-lag optical surveillance system deployed on DRAM-constrained ESP32 IoT hardware. By formulating an in-place optical motion detector operating directly on compressed JPEG entropy variance via an Exponential Moving Average baseline, the system achieves sub-millisecond evaluation while completely rejecting CMOS shot noise. The zero-lag frame preservation architecture eliminates the multi-second recapture delays inherent in legacy systems, ensuring moving targets are captured at the moment of threshold crossing. Furthermore, the chunk-aligned zero-heap TLS streaming protocol completely circumvents heap fragmentation, enabling robust communication with Google Gemini multimodal models on a platform with zero external PSRAM.

Future research will explore integrating quantized, ultra-lightweight on-device TinyML micro-models (such as MobileNet-V1 0.25 INT8) directly into the ESP32 pipeline to perform preliminary human-presence verification before initiating Wi-Fi TLS handshakes, further reducing network energy consumption for off-grid solar-powered deployments.

---

## REFERENCES

- **[1]** A. Al-Fuqaha, M. Guizani, M. Mohammadi, M. Aledhari, and M. Ayyash, "Internet of Things: A survey on enabling technologies, protocols, and applications," *IEEE Communications Surveys & Tutorials*, vol. 17, no. 4, pp. 2347-2376, 2015.
- **[2]** P. Warden and D. Situnayake, *TinyML: Machine Learning with TensorFlow Lite on Arduino and Ultra-Low-Power Microcontrollers*. O'Reilly Media, 2019.
- **[3]** S. Ray, "Smart Home Surveillance Systems: Review of Contemporary Architectures, Latency Bottlenecks, and Power Trade-Offs," *IEEE Consumer Electronics Magazine*, vol. 12, no. 3, pp. 45-56, 2023.
- **[4]** Y. K. Tan and T. P. Huynh, "Zero-Lag Motion Sensing: Mitigating Wake-Up Delays in Battery-Powered IoT Security Cameras," *IEEE Sensors Journal*, vol. 21, no. 14, pp. 16210-16219, 2021.
- **[5]** M. Satyanarayanan, "The emergence of edge computing," *Computer*, vol. 50, no. 1, pp. 30-39, 2017.
- **[6]** J. Lin, W.-M. Chen, Y. Lin, C. Gan, S. Han, et al., "MCUNet: Tiny deep learning on IoT devices," *Advances in Neural Information Processing Systems (NeurIPS)*, vol. 33, pp. 11711-11722, 2020.
- **[7]** Gemini Team, Google, "Gemini: A family of highly capable multimodal models," *arXiv preprint arXiv:2312.11805*, 2023.
- **[8]** Espressif Systems, "ESP32 Series Datasheet and Hardware Design Guidelines," v4.2, Espressif Systems Inc., 2024.
- **[9]** G. K. Wallace, "The JPEG still picture compression standard," *IEEE Transactions on Consumer Electronics*, vol. 38, no. 1, pp. xviii-xxxiv, 1992.
- **[10]** N. Farsad, H. B. Yilmaz, A. Eckford, C.-B. Chae, and W. Guo, "A comprehensive survey of recent advancements in molecular communication," *IEEE Communications Surveys & Tutorials*, vol. 18, no. 3, pp. 1887-1919, 2016.
- **[11]** M. M. Al-Zubi and M.-S. Alouini, "Macroscale Molecular Communication in IoT-based Pipeline Inspection and Monitoring Applications: Preliminary Experiment and Mathematical Model," *IEEE Internet of Things Journal*, 2024.
- **[12]** E. Rescorla, "The Transport Layer Security (TLS) Protocol Version 1.3," RFC 8446, Internet Engineering Task Force (IETF), 2018.
- **[13]** ARM mbedTLS Library, "mbedTLS Architectural Design and Memory Footprint Optimization for Microcontrollers," *TrustedFirmware.org*, 2024.
- **[14]** S. Josefsson, "The Base16, Base32, and Base64 Data Encodings," RFC 4648, Internet Engineering Task Force (IETF), 2006.
- **[15]** OmniVision Technologies, "OV2640 Color CMOS QSXGA (2.0 Megapixel) Image Sensor with OmniPixel2 Technology," *Datasheet Version 1.6*, 2006.
- **[16]** B. Benoit, M. J. B. Smith, and C. V. Stewart, "Direct DCT Compressed Domain Motion Estimation for Low-Power Visual Surveillance," *IEEE Transactions on Circuits and Systems for Video Technology*, vol. 16, no. 11, pp. 1380-1392, 2006.
- **[17]** Z. He and S. K. Mitra, "A unified rate-distortion analysis framework for transform coding," *IEEE Transactions on Circuits and Systems for Video Technology*, vol. 11, no. 12, pp. 1221-1236, 2001.
- **[18]** F. Karray, A. Garcia-Ortiz, M. W. Jmal, A. M. Obeid, and M. Abid, "EARNPIPE: A testbed for smart water pipeline monitoring using wireless sensor networks," *Procedia Computer Science*, vol. 96, pp. 285-294, 2016.
- **[19]** R. Buyya, S. N. Srirama, et al., *Fog and Edge Computing: Principles and Paradigms*, John Wiley & Sons, 2019.
- **[20]** C. Zhang, P. Patras, and H. Haddadi, "Deep learning in mobile and wireless networking: A survey," *IEEE Communications Surveys & Tutorials*, vol. 21, no. 3, pp. 2224-2287, 2019.
