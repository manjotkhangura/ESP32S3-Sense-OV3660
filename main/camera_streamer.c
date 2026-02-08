/*
 * Working XIAO ESP32S3 OV3660 Camera Stream
 * Uses RGB565 format with JPEG conversion
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
#include "esp_camera.h"
#include "driver/gpio.h"

#include "pins.h"

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


static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static httpd_handle_t camera_httpd = NULL;

// Working OV3660 config
static camera_config_t camera_config = {
    .pin_pwdn     = -1,
    .pin_reset    = -1,
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
    
    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    
    .pixel_format = PIXFORMAT_JPEG,     // Try JPEG first
    //WORKING .frame_size   = FRAMESIZE_QVGA,     // 800x600
    .frame_size   = FRAMESIZE_SVGA,     // 800x600
    .jpeg_quality = 20,
    .fb_count     = 2,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = 1
};

// WiFi event handler
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

// WiFi init
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

    ESP_LOGI(TAG, "WiFi connecting to %s...", ESP_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
    } else {
        ESP_LOGI(TAG, "Failed to connect to WiFi");
    }
}

// Camera init
static esp_err_t init_camera(void)
{
    ESP_LOGI(TAG, "Initializing OV3660...");
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    // Initial settings
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_sharpness(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 0);
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
    s->set_colorbar(s, 0);
    
    ESP_LOGI(TAG, "✓ Camera initialized");
    return ESP_OK;
}


#if 1

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[128];

    // Set content type AND additional headers for Chrome
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "30");
    
    ESP_LOGI(TAG, "Stream started");  // Add logging

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            ESP_LOGE(TAG, "Non-JPEG format");
            esp_camera_fb_return(fb);
            break;
        }

        size_t hlen = snprintf(part_buf, 128,
                             "Content-Type: image/jpeg\r\n"
                             "Content-Length: %u\r\n"
                             "\r\n",
                             fb->len);
        
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 14);
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Stream send failed");
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(90));
    }

    ESP_LOGI(TAG, "Stream ended");
    return res;
}

#else

static esp_err_t stream_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Stream request started");  // ADD THIS
    
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    int frame_count = 0;  // ADD THIS
    
    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Capture failed");
            break;
        }

        frame_count++;  // ADD THIS
        if (frame_count % 10 == 0) {  // ADD THIS
            ESP_LOGI(TAG, "Streamed %d frames", frame_count);
        }

        size_t hlen = snprintf(part_buf, 64,
                             "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                             fb->len);
        
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 14);
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Stream failed at frame %d", frame_count);  // ADD THIS
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "Stream ended");  // ADD THIS
    return res;
}

//#else

// Stream handler
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            ESP_LOGE(TAG, "Non-JPEG frame");
            esp_camera_fb_return(fb);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        size_t hlen = snprintf(part_buf, 64,
                             "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                             fb->len);
        
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
        
        // Small delay to prevent overload
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    return res;
}
#endif

// Index handler
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html = 
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width'>"
        "<title>XIAO Camera</title>"
        "<style>body{margin:0;text-align:center;background:#000}"
        "img{max-width:100%;height:auto}</style>"
        "</head><body>"
        "<h1 style='color:#fff'>XIAO ESP32S3 Camera</h1>"
        "<img id='stream' src='/stream'>"
        "</body></html>";
    
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// Start server
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
        };
        httpd_register_uri_handler(camera_httpd, &index_uri);

        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
        };
        httpd_register_uri_handler(camera_httpd, &stream_uri);

        ESP_LOGI(TAG, "✓ Web server started");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  XIAO ESP32S3 Camera Stream");
    ESP_LOGI(TAG, "========================================");
    
    gpio_reset_pin(XIAO_LED_RGB_GPIO);
    gpio_set_direction(XIAO_LED_RGB_GPIO, GPIO_MODE_OUTPUT);
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera failed!");
        return;
    }
    
    wifi_init_sta();
    start_webserver();
    
    ESP_LOGI(TAG, "✓ Ready! Open browser to your IP");
    
    while (1) {
        gpio_set_level(XIAO_LED_RGB_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(XIAO_LED_RGB_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}