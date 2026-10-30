// F18_RWR: F/A-18C RWR repeater on the Waveshare ESP32-S3-LCD-2.8C (480x480 round, ST7701).
// Shows the cockpit RWR streamed as JPEG frames by receiver/rwr_stream.bat (TCP 7791).
// Without a stream it shows a status screen.
// Display, IO expander and I2C drivers are copied unchanged from F18_SARI_v2.
// Network settings in wifi_config.h (same format as the SARI's, so that file can be copied).

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>             // before the fonts, so the IDE finds the library
#include <Fonts/FreeSansBold12pt7b.h>
#include <JPEGDEC.h>
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"            // pixel clock and bounce buffer are set there: the driver .cpp only sees that header
#include "FbGfx.h"
#include "wifi_config.h"
#include "VideoStream.h"

// ────────────────────────────── Config ──────────────────────────────
constexpr bool     ROTATE_180     = false;   // true rotates everything drawn by 180 degrees
constexpr uint8_t  BACKLIGHT      = 50;      // 0..100
constexpr uint32_t STALE_MS       = 1500;    // no video frame for this long = not live
constexpr uint32_t VIDEO_MIN_MS   = 60;      // shortest gap between drawn frames; keep it well under the sender's frame interval (83 ms at 12 fps)
// Draw the cockpit instrument's rings and tick marks over the video, and the bezel on the status screens.
// false = symbols and status text only, for a display with a physical ring overlay on top of it.
constexpr bool     DRAW_RINGS     = true;

constexpr int      VIDEO_GAIN     = 4;    // brightness multiplier for the streamed RWR picture (1 = unchanged)
constexpr int      VIDEO_BLACK    = 7;     // green level (of 63) treated as black before the gain, to hide JPEG noise
// Decode only the brightness of each frame and tint it RWR green on the board: skips the decoder's colour work.
// The cockpit RWR is one colour, so nothing is lost. false = decode in full colour as sent.
constexpr bool     VIDEO_LUMA_ONLY = true;
constexpr uint8_t  TINT_R = 90, TINT_G = 255, TINT_B = 130;   // colour of a full-brightness symbol in luma-only mode

constexpr int DISP_W = 480, DISP_H = 480;
constexpr int CX = DISP_W / 2, CY = DISP_H / 2;
constexpr int SCOPE_R = 222;                 // outer ring of the status screen, inside the round panel's edge

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
constexpr uint16_t BLACK  = 0;
constexpr uint16_t YELLOW = rgb565(255, 217, 61);
constexpr uint16_t DIM    = rgb565(29, 92, 44);

// ────────────────────────────── Shared state ──────────────────────────────
static volatile bool wifi_up = false;
static char ip_text[20] = "";

static FbGfx gfx(DISP_W, DISP_H, ROTATE_180);
static uint16_t *fb_front = nullptr, *fb_back = nullptr, *fb_spare = nullptr;

// ────────────────────────────── WiFi task ──────────────────────────────
static void wifiTask(void *) {
  WiFi.persistent(false);        // no flash writes for WiFi settings: they stall the cache the panel interrupt runs from
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);          // steady radio traffic; modem-sleep bursts disturb the display panel (as on the SARI)
  WiFi.begin(DCS_WIFI_SSID, DCS_WIFI_PASSWORD);
  Serial.printf("[wifi] joining %s\n", DCS_WIFI_SSID);

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      wifi_up = false;
    } else if (!wifi_up) {
      snprintf(ip_text, sizeof(ip_text), "%s", WiFi.localIP().toString().c_str());
      Serial.printf("[wifi] joined, ip %s\n", ip_text);
      wifi_up = true;
    }
    delay(250);
  }
}

// ────────────────────────────── Drawing ──────────────────────────────
static void drawBezel() {
  gfx.ring(CX, CY, SCOPE_R - 3, SCOPE_R, DIM);
  gfx.ring(CX, CY, SCOPE_R / 2 - 1, SCOPE_R / 2 + 1, DIM);
  for (int deg = 0; deg < 360; deg += 30) {
    const float a = deg * DEG_TO_RAD, inner = (deg % 90) ? 0.92f : 0.85f;
    gfx.thickLine(CX + sinf(a) * SCOPE_R * inner, CY - cosf(a) * SCOPE_R * inner,
                  CX + sinf(a) * SCOPE_R, CY - cosf(a) * SCOPE_R, 3, DIM);
  }
  const int s = 12;   // own-ship cross
  gfx.thickLine(CX, CY - s, CX, CY + s, 3, DIM);
  gfx.thickLine(CX - s, CY, CX + s, CY, 3, DIM);
}

// Hash of the last video frame drawn: the stream repeats identical frames while the scope is quiet, and
// decoding them again only loads the PSRAM the panel is reading from. Cleared whenever video is replaced.
static uint32_t last_video_hash = 0;

// Shows fb_back and rotates the three buffers
static void present() {
  LCD_showFrameBuffer(fb_back);
  uint16_t *shown = fb_back;
  fb_back = fb_spare;
  fb_spare = fb_front;
  fb_front = shown;
}

static void drawStatus(bool joining) {
  last_video_hash = 0;
  LCD_waitForSpareFrameBuffer();   // the panel may still be reading fb_back
  gfx.setBuffer(fb_back);
  gfx.fillScreen(BLACK);
  if (DRAW_RINGS) drawBezel();

  if (joining) {
    gfx.textCentered("JOINING WIFI", CX, CY - 50, &FreeSansBold12pt7b, YELLOW);
    gfx.textCentered(DCS_WIFI_SSID, CX, CY + 50, &FreeSansBold12pt7b, DIM);
  } else {
    gfx.textCentered("NO VIDEO", CX, CY - 50, &FreeSansBold12pt7b, YELLOW);
    gfx.textCentered(ip_text, CX, CY + 50, &FreeSansBold12pt7b, DIM);   // the address the stream goes to
  }
  present();
}

// ────────────────────────────── Video ──────────────────────────────
static JPEGDEC jpeg;

// The cockpit RWR's symbols are dim, and more so after JPEG compression. Each channel has the near-black noise floor
// subtracted, then is amplified until it saturates. (Lookup tables were tried: slower than this arithmetic.)
static inline __attribute__((always_inline)) uint16_t brighten(uint16_t c) {
  const int r = max(0, (int)(c >> 11) - VIDEO_BLACK / 2) * VIDEO_GAIN;
  const int g = max(0, (int)((c >> 5) & 0x3F) - VIDEO_BLACK) * VIDEO_GAIN;
  const int b = max(0, (int)(c & 0x1F) - VIDEO_BLACK / 2) * VIDEO_GAIN;
  return (min(r, 31) << 11) | (min(g, 63) << 5) | min(b, 31);
}
// A pixel with none of these bits set is at or below the black level in every channel, so brighten() gives 0.
// Exact only while each black level plus one is a power of two (7 -> green 0..7, red/blue 0..3).
static_assert(((VIDEO_BLACK + 1) & VIDEO_BLACK) == 0 && ((VIDEO_BLACK / 2 + 1) & (VIDEO_BLACK / 2)) == 0,
              "VIDEO_BLACK must be 2^n - 1 for the black fast path");
constexpr uint16_t BRIGHT_BITS = ((0x1F & ~(VIDEO_BLACK / 2)) << 11) | ((0x3F & ~VIDEO_BLACK) << 5) | (0x1F & ~(VIDEO_BLACK / 2));

// The cockpit instrument's printed face (rings and ticks) as one bit per screen pixel, in internal RAM. jpegDraw puts
// it into each decoded block before the block is copied out, so the face costs no extra pass over PSRAM.
constexpr uint16_t FACE = rgb565(200, 210, 140);
static uint8_t face_mask[DISP_W * DISP_H / 8];
constexpr int FACE_BEHIND_GREEN = 12;   // a face pixel stays behind anything brighter than this (RGB565 green, of 63)
static bool face_in_decode = false;     // the frame fills the screen, so jpegDraw draws the face

// Luma-only mode: brightness (0..255) to the tinted RGB565 pixel, with the same black level and gain as brighten()
static uint16_t gray_lut[256];
static void buildGrayTable() {
  for (int v = 0; v < 256; v++) {
    const int l = constrain((v - VIDEO_BLACK * 4) * VIDEO_GAIN, 0, 255);
    gray_lut[v] = rgb565(TINT_R * l / 255, TINT_G * l / 255, TINT_B * l / 255);
  }
}
static uint16_t gray_block[MAX_BUFFERED_PIXELS * 2];   // a decoded 8-bit block, widened; the decoder's buffer holds this many

static uint32_t prof_pixel_us = 0, prof_blit_us = 0;   // time inside jpegDraw, for the 5 s report
static int IRAM_ATTR jpegDraw(JPEGDRAW *d) {   // in IRAM: program code otherwise runs from PSRAM (XIP) and competes with the decoder for the cache
  const uint32_t p0 = micros();
  if (d->iBpp == 8) {   // luma only: one byte per pixel
    const uint8_t *src = (const uint8_t *)d->pPixels;
    uint16_t *dst = gray_block;
    for (int j = 0; j < d->iHeight; j++) {
      const int row = (d->y + j) * DISP_W + d->x;
      for (int i = 0; i < d->iWidth; i++) {
        const uint16_t out = gray_lut[*src++];
        const int k = row + i;
        const bool face = face_in_decode && ((face_mask[k >> 3] >> (k & 7)) & 1);
        *dst++ = face && ((out >> 5) & 0x3F) <= FACE_BEHIND_GREEN ? FACE : out;
      }
    }
    const uint32_t p1 = micros();
    gfx.blit(d->x, d->y, d->iWidth, d->iHeight, gray_block);
    prof_pixel_us += p1 - p0;
    prof_blit_us += micros() - p1;
    return 1;
  }
  uint16_t *px = d->pPixels;
  if (!face_in_decode) {
    for (int n = d->iWidth * d->iHeight; n > 0; n--, px++) *px = (*px & BRIGHT_BITS) ? brighten(*px) : 0;
  } else {
    for (int j = 0; j < d->iHeight; j++) {
      const int row = (d->y + j) * DISP_W + d->x;
      for (int i = 0; i < d->iWidth; i++, px++) {
        const int k = row + i;
        const bool face = (face_mask[k >> 3] >> (k & 7)) & 1;
        const uint16_t c = *px;
        if (!(c & BRIGHT_BITS)) { *px = face ? FACE : 0; continue; }   // most of the picture: black
        const uint16_t out = brighten(c);
        *px = face && ((out >> 5) & 0x3F) <= FACE_BEHIND_GREEN ? FACE : out;
      }
    }
  }
  const uint32_t p1 = micros();
  gfx.blit(d->x, d->y, d->iWidth, d->iHeight, d->pPixels);
  prof_pixel_us += p1 - p0;
  prof_blit_us += micros() - p1;
  return 1;
}

// The cockpit instrument's printed face. DCS exports only the symbols, not these rings and ticks.
// Radii are fractions of the half-width: DCS's own ring radii (RWR_ALR67_MAIN_page.lua), checked
// against a photo of the cockpit RWR.
static void drawRwrFace();

// Draws the face once into a spare frame buffer and keeps it as face_mask, in screen coordinates (as jpegDraw sees
// them, before the 180 degree rotation that FbGfx applies)
static void buildFaceMask(uint16_t *scratch) {
  gfx.setBuffer(scratch);
  gfx.fillScreen(BLACK);
  drawRwrFace();
  memset(face_mask, 0, sizeof(face_mask));
  const int n = DISP_W * DISP_H;
  for (int i = 0; i < n; i++) {
    if (!scratch[i]) continue;
    const int k = ROTATE_180 ? n - 1 - i : i;
    face_mask[k >> 3] |= 1 << (k & 7);
  }
  gfx.fillScreen(BLACK);
}

static void drawRwrFace() {
  const float half = DISP_W / 2;
  for (const float r : {0.19f, 0.56f, 0.74f}) {
    const int px = lrintf(r * half);
    gfx.ring(CX, CY, px - 1, px + 1, FACE);
  }
  for (int deg = 0; deg < 360; deg += 30) {
    const float a = deg * DEG_TO_RAD, s = sinf(a), c = cosf(a);
    const float r0 = (deg % 90) ? 0.81f : 0.76f, r1 = 0.97f;   // long ticks at the cardinal points
    gfx.thickLine(CX + s * r0 * half, CY - c * r0 * half, CX + s * r1 * half, CY - c * r1 * half, 3, FACE);
  }
}

// One frame of the streamed cockpit RWR, centred, rotated like the rest of the display
static void drawVideoFrame(const uint8_t *data, size_t len) {
  uint32_t hash = 2166136261u;   // FNV-1a
  for (size_t i = 0; i < len; i++) hash = (hash ^ data[i]) * 16777619u;
  if (hash == last_video_hash) return;
  last_video_hash = hash;

  static uint32_t errors = 0;
  if (!jpeg.openRAM((uint8_t *)data, len, jpegDraw)) {
    if (errors++ % 50 == 0) Serial.printf("[video] open failed, error %d (%u so far)\n", jpeg.getLastError(), (unsigned)errors);
    return;
  }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  const uint32_t t0 = micros();
  LCD_waitForSpareFrameBuffer();
  const uint32_t t1 = micros();
  gfx.setBuffer(fb_back);
  const int w = jpeg.getWidth(), h = jpeg.getHeight();
  const bool full = w >= DISP_W && h >= DISP_H;
  face_in_decode = DRAW_RINGS && full;
  if (!full) gfx.fillScreen(BLACK);
  const bool ok = jpeg.decode(max(0, (DISP_W - w) / 2), max(0, (DISP_H - h) / 2), VIDEO_LUMA_ONLY ? JPEG_LUMA_ONLY : 0);
  if (!ok && errors++ % 50 == 0) {
    Serial.printf("[video] decode failed, error %d, %dx%d (%u so far)\n", jpeg.getLastError(), w, h, (unsigned)errors);
  }
  jpeg.close();
  if (!ok) return;   // a failed frame leaves the last good one on screen
  if (DRAW_RINGS && !full) {   // a smaller frame leaves a border jpegDraw never sees: draw the face the slow way
    gfx.setUnderlay(true);
    drawRwrFace();
    gfx.setUnderlay(false);
  }
  present();

  // Every 5 s: frames drawn, and the average wait for the panel and time spent drawing
  static uint32_t frames = 0, wait_us = 0, draw_us = 0, report_ms = 0;
  frames++;
  wait_us += t1 - t0;
  draw_us += micros() - t1;
  if (millis() - report_ms >= 5000) {
    static uint32_t replaced_before = 0;
    const uint32_t replaced = VideoStream::replaced;
    Serial.printf("[video] %u drawn, %u replaced unseen, avg wait %u us, draw %u us (pixels %u, copy %u), free heap %u\n",
                  (unsigned)frames, (unsigned)(replaced - replaced_before), (unsigned)(wait_us / frames),
                  (unsigned)(draw_us / frames), (unsigned)(prof_pixel_us / frames), (unsigned)(prof_blit_us / frames),
                  (unsigned)ESP.getFreeHeap());
    replaced_before = replaced;
    frames = wait_us = draw_us = prof_pixel_us = prof_blit_us = 0;
    report_ms = millis();
  }
}

// ────────────────────────────── Setup / loop ──────────────────────────────
static void renderTask(void *);

void setup() {
  Serial.begin(115200);
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Set_EXIO(EXIO_PIN8, Low);    // keep the board's buzzer off
  LCD_Init();
  Backlight_Init();
  Set_Backlight(BACKLIGHT);

  void *fb0 = nullptr, *fb1 = nullptr, *fb2 = nullptr;
  LCD_getFrameBuffers(&fb0, &fb1, &fb2);
  fb_front = (uint16_t *)fb0;  // the panel starts by showing its first frame buffer
  fb_back  = (uint16_t *)fb1;
  fb_spare = (uint16_t *)fb2;
  memset(fb0, 0, DISP_W * DISP_H * 2);
  if (DRAW_RINGS) buildFaceMask(fb_spare);   // not on screen yet
  buildGrayTable();

  xTaskCreatePinnedToCore(wifiTask, "rwrwifi", 8192, nullptr, 1, nullptr, 0);
  if (!VideoStream::begin()) Serial.println("[video] no PSRAM for frame buffers");
  Serial.printf("[boot] reset reason %d\n", (int)esp_reset_reason());   // 4 = brown-out, 1 = power-on
  // All drawing runs on core 0. The panel's bounce-buffer interrupt runs on this core (1), where LCD_Init was called,
  // and copies every frame out of PSRAM: shared with the decoder it took about half the core.
  xTaskCreatePinnedToCore(renderTask, "render", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  delay(1000);   // the render task does the work
}

static void renderStep();
static void renderTask(void *) {
  for (;;) renderStep();
}

static void renderStep() {
  static uint32_t last_video_ms = 0;
  static bool last_joining = true, drawn = false;
  static char last_ip[20] = "";

  const uint32_t video_ms = VideoStream::last_frame_ms;
  const uint32_t now = millis();   // after reading video_ms, so now - video_ms cannot go negative

  // A live video stream takes over the screen; the status screen below is the fallback
  if (video_ms && now - video_ms < STALE_MS) {
    const uint8_t *frame;
    size_t len;
    if (now - last_video_ms >= VIDEO_MIN_MS && VideoStream::take(&frame, &len)) {
      last_video_ms = now;
      drawVideoFrame(frame, len);
    }
    drawn = false;   // redraw the status screen as soon as the stream stops
    delay(1);
    return;
  }

  // Redraw only when what is shown changes: each redraw clears a frame buffer and swaps buffers under the panel
  const bool joining = !wifi_up;
  if (!drawn || joining != last_joining || strcmp(ip_text, last_ip) != 0) {
    drawStatus(joining);
    drawn = true;
    last_joining = joining;
    strlcpy(last_ip, ip_text, sizeof(last_ip));
  }
  delay(5);
}
