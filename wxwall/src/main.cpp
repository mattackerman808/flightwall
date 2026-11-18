// WxWall — aviation weather for the FlightWall Mini (Huidu HD-WF2 + 64x32 HUB75).
// Data: PiAware/adsb.lol + AeroAPI/adsbdb (flights), Tachbase API (METAR/TAF), Open-Meteo (forecast). Config: web UI at http://wxwall.local
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "board.h"
#include "flights.h"
#include "render.h"
#include "settings.h"
#include "web.h"
#include "wx.h"

static const uint32_t METAR_EVERY = 5 * 60 * 1000UL;
static const uint32_t TAF_EVERY = 15 * 60 * 1000UL;
static const uint32_t OUTLOOK_EVERY = 30 * 60 * 1000UL;
static const uint32_t FLIGHTS_EVERY = 10 * 1000UL;  // PiAware is local; adsb.lol is throttled to 30s inside
static const uint32_t ROUTES_EVERY = 2 * 1000UL;
static const uint32_t RETRY_MIN = 60 * 1000UL, RETRY_MAX = 10 * 60 * 1000UL;

struct Job {
  bool (*fn)();
  uint32_t every, next, retry;
};
static Job jobs[] = {{fetchFlights, FLIGHTS_EVERY, 0, RETRY_MIN},
                     {fetchRoutes, ROUTES_EVERY, 0, RETRY_MIN},
                     {fetchMetars, METAR_EVERY, 0, RETRY_MIN},
                     {fetchTaf, TAF_EVERY, 0, RETRY_MIN},
                     {fetchOutlook, OUTLOOK_EVERY, 0, RETRY_MIN}};

static Page pages[32];
static int nPages = 0, pageIdx = 0;
static uint32_t pageStartMs = 0, lastDrawMs = 0, splashUntil = 0;
static volatile bool settingsDirty = false, refreshRequested = false;
static int previewBright = -1;
static uint32_t previewUntil = 0;
static int appliedBright = -1;

// ---------- hooks for web.cpp ----------
void appSettingsChanged() { settingsDirty = true; }
void appRequestRefresh() { refreshRequested = true; }
void appPreviewBrightness(uint8_t b) {
  previewBright = b;
  previewUntil = millis() + 5000;
}
void appForgetWifi() {
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

static bool timeValid() { return time(nullptr) > 1700000000; }

static void applyTimeZone() { configTzTime(settings.tz.c_str(), "pool.ntp.org", "time.google.com"); }

static uint8_t targetBrightness() {
  if (previewBright >= 0 && (int32_t)(millis() - previewUntil) < 0) return previewBright;
  if (settings.nightStart != settings.nightEnd && timeValid()) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    int h = lt.tm_hour, s = settings.nightStart, e = settings.nightEnd;
    bool night = s < e ? (h >= s && h < e) : (h >= s || h < e);
    if (night) return settings.nightBright;
  }
  return settings.bright;
}

static void scheduleAllNow() {
  for (Job& j : jobs) {
    j.next = millis();
    j.retry = RETRY_MIN;
  }
}

static void runOneDueJob() {
  uint32_t now = millis();
  for (Job& j : jobs) {
    if ((int32_t)(now - j.next) < 0) continue;
    bool ok = j.fn();
    if (ok) {
      j.next = millis() + j.every;
      j.retry = RETRY_MIN;
    } else {
      j.next = millis() + (j.every < RETRY_MIN ? j.every * 3 : j.retry);
      j.retry = min(j.retry * 2, RETRY_MAX);
    }
    return;  // one network call per loop pass keeps the web UI responsive
  }
}

// ---------- button: short press = next page, hold 2s = refresh ----------
static void pollButton() {
  static bool last = HIGH;
  static uint32_t downAt = 0;
  static bool held = false;
  bool now = digitalRead(PUSH_BUTTON_PIN);
  if (now == LOW && last == HIGH) {
    downAt = millis();
    held = false;
  } else if (now == LOW && !held && millis() - downAt > 2000) {
    held = true;
    refreshRequested = true;
    drawMessage("REFRESHING", "FETCHING WEATHER...");
    lastDrawMs = millis();
  } else if (now == HIGH && last == LOW && !held && millis() - downAt > 40) {
    if (nPages) pageIdx = (pageIdx + 1) % nPages;
    pageStartMs = millis();
    lastDrawMs = 0;
    splashUntil = 0;
  }
  last = now;
}

void setup() {
  Serial.begin(115200);
  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
  settingsLoad();
  if (!displayBegin()) Serial.println("HUB75 init failed");
  drawMessage("WXWALL " FW_VERSION, "AVIATION WEATHER", "", "CONNECTING WIFI...");

  WiFiManager wm;
  wm.setHostname("wxwall");
  wm.setConnectTimeout(20);
  wm.setAPCallback([](WiFiManager*) {
    drawMessage("WIFI SETUP", "JOIN NETWORK:", "WXWALL-SETUP", "THEN OPEN", "192.168.4.1");
  });
  if (!wm.autoConnect("WxWall-Setup")) ESP.restart();
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  applyTimeZone();
  webBegin();

  String ip = WiFi.localIP().toString();
  drawMessage("CONNECTED", "OPEN IN BROWSER:", ip.c_str(), "OR WXWALL.LOCAL");
  splashUntil = millis() + 15000;
  Serial.printf("WxWall %s at http://%s/\n", FW_VERSION, ip.c_str());

  uint32_t t0 = millis();
  while (!timeValid() && millis() - t0 < 8000) {
    webLoop();
    delay(50);
  }
  scheduleAllNow();
}

void loop() {
  webLoop();
  pollButton();

  if (settingsDirty) {
    settingsDirty = false;
    applyTimeZone();
    flightsResetCenter();
    scheduleAllNow();
    pageIdx = 0;
    pageStartMs = millis();
    lastDrawMs = 0;
  }
  if (refreshRequested) {
    refreshRequested = false;
    scheduleAllNow();
  }

  if (WiFi.status() == WL_CONNECTED) runOneDueJob();

  uint8_t b = targetBrightness();
  if (b != appliedBright) {
    displaySetBrightness(b);
    appliedBright = b;
  }

  uint32_t now = millis();
  if ((int32_t)(now - splashUntil) < 0) return;  // keep the IP splash up

  if (!settings.configured()) {
    if (now - lastDrawMs > 2000) {
      String ip = WiFi.localIP().toString();
      drawMessage("SET UP AIRPORTS", "OPEN IN BROWSER:", ip.c_str(), "OR WXWALL.LOCAL");
      lastDrawMs = now;
    }
    return;
  }

  nPages = buildPages(pages, 32);
  if (pageIdx >= nPages) pageIdx = 0;
  if (now - pageStartMs > (uint32_t)settings.pageSecs * 1000) {
    pageIdx = (pageIdx + 1) % nPages;
    pageStartMs = now;
    lastDrawMs = 0;
  }
  if (lastDrawMs == 0 || now - lastDrawMs >= 1000) {
    drawPage(pages[pageIdx]);
    lastDrawMs = now ? now : 1;
  }
}
