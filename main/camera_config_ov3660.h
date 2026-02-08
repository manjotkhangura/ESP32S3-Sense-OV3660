#ifndef CAMERA_CONFIG_OV3660_H
#define CAMERA_CONFIG_OV3660_H

#include "esp_camera.h"
#include "pins.h"

// Camera configuration for XIAO ESP32S3 Sense with OV3660
static camera_config_t camera_config = {
    // Pin configuration
    .pin_pwdn     = XIAO_CAM_PIN_PWDN,
    .pin_reset    = XIAO_CAM_PIN_RESET,
    .pin_xclk     = XIAO_CAM_PIN_XCLK,
    .pin_sccb_sda = XIAO_CAM_PIN_SIOD,
    .pin_sccb_scl = XIAO_CAM_PIN_SIOC,
    
    .pin_d7       = XIAO_CAM_PIN_D7,
    .pin_d6       = XIAO_CAM_PIN_D6,
    .pin_d5       = XIAO_CAM_PIN_D5,
    .pin_d4       = XIAO_CAM_PIN_D4,
    .pin_d3       = XIAO_CAM_PIN_D3,
    .pin_d2       = XIAO_CAM_PIN_D2,
    .pin_d1       = XIAO_CAM_PIN_D1,
    .pin_d0       = XIAO_CAM_PIN_D0,
    .pin_vsync    = XIAO_CAM_PIN_VSYNC,
    .pin_href     = XIAO_CAM_PIN_HREF,
    .pin_pclk     = XIAO_CAM_PIN_PCLK,
    
    // XCLK settings - OV3660 supports 6-27MHz
    //.xclk_freq_hz = 20000000,
    .xclk_freq_hz = 10000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    
    // Image settings - OV3660 specific
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QQVGA,    // Start with QQVGA (160x120) for testing
    .jpeg_quality = 5,                 // 0-63 lower = higher quality
    .fb_count     = 1,                  // 2 frame buffers
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_LATEST,
    
    // OV3660 can do up to QXGA (2048x1536)
    // But start small for testing!
    //FRAMESIZE_QQVGA
};

#endif // CAMERA_CONFIG_OV3660_H