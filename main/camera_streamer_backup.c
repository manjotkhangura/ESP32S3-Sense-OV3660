/*
 * XIAO ESP32S3 Camera Web Streaming with Motion Detection
 * Camera: OV3660
 * 
 * Features:
 * - Live camera stream in web browser
 * - Motion detection with LED indicator
 * - Access via WiFi
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "driver/gpio.h"

#include "pins.h"
#include "camera_config_ov3660.h"

static const char *TAG = "XIAO_CAM";


//WiFi Configuration - Set these in sdkconfig or here
#ifndef CONFIG_ESP_WIFI_SSID
#define CONFIG_ESP_WIFI_SSID  "Skynetwork"
#endif

#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD "408singhfam"
#endif

// WiFi credentials from sdkconfig
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  5



//Camera
#define CONFIG_CAMERA_TASK_STACK_SIZE 4096
#define CONFIG_CAMERA_TASK_PRIORITY 5

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

// Motion detection variables
static bool motion_detected = false;
static uint32_t motion_count = 0;
static camera_fb_t *prev_frame = NULL;
static const uint32_t MOTION_THRESHOLD = 20;  // Adjust sensitivity
static const uint32_t MOTION_PIXELS = 100;     // Minimum changed pixels

// HTTP server handle
static httpd_handle_t camera_httpd = NULL;

// ============================================================================
// WiFi Event Handler
// ============================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)", s_retry_num, ESP_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============================================================================
// WiFi Initialization
// ============================================================================
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished. Connecting to %s...", ESP_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", ESP_WIFI_SSID);
    }
}

// ============================================================================
// Camera Initialization
// ============================================================================
static esp_err_t init_camera(void)
{
    ESP_LOGI(TAG, "Initializing OV3660 camera...");
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    
    // OV3660 sensor settings
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 = No Effect
    s->set_whitebal(s, 1);       // enable
    s->set_awb_gain(s, 1);       // enable
    s->set_wb_mode(s, 0);        // 0 to 4
    s->set_exposure_ctrl(s, 1);  // enable
    s->set_aec2(s, 0);           // disable
    s->set_gain_ctrl(s, 1);      // enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_bpc(s, 0);            // disable
    s->set_wpc(s, 1);            // enable
    s->set_raw_gma(s, 1);        // enable
    s->set_lenc(s, 1);           // enable
    s->set_hmirror(s, 0);        // disable
    s->set_vflip(s, 0);          // disable
    s->set_dcw(s, 1);            // enable
    s->set_colorbar(s, 0);       // disable

    // Reduce complexity
    s->set_framesize(s, FRAMESIZE_QQVGA);  // Force small size
    s->set_quality(s, 15);                  // Lower quality
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);

    // Disable some features for stability
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 0);          // Disable
    s->set_ae_level(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_bpc(s, 0);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_dcw(s, 1);
    ESP_LOGI(TAG, "Camera settings optimized for stability");
    
    ESP_LOGI(TAG, "OV3660 camera initialized successfully");
    ESP_LOGI(TAG, "Camera supports up to QXGA (2048x1536)");
    
    return ESP_OK;
}

// ============================================================================
// Simple Motion Detection
// ============================================================================
static void detect_motion(camera_fb_t *current_fb)
{
    if (prev_frame == NULL || prev_frame->len != current_fb->len) {
        // First frame or size mismatch
        if (prev_frame) {
            esp_camera_fb_return(prev_frame);
        }
        prev_frame = esp_camera_fb_get();
        
        // Add timeout handling:
        const int max_attempts = 3;
        for (int i = 0; i < max_attempts && !prev_frame; i++) {
        if (i > 0) {
            ESP_LOGW(TAG, "Retry capture attempt %d", i + 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        prev_frame = esp_camera_fb_get();       
    }

    if (!prev_frame) {
        ESP_LOGE(TAG, "Camera capture failed after %d attempts", max_attempts);
        return ESP_FAIL;
    }
        motion_detected = false;
        return;
    }
    
    // Simple pixel difference detection
    uint32_t changed_pixels = 0;
    size_t pixels_to_check = current_fb->len / 10; // Sample 10% of pixels
    
    for (size_t i = 0; i < pixels_to_check; i += 10) {
        int diff = abs(current_fb->buf[i] - prev_frame->buf[i]);
        if (diff > MOTION_THRESHOLD) {
            changed_pixels++;
        }
    }
    
    // Update previous frame
    esp_camera_fb_return(prev_frame);
    prev_frame = esp_camera_fb_get();
    
    // Detect motion
    if (changed_pixels > MOTION_PIXELS) {
        if (!motion_detected) {
            motion_count++;
            ESP_LOGI(TAG, "Motion detected! Count: %lu", motion_count);
        }
        motion_detected = true;
        gpio_set_level(XIAO_LED_RGB_GPIO, 1);  // LED ON
    } else {
        motion_detected = false;
        gpio_set_level(XIAO_LED_RGB_GPIO, 0);  // LED OFF
    }
}

// ============================================================================
// HTTP Stream Handler
// ============================================================================
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[128];

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        // Motion detection
        detect_motion(fb);

        if (fb->format != PIXFORMAT_JPEG) {
            ESP_LOGE(TAG, "Non-JPEG frame");
            esp_camera_fb_return(fb);
            res = ESP_FAIL;
            break;
        }

        // Send frame header
        size_t hlen = snprintf(part_buf, 128,
                             "Content-Type: image/jpeg\r\n"
                             "Content-Length: %u\r\n"
                             "X-Motion: %s\r\n\r\n",
                             fb->len,
                             motion_detected ? "detected" : "none");
        res = httpd_resp_send_chunk(req, part_buf, hlen);

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 14);
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            break;
        }
    }

    return res;
}

// ============================================================================
// HTTP Index Handler
// ============================================================================
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "    <title>XIAO Camera - OV3660</title>\n"
        "    <style>\n"
        "        body { \n"
        "            font-family: Arial, sans-serif; \n"
        "            text-align: center; \n"
        "            margin: 0;\n"
        "            background: #1a1a1a;\n"
        "            color: #fff;\n"
        "        }\n"
        "        h1 { \n"
        "            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
        "            margin: 0;\n"
        "            padding: 20px;\n"
        "        }\n"
        "        .container { padding: 20px; }\n"
        "        img { \n"
        "            max-width: 100%; \n"
        "            height: auto; \n"
        "            border: 3px solid #667eea;\n"
        "            border-radius: 10px;\n"
        "            box-shadow: 0 4px 20px rgba(102, 126, 234, 0.4);\n"
        "        }\n"
        "        .status { \n"
        "            margin: 20px auto; \n"
        "            padding: 15px; \n"
        "            background: #2a2a2a;\n"
        "            border-radius: 10px;\n"
        "            max-width: 400px;\n"
        "        }\n"
        "        .motion { \n"
        "            color: #ff4444; \n"
        "            font-weight: bold;\n"
        "            font-size: 1.2em;\n"
        "            animation: pulse 1s infinite;\n"
        "        }\n"
        "        @keyframes pulse {\n"
        "            0%, 100% { opacity: 1; }\n"
        "            50% { opacity: 0.5; }\n"
        "        }\n"
        "        .info { color: #667eea; margin: 5px; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>🎥 XIAO ESP32S3 Camera Stream</h1>\n"
        "    <div class='container'>\n"
        "        <div class='status'>\n"
        "            <p class='info'>Camera: OV3660</p>\n"
        "            <p class='info'>Resolution: QVGA (320×240)</p>\n"
        "            <p>Status: <span id='status'>Streaming...</span></p>\n"
        "            <p>Motion Events: <span id='motion'>0</span></p>\n"
        "        </div>\n"
        "        <img id='stream' src='/stream'>\n"
        "        <p class='info' style='margin-top:20px;'>Access this page from any device on your WiFi network</p>\n"
        "    </div>\n"
        "    <script>\n"
        "        setInterval(() => {\n"
        "            fetch('/status')\n"
        "                .then(r => r.json())\n"
        "                .then(data => {\n"
        "                    document.getElementById('motion').innerText = data.motion_count;\n"
        "                    const statusEl = document.getElementById('status');\n"
        "                    if (data.motion) {\n"
        "                        statusEl.innerText = '🚨 MOTION DETECTED!';\n"
        "                        statusEl.className = 'motion';\n"
        "                    } else {\n"
        "                        statusEl.innerText = 'Streaming...';\n"
        "                        statusEl.className = '';\n"
        "                    }\n"
        "                });\n"
        "        }, 500);\n"
        "    </script>\n"
        "</body>\n"
        "</html>";
    
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// HTTP Status Handler
// ============================================================================
static esp_err_t status_handler(httpd_req_t *req)
{
    char json[100];
    snprintf(json, sizeof(json), 
             "{\"motion\":%s,\"motion_count\":%lu}",
             motion_detected ? "true" : "false",
             motion_count);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// Start Web Server
// ============================================================================
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting web server on port 80");

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &index_uri);

        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &stream_uri);

        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &status_uri);

        ESP_LOGI(TAG, "✓ Web server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}

// ============================================================================
// Main Application
// ============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  XIAO ESP32S3 Camera Streaming");
    ESP_LOGI(TAG, "  Camera: OV3660");
    ESP_LOGI(TAG, "  Motion Detection Enabled");
    ESP_LOGI(TAG, "========================================");
    
    // Initialize LED
    gpio_reset_pin(XIAO_LED_RGB_GPIO);
    gpio_set_direction(XIAO_LED_RGB_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(XIAO_LED_RGB_GPIO, 0);
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "=== Memory Configuration ===");

    #ifdef CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
    ESP_LOGI(TAG, "Internal-only reserve: %d bytes", 
            CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    #endif

    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "Total PSRAM: %u bytes (%.2f MB)", 
            total_psram, total_psram / 1024.0 / 1024.0);
    ESP_LOGI(TAG, "Free PSRAM: %u bytes (%.2f MB)", 
            free_psram, free_psram / 1024.0 / 1024.0);
    ESP_LOGI(TAG, "Largest block: %u bytes (%.2f KB)", 
            largest_block, largest_block / 1024.0);
    ESP_LOGI(TAG, "===========================");



    // Initialize camera
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed!");
        ESP_LOGE(TAG, "Please check camera connections");
        return;
    }

    
    // Connect to WiFi
    wifi_init_sta();
    
    // Start web server
    start_webserver();
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✓ Setup complete!");
    ESP_LOGI(TAG, "✓ Open browser and go to your IP address");
    ESP_LOGI(TAG, "✓ LED will light up when motion detected");
    ESP_LOGI(TAG, "========================================");
    
    // Keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}