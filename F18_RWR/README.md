# F18_RWR firmware

The Arduino sketch for the RWR repeater on the Waveshare **ESP32-S3-LCD-2.8C** (480×480 round ST7701 panel).
How to build it, set up DCS and use it is in the [project README](../README.md). This file is the firmware reference.

## Source files

| File | What it does |
|---|---|
| `F18_RWR.ino` | Main sketch: Wi-Fi task, status screens, video frames with brightness boost, rings and ticks. |
| `VideoStream.h` | Receives the JPEG stream on TCP 7791 into three PSRAM buffers; the decoder never waits for the network. |
| `FbGfx.h` | Adafruit GFX drawing into the panel's frame buffer, with optional 180° rotation, rings, thick lines, image blit and an underlay mode. |
| `Display_ST7701.*`, `TCA9554PWR.*`, `I2C_Driver.*` | Panel, IO-expander and I2C drivers, copied from F18_SARI_v2. The pixel clock in `Display_ST7701.h` is now overridable (`ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ`); the sketch sets it. |
| `wifi_config.h` | Your Wi-Fi name and password (copy from `wifi_config.example.h`; git-ignored). |

## Options (top of `F18_RWR.ino`)

| Constant | Use |
|---|---|
| `ROTATE_180` | Off by default. On rotates everything drawn, text and video included, by 180°. |
| `BACKLIGHT` | Brightness 0–100. |
| `STALE_MS` | How long without a video frame before the board shows the status screen. |
| `VIDEO_GAIN`, `VIDEO_BLACK` | Brightness multiplier for the streamed picture (4), and the level below which it counts as black (7), to hide JPEG noise. |
| `VIDEO_MIN_MS` | Shortest time between decoded video frames (60 ms). Keep it well under the sender's frame interval (83 ms at 12 fps), or frames are skipped. |
| `VIDEO_LUMA_ONLY`, `TINT_*` | Decode only the brightness of each frame and tint it RWR green on the board (faster). Off decodes in full colour. |

Panel pixel clock (16 MHz) and bounce buffer depth (20 lines) are `ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ` and
`ESP_PANEL_BOUNCE_LINES` in `Display_ST7701.h`. They must be set there: the driver's `.cpp` only sees that header,
so a `#define` in the sketch has no effect. 12 MHz, no bounce buffer and a 40-line one were all tried with 12 fps
video, and none stopped the glitches. What did is the PlatformIO build's custom sdkconfig (`platformio.ini`), which
runs the panel interrupt from internal RAM.

## Video input

TCP port 7791 takes back-to-back JPEGs (ffmpeg's `mjpeg` output), 480×480 or smaller, centred on the panel.
The decoder (JPEGDEC) **fails on 4:4:4 JPEGs**, so the sender must use `-pix_fmt yuvj420p -huffman default`.
Frames larger than 96 KB are skipped. The board draws the cockpit instrument's rings and ticks over the black
parts of each frame, at the radii DCS uses (0.19, 0.56 and 0.74 of the half-width).
The serial log (115200 baud) shows the frame count and, every 5 s while the picture is changing, the average
panel wait and draw time.
