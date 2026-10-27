// Adafruit_GFX drawing straight into one of the ST7701 panel's RGB565 frame buffers,
// plus the thick shapes the scope needs (rings and wide lines).
#pragma once
#include <Adafruit_GFX.h>

class FbGfx : public Adafruit_GFX {
public:
  FbGfx(int16_t w, int16_t h, bool rotate180) : Adafruit_GFX(w, h), flip(rotate180) {}

  void setBuffer(uint16_t *fb) { buf = fb; }

  // While on, drawing skips pixels that already hold something bright, so a shape drawn over
  // a picture stays behind its green symbols. Black and JPEG noise near black count as empty.
  void setUnderlay(bool on) { under = on; }

  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if ((uint16_t)x >= WIDTH || (uint16_t)y >= HEIGHT) return;
    const size_t i = index(x, y);
    if (under && ((buf[i] >> 5) & 0x3F) > UNDERLAY_GREEN_MAX) return;
    buf[i] = c;
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    if ((uint16_t)y >= HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > WIDTH) w = WIDTH - x;
    if (w <= 0) return;
    if (under) {
      for (int i = 0; i < w; i++) drawPixel(x + i, y, c);
      return;
    }
    // Rotated 180, the run is still contiguous, just starting from the other end
    uint16_t *p = flip ? &buf[index(x + w - 1, y)] : &buf[index(x, y)];
    for (int i = 0; i < w; i++) p[i] = c;
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    for (int i = 0; i < h; i++) drawPixel(x, y + i, c);
  }

  void fillScreen(uint16_t c) override {
    const size_t n = (size_t)WIDTH * HEIGHT;
    if (c == 0) { memset(buf, 0, n * sizeof(uint16_t)); return; }
    for (size_t i = 0; i < n; i++) buf[i] = c;
  }

  // Filled annulus between radius ri and ro
  void ring(int cx, int cy, int ri, int ro, uint16_t c) {
    const int ro2 = ro * ro, ri2 = ri * ri;
    for (int dy = -ro; dy <= ro; dy++) {
      const int xo = (int)sqrtf((float)(ro2 - dy * dy));
      if (dy * dy >= ri2) {
        drawFastHLine(cx - xo, cy + dy, 2 * xo + 1, c);
      } else {
        const int xi = (int)sqrtf((float)(ri2 - dy * dy));
        drawFastHLine(cx - xo, cy + dy, xo - xi, c);
        drawFastHLine(cx + xi + 1, cy + dy, xo - xi, c);
      }
    }
  }

  // Line w pixels wide, with round ends so joined lines have no notches
  void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c) {
    const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    const int ax = lrintf(x0 + nx), ay = lrintf(y0 + ny), bx = lrintf(x0 - nx), by = lrintf(y0 - ny);
    const int cx = lrintf(x1 - nx), cy = lrintf(y1 - ny), ex = lrintf(x1 + nx), ey = lrintf(y1 + ny);
    fillTriangle(ax, ay, bx, by, cx, cy, c);
    fillTriangle(ax, ay, cx, cy, ex, ey, c);
    const int r = (int)(w / 2);
    if (r > 0) {
      fillCircle(lrintf(x0), lrintf(y0), r, c);
      fillCircle(lrintf(x1), lrintf(y1), r, c);
    }
  }

  // Copy a w x h block of pixels to (x, y), rotated like everything else
  void blit(int x, int y, int w, int h, const uint16_t *px) {
    if (!flip && x >= 0 && x + w <= WIDTH) {   // unrotated: each row is one contiguous copy
      for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if ((uint16_t)yy < HEIGHT) memcpy(&buf[(size_t)yy * WIDTH + x], &px[j * w], w * sizeof(uint16_t));
      }
      return;
    }
    for (int j = 0; j < h; j++) {
      const int yy = y + j;
      if ((uint16_t)yy >= HEIGHT) continue;
      for (int i = 0; i < w; i++) {
        const int xx = x + i;
        if ((uint16_t)xx < WIDTH) buf[index(xx, yy)] = px[j * w + i];
      }
    }
  }

  // Text centred on (x, y)
  void textCentered(const char *s, int x, int y, const GFXfont *font, uint16_t c) {
    setFont(font);
    int16_t x1, y1;
    uint16_t w, h;
    getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    setTextColor(c);
    setCursor(x - (int)w / 2 - x1, y - (int)h / 2 - y1);
    print(s);
  }

private:
  static constexpr uint8_t UNDERLAY_GREEN_MAX = 12;   // of 63 (RGB565 green)
  uint16_t *buf = nullptr;
  bool under = false;
  const bool flip;
  inline size_t index(int x, int y) const {
    return flip ? (size_t)(HEIGHT - 1 - y) * WIDTH + (WIDTH - 1 - x) : (size_t)y * WIDTH + x;
  }
};
