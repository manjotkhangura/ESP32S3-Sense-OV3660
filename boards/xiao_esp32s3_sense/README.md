# XIAO ESP32S3 Sense Camera Streaming with OV3660

WiFi camera streaming for the XIAO ESP32S3 Sense board with OV3660 camera using ESP-IDF. Built for edge AI applications requiring stable video streaming.

## Features

- 📹 Live MJPEG streaming over WiFi
- 🎯 Optimized for OV3660 camera (not OV2640)
- 💾 8MB OCTAL PSRAM configuration
- 🌐 Web interface accessible from any browser
- ⚡ Up to 30fps at VGA, 20fps at HD
- 🔧 Configurable quality and resolution

## Hardware

- XIAO ESP32S3 Sense board
- OV3660 camera module (included with board)
- WiFi antenna (recommended)
- USB-C cable

## Quick Start

### 1. Prerequisites

- ESP-IDF v5.5.2 or later
- VS Code with ESP-IDF extension (recommended)
- 2.4GHz WiFi network

### 2. Clone and Setup

```bash
git clone <your-repo-url>
cd ESPIDF-example
```

### 3. Configure WiFi

```bash
# Copy template
cp sdkconfig.defaults.template sdkconfig.defaults

# Edit with your credentials
vim sdkconfig.defaults
```

Update these lines:
```ini
CONFIG_ESP_WIFI_SSID="YourWiFiName"
CONFIG_ESP_WIFI_PASSWORD="YourPassword"
```

### 4. Build and Flash

```bash
# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

### 5. Access Stream

Watch the serial output for:
```
I (3046) XIAO_CAM: Got IP Address: 192.168.1.252
```

Open Chrome browser: `http://192.168.1.252`

## Critical OV3660 Configuration

The OV3660 requires specific configuration different from the common OV2640. These settings are **essential** for stable operation.

### PSRAM Configuration (OCTAL Mode)

**File: `sdkconfig.defaults`**

```ini
# CRITICAL: XIAO ESP32S3 uses OCTAL PSRAM, not QUAD
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_TYPE_ESPPSRAM64=y
CONFIG_SPIRAM_SPEED_40M=y

# Memory allocation - reduce internal reservation
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
```

**Why:** XIAO board uses OCTAL PSRAM. QUAD mode (default) won't work.

### Camera Buffer Configuration

**File: `boards/xiao_esp32s3_sense/sdkconfig.board`**

```ini
# Disable PSRAM DMA mode (causes overflow with OV3660)
CONFIG_CAMERA_PSRAM_DMA_MODE=n

# Use PSRAM for frame buffers
CONFIG_CAMERA_FB_IN_PSRAM=y
```

**File: `main/camera_stream.c`**

```c
static camera_config_t camera_config = {
    // ... other settings ...
    
    .fb_count = 2,  // MUST be 2 to prevent FB-OVF errors
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};
```

**Why:** OV3660 needs dual frame buffers to prevent overflow during streaming.

### Stream Delay

**File: `main/camera_stream.c` in `stream_handler()`**

```c
esp_camera_fb_return(fb);

if (res != ESP_OK) {
    break;
}

// Prevent buffer overflow
vTaskDelay(pdMS_TO_TICKS(30));
```

**Why:** Gives camera time to prepare next frame without overwhelming JPEG encoder.

## Project Structure

```
ESPIDF-example/
├── boards/
│   └── xiao_esp32s3_sense/
│       ├── pins.h                    # GPIO pin definitions
│       ├── sdkconfig.board           # Hardware-specific config
│       └── board.cmake               # Board integration
│
├── main/
│   ├── camera_stream.c               # Main application
│   ├── Kconfig.projbuild             # WiFi credential definitions
│   └── CMakeLists.txt                # Build configuration
│
├── managed_components/
│   └── espressif__esp32-camera/      # Auto-installed
│
├── idf_component.yml                 # esp32-camera dependency
├── sdkconfig.defaults                # Your settings (gitignored)
├── sdkconfig.defaults.template       # Template for users
├── .gitignore
└── README.md
```

## Configuration

### Resolution Options

Edit in `main/camera_stream.c`:

```c
.frame_size = FRAMESIZE_SVGA,  // Change this
```

Available resolutions:

| Setting | Resolution | Frame Rate | Notes |
|---------|-----------|------------|-------|
| `FRAMESIZE_QVGA` | 320×240 | ~30fps | Low quality, very stable |
| `FRAMESIZE_VGA` | 640×480 | ~30fps | Good balance |
| `FRAMESIZE_SVGA` | 800×600 | ~30fps | Default, recommended |
| `FRAMESIZE_HD` | 1280×720 | ~20fps | HD quality |
| `FRAMESIZE_UXGA` | 1600×1200 | ~12fps | High quality |
| `FRAMESIZE_QXGA` | 2048×1536 | ~8fps | Maximum for OV3660 |

### Quality Settings

```c
.jpeg_quality = 12,  // 0-63 (lower = better quality)
```

- **High quality:** 6-8 (larger files, slower)
- **Balanced:** 10-12 (recommended)
- **Fast:** 15-20 (smaller files, lower quality)

### Brightness (for dark environments)

In `init_camera()` function:

```c
sensor_t *s = esp_camera_sensor_get();

// Optimize for indoor lighting
s->set_brightness(s, 2);              // -2 to 2
s->set_ae_level(s, 2);                // Auto exposure level
s->set_aec_value(s, 700);             // Exposure time (0-1200)
s->set_agc_gain(s, 20);               // Gain (0-30)
s->set_gainceiling(s, GAINCEILING_128X);
```

## Troubleshooting

### PSRAM Not Detected

**Error:**
```
E (178) quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode
```

**Fix:**
```bash
rm sdkconfig sdkconfig.old
idf.py reconfigure
```

Ensure `sdkconfig.defaults` has:
```ini
CONFIG_SPIRAM_MODE_OCT=y  # NOT QUAD
```

### Frame Buffer Overflow

**Error:**
```
cam_hal: FB-OVF
cam_hal: DMA overflow
```

**Fix:**

1. Check `sdkconfig.board`:
```ini
CONFIG_CAMERA_PSRAM_DMA_MODE=n
```

2. Check `camera_stream.c`:
```c
.fb_count = 2,  // Must be 2
```

3. Rebuild:
```bash
idf.py fullclean
idf.py build
```

### JPEG Encoding Timeout

**Error:**
```
cam_hal: NO-EOI - JPEG end marker missing
W (30157) cam_hal: Failed to get frame: timeout
```

**Fix:**

Lower resolution or increase quality number:
```c
.frame_size = FRAMESIZE_VGA,    // Try smaller
.jpeg_quality = 15,              // Higher = faster
```

Or increase delay in `stream_handler()`:
```c
vTaskDelay(pdMS_TO_TICKS(50));  // More time
```

### WiFi Connection Failed

**Error:**
```
Failed to connect to SSID:YourNetwork
```

**Fix:**

1. Verify credentials in `sdkconfig.defaults`
2. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
3. Check WiFi is not hidden SSID
4. Attach WiFi antenna to board

### Black Screen in Browser

**Issue:** Page loads but no video

**Fixes:**

1. **Use Chrome or Firefox** (Safari has poor MJPEG support)

2. Test stream endpoint directly:
```bash
curl http://192.168.1.252/stream
```

3. Check brightness settings (add to `init_camera()`):
```c
s->set_brightness(s, 2);
s->set_ae_level(s, 2);
```

4. Try lower resolution:
```c
.frame_size = FRAMESIZE_QVGA,
```

### Build Errors - WiFi Symbols Not Found

**Error:**
```
'CONFIG_ESP_WIFI_SSID' undeclared
```

**Fix:**

Ensure `main/Kconfig.projbuild` exists with:
```kconfig
menu "WiFi Configuration"
    config ESP_WIFI_SSID
        string "WiFi SSID"
        default "myssid"
    
    config ESP_WIFI_PASSWORD
        string "WiFi Password"
        default "mypassword"
endmenu
```

Then:
```bash
rm sdkconfig
idf.py reconfigure
idf.py build
```

### Flash Errors - JTAG Timeout

**Error:**
```
Error: timed out while waiting for target halted
```

**Fix:**

Use serial flash instead:
```bash
idf.py flash monitor
```

Don't use the ⚡ Flash icon in VS Code - use terminal command.

### Can't Access from Browser

**Error:** `ERR_CONNECTION_REFUSED`

**Fixes:**

1. Verify IP from serial monitor:
```
I (3046) XIAO_CAM: Got IP Address: 192.168.1.XXX
```

2. Test connectivity:
```bash
ping 192.168.1.252
curl http://192.168.1.252
```

3. Check firewall settings

4. Try different port (edit `camera_stream.c`):
```c
config.server_port = 8080;  // Instead of 80
```
Then use: `http://192.168.1.252:8080`

## Browser Compatibility

| Browser | Status | Notes |
|---------|--------|-------|
| Chrome | ✅ Recommended | Best MJPEG support |
| Firefox | ✅ Works well | Good alternative |
| Edge | ✅ Works | Chromium-based |
| Safari | ⚠️ Limited | Poor MJPEG support |

## Performance Optimization

### For Speed (High Frame Rate)

```c
.frame_size   = FRAMESIZE_QVGA,     // 320×240
.jpeg_quality = 15,
.xclk_freq_hz = 24000000,

// In stream_handler:
vTaskDelay(pdMS_TO_TICKS(10));
```

### For Quality (Better Image)

```c
.frame_size   = FRAMESIZE_HD,       // 1280×720
.jpeg_quality = 6,
.xclk_freq_hz = 20000000,

// In stream_handler:
vTaskDelay(pdMS_TO_TICKS(30));
```

### For Stability (Rock Solid)

```c
.frame_size   = FRAMESIZE_VGA,      // 640×480
.jpeg_quality = 12,
.fb_count     = 2,
.xclk_freq_hz = 16000000,

// In stream_handler:
vTaskDelay(pdMS_TO_TICKS(40));
```

## Security Note

`sdkconfig.defaults` contains WiFi credentials and is in `.gitignore`. 

For new users:
```bash
cp sdkconfig.defaults.template sdkconfig.defaults
# Edit with your WiFi credentials
```

Never commit `sdkconfig.defaults` to version control.

## OV3660 vs OV2640 Differences

| Configuration | OV2640 (Common) | OV3660 (This Project) |
|--------------|-----------------|---------------------|
| PSRAM Mode | QUAD | **OCTAL** |
| PSRAM DMA | Enabled | **Disabled** |
| Max Resolution | 1600×1200 | **2048×1536** |
| Frame Buffers | 1-2 | **Must be 2** |
| Memory Reserve | 16KB | **4KB** |

**Key Insight:** Most ESP32 camera examples assume OV2640. The OV3660 needs different configuration to work properly.

## Technical Details

### Memory Usage

- **Total PSRAM:** 8MB (8,388,608 bytes)
- **Reserved Internal:** 4KB
- **Available for Camera:** ~8MB
- **Frame Buffer (VGA):** ~307KB per buffer
- **Frame Buffer (HD):** ~922KB per buffer

### Streaming Protocol

- **Protocol:** MJPEG over HTTP
- **Content-Type:** `multipart/x-mixed-replace; boundary=frame`
- **Port:** 80 (HTTP)
- **Frame Rate:** 20-30fps (depends on resolution)

## Common Issues Summary

| Symptom | Cause | Fix |
|---------|-------|-----|
| PSRAM not detected | QUAD mode configured | Set `CONFIG_SPIRAM_MODE_OCT=y` |
| `FB-OVF` errors | Single frame buffer or DMA mode | Set `.fb_count = 2` and disable DMA |
| JPEG timeouts | Too fast streaming | Add `vTaskDelay(30ms)` |
| Dark images | Poor exposure | Increase brightness/exposure settings |
| WiFi won't connect | Wrong credentials or 5GHz | Use 2.4GHz, check credentials |
| Black screen | Browser incompatibility | Use Chrome, not Safari |
| Build errors | Missing Kconfig | Add `main/Kconfig.projbuild` |
| Flash errors | JTAG mode | Use `idf.py flash` command |

## Development Workflow

```bash
# Clean build
idf.py fullclean
idf.py reconfigure
idf.py build

# Flash and monitor
idf.py flash monitor

# Exit monitor
Ctrl+]

# Just monitor (after flash)
idf.py monitor
```

---

**Questions? Suggestions? Found this helpful?**

Star the repo and share with others struggling with OV3660! ⭐
