#include "web.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "flights.h"
#include "render.h"
#include "settings.h"
#include "webpage.h"
#include "wx.h"

static WebServer server(80);
static String updateErr;

static void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static long ageSecs(time_t at) { return at ? (long)(time(nullptr) - at) : -1; }

static void handleState() {
  JsonDocument doc;
  settingsToJson(doc["settings"].to<JsonObject>());
  const Metar* m = primaryMetar();
  if (m) {
    JsonObject h = doc["primary"].to<JsonObject>();
    h["icao"] = m->icao;
    h["cat"] = catName(m->cat);
    h["raw"] = m->raw;
  }
  doc["metarAge"] = ageSecs(wx.metarAt);
  doc["tafAge"] = ageSecs(wx.tafAt);
  doc["outlookAge"] = ageSecs(wx.outlookAt);
  doc["metarErr"] = wx.metarErr;
  doc["tafErr"] = wx.tafErr;
  doc["outlookErr"] = wx.outlookErr;
  JsonObject fl = doc["flights"].to<JsonObject>();
  fl["count"] = flights.n;
  fl["age"] = ageSecs(flights.fetchedAt);
  fl["err"] = flights.err;
  fl["center"] = flights.centerSource;
  fl["aeroToday"] = flights.aeroToday;
  fl["aeroMonth"] = flights.aeroMonth;
  fl["aeroAllowedToday"] = flights.aeroAllowedToday;
  fl["aeroErr"] = flights.aeroErr;
  doc["ip"] = WiFi.localIP().toString();
  doc["version"] = FW_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  sendJson(200, doc);
}

static void handleSettings() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) {
    JsonDocument e;
    e["error"] = "Invalid JSON";
    sendJson(400, e);
    return;
  }
  String err;
  if (!settingsFromJson(in.as<JsonObjectConst>(), err)) {
    JsonDocument e;
    e["error"] = err;
    sendJson(400, e);
    return;
  }
  settingsSave();
  appSettingsChanged();
  JsonDocument out;
  settingsToJson(out["settings"].to<JsonObject>());
  sendJson(200, out);
}

void webBegin() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/flights", HTTP_GET, [] {
    JsonDocument doc;
    flightsToJson(doc.to<JsonArray>());
    sendJson(200, doc);
  });
  server.on("/api/refresh", HTTP_POST, [] {
    appRequestRefresh();
    server.send(204);
  });
  server.on("/api/brightness", HTTP_POST, [] {
    appPreviewBrightness(constrain(server.arg("v").toInt(), 1, 255));
    server.send(204);
  });
  server.on("/api/restart", HTTP_POST, [] {
    server.send(204);
    delay(300);
    ESP.restart();
  });
  server.on("/api/forget-wifi", HTTP_POST, [] {
    server.send(204);
    delay(300);
    appForgetWifi();
  });
  server.on(
      "/update", HTTP_POST,
      [] {
        bool ok = !Update.hasError() && updateErr.isEmpty();
        server.sendHeader("Connection", "close");
        server.send(ok ? 200 : 500, "text/plain", ok ? "Update OK — restarting" : "Update failed: " + updateErr);
        if (ok) {
          delay(500);
          ESP.restart();
        }
      },
      [] {
        HTTPUpload& up = server.upload();
        if (up.status == UPLOAD_FILE_START) {
          updateErr = "";
          drawMessage("UPDATING", "FIRMWARE...", "DO NOT UNPLUG");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) updateErr = Update.errorString();
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (updateErr.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
            updateErr = Update.errorString();
        } else if (up.status == UPLOAD_FILE_END) {
          if (updateErr.isEmpty() && !Update.end(true)) updateErr = Update.errorString();
        } else if (up.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          updateErr = "upload aborted";
        }
      });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
  if (MDNS.begin("wxwall")) MDNS.addService("http", "tcp", 80);
}

void webLoop() { server.handleClient(); }
