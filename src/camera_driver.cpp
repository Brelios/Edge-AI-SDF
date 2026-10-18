#include "camera_driver.h"
#include "board_config.h"
#include "mbedtls/base64.h"

bool initCamera() {
    // Hardware reset sensor via PWDN if available to clear hung I2C/SCCB state
    #if defined(PWDN_GPIO_NUM) && (PWDN_GPIO_NUM != -1)
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // Power off sensor
    delay(50);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // Power on sensor
    delay(50);
    #endif

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        Serial.println("PSRAM found — configuring HD (1280x720)");
        config.frame_size   = FRAMESIZE_HD;
        config.jpeg_quality = 10;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;
        config.xclk_freq_hz = 20000000;
        config.jpeg_buffer_size = 0;
    } else {
        Serial.println("No PSRAM — using DRAM with 10MHz clock, HQVGA, and 32KB buffer");
        config.frame_size   = FRAMESIZE_HQVGA;      // 240x176
        config.jpeg_quality = 20;                  // Quality 20: reliable size (~8-12KB)
        config.fb_count     = 1;                   // Single buffer saves precious DRAM
        config.fb_location  = CAMERA_FB_IN_DRAM;
        config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
        config.xclk_freq_hz = 10000000;             // 10 MHz prevents bus overrun
        config.jpeg_buffer_size = 32768;           // 32 KB buffer (prevents FB-OVF)
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("esp_camera_init failed: 0x%x\n", err);
        return false;
    }

    // Enhance image clarity & prevent smearing on OV2640
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);     // -2 to 2 (slight lift for indoor/shadows)
        s->set_contrast(s, 1);       // -2 to 2 (clean edge contrast)
        s->set_saturation(s, 0);     // -2 to 2
        s->set_sharpness(s, 1);      // -2 to 2 (hardware edge sharpening)
        s->set_whitebal(s, 1);       // enable auto white balance
        s->set_awb_gain(s, 1);       // enable AWB gain
        s->set_wb_mode(s, 0);        // 0 = Auto AWB
        s->set_exposure_ctrl(s, 1);  // enable auto exposure
        s->set_aec2(s, 1);           // enable advanced AEC DSP
        s->set_ae_level(s, 0);       // target exposure level
        s->set_gain_ctrl(s, 1);      // enable auto gain
        s->set_gainceiling(s, (gainceiling_t)4); // 4X ceiling prevents slow shutter smearing in low light
        s->set_lenc(s, 1);           // Lens correction: eliminates corner vignette/edge blur
        s->set_bpc(s, 1);            // Black pixel correction
        s->set_wpc(s, 1);            // White pixel correction
        s->set_dcw(s, 1);            // Downsize/crop window (enhances clarity)
        s->set_raw_gma(s, 1);        // Raw gamma curve correction
    }
    return true;
}

camera_fb_t* captureJPEG() {
    // Restore HD JPEG mode — motion detector may have left sensor in QQVGA grayscale
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, FRAMESIZE_HD);
        s->set_pixformat(s, PIXFORMAT_JPEG);
    }

    // Discard two stale frames to flush DMA and let AWB/AEC settle after mode switch
    for (int i = 0; i < 2; i++) {
        camera_fb_t* flush = esp_camera_fb_get();
        if (flush) esp_camera_fb_return(flush);
        delay(40);
    }

    // Capture with up to 3 retries
    camera_fb_t* fb = nullptr;
    for (int attempt = 1; attempt <= 3; attempt++) {
        fb = esp_camera_fb_get();
        if (fb) break;
        Serial.printf("[captureJPEG] Attempt %d null — retrying in 150ms...\n", attempt);
        delay(150);
    }

    if (!fb) {
        Serial.println("[captureJPEG] Capture failed after 3 attempts");
        return nullptr;
    }

    Serial.printf("[captureJPEG] Frame: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);
    return fb;
}

String captureAndEncodeJPEG(camera_fb_t** fb_out) {
    Serial.println("Stabilizing sensor & capturing fresh frame...");

    // Discard initial frames to let AEC/AWB converge and clear stale DMA buffer
    for (int i = 0; i < 4; i++) {
        camera_fb_t* dummy = esp_camera_fb_get();
        if (dummy) esp_camera_fb_return(dummy);
        delay(40);
    }

    camera_fb_t* fb = nullptr;
    for (int attempt = 1; attempt <= 3; attempt++) {
        fb = esp_camera_fb_get();
        if (fb) break;
        Serial.printf("Attempt %d returned null, retrying in 200ms...\n", attempt);
        delay(200);
    }

    if (!fb) {
        Serial.println("Camera capture failed completely");
        return "";
    }
    *fb_out = fb;
    Serial.printf("Captured frame: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);

    // Calculate Base64 output size and allocate buffer in PSRAM if available
    size_t encodedLen = ((fb->len + 2) / 3) * 4 + 1;
    uint8_t* encodedBuf = psramFound() ? (uint8_t*)ps_malloc(encodedLen) : (uint8_t*)malloc(encodedLen);
    if (!encodedBuf) {
        Serial.println("Base64 malloc failed");
        return "";
    }

    size_t outLen = 0;
    int ret = mbedtls_base64_encode(encodedBuf, encodedLen, &outLen,
                                    fb->buf, fb->len);
    if (ret != 0) {
        Serial.printf("Base64 encode failed: %d\n", ret);
        free(encodedBuf);
        return "";
    }
    encodedBuf[outLen] = '\0';

    String encoded = String((char*)encodedBuf);
    free(encodedBuf);
    return encoded;
}
