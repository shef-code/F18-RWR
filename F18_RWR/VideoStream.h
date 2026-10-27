// Receives the DCS RWR viewport as a stream of JPEG frames over TCP (receiver/rwr_stream.bat).
//
// The PC connects and sends back-to-back JPEGs (ffmpeg's mjpeg output). Each frame is cut out
// of the byte stream at its start (FF D8) and end (FF D9) markers; those byte pairs cannot occur
// inside the compressed data, which escapes every FF.
//
// Three PSRAM buffers so the network never waits for the decoder: the task fills `rx`, then swaps
// it with `ready`; loop() swaps `ready` with `busy` and decodes `busy` at its own pace.
#pragma once

#include <WiFi.h>
#include <utility>

namespace VideoStream {

constexpr uint16_t PORT      = 7791;
constexpr size_t   MAX_JPEG  = 96 * 1024;   // a 480x480 frame is typically 20-40 KB
constexpr uint32_t IDLE_MS   = 3000;        // a connection silent this long is dropped

static uint8_t *rx = nullptr, *ready = nullptr, *busy = nullptr;
static size_t ready_len = 0;
static bool fresh = false;
static volatile uint32_t last_frame_ms = 0;   // 0 until the first frame
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t frames_in = 0;
static volatile uint32_t replaced = 0;   // frames overwritten by a newer one before the screen took them
static void publish(size_t len) {
  if (frames_in++ % 100 == 0) Serial.printf("[video] frame %u, %u bytes\n", (unsigned)frames_in, (unsigned)len);
  portENTER_CRITICAL(&lock);
  if (fresh) replaced++;
  std::swap(rx, ready);
  ready_len = len;
  fresh = true;
  last_frame_ms = millis();
  portEXIT_CRITICAL(&lock);
}

// Newest complete frame, if one arrived since the last call. The caller may use it until it calls again.
static bool take(const uint8_t **data, size_t *len) {
  bool got = false;
  portENTER_CRITICAL(&lock);
  if (fresh) {
    std::swap(ready, busy);
    *len = ready_len;
    fresh = false;
    got = true;
  }
  portEXIT_CRITICAL(&lock);
  *data = busy;
  return got;
}

static void receive(WiFiClient &client) {
  static uint8_t chunk[4096];
  size_t len = 0;
  bool in_frame = false;
  uint8_t prev = 0;
  uint32_t last_rx = millis();
  while (client.connected()) {
    const int n = client.read(chunk, sizeof(chunk));
    if (n <= 0) {
      if (millis() - last_rx > IDLE_MS) break;
      delay(2);
      continue;
    }
    last_rx = millis();
    for (int i = 0; i < n; i++) {
      const uint8_t b = chunk[i];
      if (!in_frame) {
        if (prev == 0xFF && b == 0xD8) {
          rx[0] = 0xFF;
          rx[1] = 0xD8;
          len = 2;
          in_frame = true;
        }
      } else if (len >= MAX_JPEG) {
        in_frame = false;            // too big: skip to the next frame
      } else {
        rx[len++] = b;
        if (prev == 0xFF && b == 0xD9) {
          publish(len);
          in_frame = false;
        }
      }
      prev = b;
    }
  }
}

static void task(void *) {
  static WiFiServer server(PORT);
  bool listening = false;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      delay(250);
      continue;
    }
    if (!listening) {
      server.begin();
      listening = true;
      Serial.printf("[video] listening on TCP %u\n", PORT);
    }
    WiFiClient client = server.accept();
    if (!client) {
      delay(20);
      continue;
    }
    client.setNoDelay(true);
    Serial.printf("[video] stream from %s\n", client.remoteIP().toString().c_str());
    receive(client);
    client.stop();
    Serial.println("[video] stream ended");
  }
}

static bool begin() {
  rx    = (uint8_t *)ps_malloc(MAX_JPEG);
  ready = (uint8_t *)ps_malloc(MAX_JPEG);
  busy  = (uint8_t *)ps_malloc(MAX_JPEG);
  if (!rx || !ready || !busy) return false;
  xTaskCreatePinnedToCore(task, "rwrvideo", 6144, nullptr, 1, nullptr, 0);
  return true;
}

}  // namespace VideoStream
