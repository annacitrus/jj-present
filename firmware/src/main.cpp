#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

#include "secrets.h"

static const uint16_t FRAME_W = 320;
static const uint16_t FRAME_H = 240;
static const size_t ROW_BYTES = FRAME_W * 2; // RGB565, 2 bytes/pixel
static const size_t IMAGE_BYTES = ROW_BYTES * FRAME_H;
static const unsigned long POLL_INTERVAL_MS = 5000;

TFT_eSPI tft = TFT_eSPI();

int lastVersion = -1;
unsigned long lastPollAt = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());
}

// Returns the version number from GET /api/version, or -1 on failure.
int fetchVersion() {
  WiFiClientSecure client;
  client.setInsecure(); // skip TLS cert validation for simplicity

  HTTPClient http;
  String url = String(SERVER_HOST) + "/api/version";
  if (!http.begin(client, url)) return -1;

  int code = http.GET();
  int version = -1;
  if (code == 200) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      version = doc["version"] | -1;
    }
  } else {
    Serial.printf("GET /api/version failed, code=%d\n", code);
  }
  http.end();
  return version;
}

// Fetches /api/image and streams it straight to the display row by row.
bool fetchAndDrawImage() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(SERVER_HOST) + "/api/image";
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("GET /api/image failed, code=%d\n", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength != (int)IMAGE_BYTES) {
    Serial.printf("Unexpected image size: %d (expected %u)\n", contentLength, (unsigned)IMAGE_BYTES);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t rowBuf[ROW_BYTES];

  tft.startWrite();
  tft.setAddrWindow(0, 0, FRAME_W, FRAME_H);

  bool ok = true;
  for (uint16_t row = 0; row < FRAME_H; row++) {
    size_t got = 0;
    unsigned long rowStart = millis();
    while (got < ROW_BYTES) {
      int avail = stream->available();
      if (avail > 0) {
        int n = stream->read(rowBuf + got, ROW_BYTES - got);
        if (n > 0) {
          got += n;
          rowStart = millis(); // reset timeout on progress
        }
      } else {
        if (millis() - rowStart > 8000) { ok = false; break; } // row read timeout
        delay(1);
      }
    }
    if (!ok) break;
    tft.pushColors((uint16_t *)rowBuf, FRAME_W);
  }

  tft.endWrite();
  http.end();

  if (!ok) {
    Serial.println("Image read timed out partway through a row.");
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  tft.init();
  tft.setRotation(1); // landscape; try 3 if the image appears upside down or mirrored
  tft.setSwapBytes(true); // our bytes are big-endian RGB565
  tft.fillScreen(TFT_BLACK);

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = millis();
    int version = fetchVersion();
    if (version >= 0 && version != lastVersion) {
      Serial.printf("New version %d (had %d), fetching image...\n", version, lastVersion);
      if (fetchAndDrawImage()) {
        lastVersion = version;
        Serial.println("Frame updated.");
      } else {
        Serial.println("Frame update failed, will retry next poll.");
      }
    }
  }
}
