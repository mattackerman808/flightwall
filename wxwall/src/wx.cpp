#include "wx.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "settings.h"

WxState wx;

// ---------- helpers ----------

static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

time_t parseIsoUtc(const char* s) {
  if (!s) return 0;
  int Y, M, D, h, m, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
  return (time_t)(daysFromCivil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec);
}

Cat catFromVisCeil(bool hasVis, float vis, int ceil) {
  bool c = ceil != CEIL_NONE;
  if ((hasVis && vis < 1) || (c && ceil < 500)) return CAT_LIFR;
  if ((hasVis && vis < 3) || (c && ceil < 1000)) return CAT_IFR;
  if ((hasVis && vis <= 5) || (c && ceil <= 3000)) return CAT_MVFR;
  if (!hasVis && !c) return CAT_UNK;
  return CAT_VFR;
}

const char* catName(Cat c) {
  switch (c) {
    case CAT_VFR: return "VFR";
    case CAT_MVFR: return "MVFR";
    case CAT_IFR: return "IFR";
    case CAT_LIFR: return "LIFR";
    default: return "---";
  }
}

static Cat catFromString(const char* s) {
  if (!s) return CAT_UNK;
  if (!strcmp(s, "VFR")) return CAT_VFR;
  if (!strcmp(s, "MVFR")) return CAT_MVFR;
  if (!strcmp(s, "IFR")) return CAT_IFR;
  if (!strcmp(s, "LIFR")) return CAT_LIFR;
  return CAT_UNK;
}

static int windDir(JsonVariantConst v) {
  if (v.is<int>()) return v.as<int>();
  if (v.is<const char*>() && !strcmp(v.as<const char*>(), "VRB")) return WIND_VRB;
  return WIND_NONE;
}

// "6+" -> 6, 3 -> 3, "1/2" -> 0.5
static bool parseVis(JsonVariantConst v, float& out) {
  if (v.is<float>() || v.is<int>()) {
    out = v.as<float>();
    return true;
  }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    int a, b;
    if (sscanf(s, "%d/%d", &a, &b) == 2 && b) {
      out = (float)a / b;
      return true;
    }
    out = atof(s);
    return out > 0 || s[0] == '0';
  }
  return false;
}

static bool httpGetJson(const String& url, JsonDocument& doc, const JsonDocument* filter, String& err,
                        bool tachbase = true) {
  WiFiClientSecure tls;
  tls.setInsecure();  // public weather data; no secrets are sent
  WiFiClient plain;
  HTTPClient http;
  http.setUserAgent("WxWall/" FW_VERSION " (ESP32-S3)");
  http.setTimeout(15000);
  http.useHTTP10(true);  // no chunked encoding, so we can stream-parse
  bool ok = url.startsWith("https://") ? http.begin(tls, url) : http.begin(plain, url);
  if (!ok) {
    err = "bad URL";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = code < 0 ? HTTPClient::errorToString(code) : ("HTTP " + String(code));
    http.end();
    return false;
  }
  DeserializationError de = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                   : deserializeJson(doc, http.getStream());
  http.end();
  if (de) {
    err = String("JSON: ") + de.c_str();
    return false;
  }
  if (tachbase && !(doc["success"] | false)) {
    err = doc["error"] | "API error";
    return false;
  }
  if (!tachbase && (doc["error"] | false)) {
    err = doc["reason"] | "API error";
    return false;
  }
  err = "";
  return true;
}

const Metar* primaryMetar() { return wx.nArea && wx.area[0].valid ? &wx.area[0] : nullptr; }

// ---------- METARs ----------

bool fetchMetars() {
  String ids = settings.airports;
  if (!ids.length()) {
    wx.nArea = 0;
    wx.metarErr = "";
    return true;
  }
  JsonDocument filter;
  filter["success"] = true;
  filter["error"] = true;
  JsonObject fp = filter["features"][0]["properties"].to<JsonObject>();
  for (const char* k : {"icao", "flight_category", "raw_text", "temperature_c", "dewpoint_c", "wind_dir_degrees",
                        "wind_speed_kt", "wind_gust_kt", "visibility_sm", "altimeter_inhg", "ceiling_agl", "obs_time", "wx"})
    fp[k] = true;

  JsonDocument doc;
  String err;
  if (!httpGetJson(settings.api + "/weather?action=get_stations&ids=" + ids, doc, &filter, err)) {
    wx.metarErr = err;
    return false;
  }

  // Keep the order of the configured list; mark missing airports invalid.
  Metar out[AREA_MAX];
  int n = 0;
  int start = 0;
  while (start < (int)ids.length() && n < AREA_MAX) {
    int comma = ids.indexOf(',', start);
    if (comma < 0) comma = ids.length();
    String id = ids.substring(start, comma);
    start = comma + 1;
    Metar& m = out[n++];
    strlcpy(m.icao, id.c_str(), sizeof(m.icao));
    for (JsonObjectConst f : doc["features"].as<JsonArrayConst>()) {
      JsonObjectConst p = f["properties"];
      if (id != (p["icao"] | "")) continue;
      m.valid = true;
      m.cat = catFromString(p["flight_category"] | "");
      m.hasTemp = p["temperature_c"].is<float>();
      m.tempC = p["temperature_c"] | 0.0f;
      m.dewC = p["dewpoint_c"] | 0.0f;
      m.wdir = windDir(p["wind_dir_degrees"]);
      m.wspd = p["wind_speed_kt"] | -1;
      m.wgst = p["wind_gust_kt"] | 0;
      m.hasVis = parseVis(p["visibility_sm"], m.vis);
      m.alt = p["altimeter_inhg"] | 0.0f;
      m.ceil = p["ceiling_agl"].is<int>() ? p["ceiling_agl"].as<int>() : CEIL_NONE;
      strlcpy(m.wx, p["wx"] | "", sizeof(m.wx));
      strlcpy(m.raw, p["raw_text"] | "", sizeof(m.raw));
      m.obs = parseIsoUtc(p["obs_time"] | "");
      if (m.cat == CAT_UNK) m.cat = catFromVisCeil(m.hasVis, m.vis, m.ceil);
      break;
    }
  }
  memcpy(wx.area, out, sizeof(out));
  wx.nArea = n;
  wx.metarErr = "";
  wx.metarAt = time(nullptr);
  wx.version++;
  return true;
}

// ---------- TAF ----------

static int tafCeiling(JsonObjectConst f, bool& stated) {
  stated = false;
  int ceil = CEIL_NONE;
  if (f["vertVis"].is<int>()) {
    int vv = f["vertVis"].as<int>();
    if (vv > 0 && vv < 50) vv *= 100;  // hundreds of feet
    stated = true;
    ceil = vv;
  }
  JsonArrayConst clouds = f["clouds"];
  if (!clouds.isNull() && clouds.size()) {
    stated = true;
    for (JsonObjectConst c : clouds) {
      const char* cover = c["cover"] | "";
      if (strcmp(cover, "BKN") && strcmp(cover, "OVC") && strcmp(cover, "OVX")) continue;
      int base = c["base"] | -1;
      if (base >= 0 && (ceil == CEIL_NONE || base < ceil)) ceil = base;
    }
  }
  return ceil;
}

bool fetchTaf() {
  String icao = firstAirport();
  if (!icao.length()) {
    wx.taf = Taf();
    wx.tafErr = "";
    return true;
  }
  JsonDocument filter;
  filter["success"] = true;
  filter["error"] = true;
  filter["taf_source"] = true;
  filter["taf_distance_nm"] = true;
  JsonObject tp = filter["taf"]["parsed"].to<JsonObject>();
  tp["icaoId"] = true;
  tp["validTimeTo"] = true;
  JsonObject f = tp["fcsts"][0].to<JsonObject>();
  for (const char* k : {"timeFrom", "timeTo", "fcstChange", "wdir", "wspd", "wgst", "visib", "vertVis"}) f[k] = true;
  f["clouds"][0]["cover"] = true;
  f["clouds"][0]["base"] = true;

  JsonDocument doc;
  String err;
  if (!httpGetJson(settings.api + "/airport?action=get_weather&icao=" + icao, doc, &filter, err)) {
    wx.tafErr = err;
    return false;
  }
  JsonObjectConst parsed = doc["taf"]["parsed"];
  Taf t;
  if (parsed.isNull()) {
    wx.taf = t;  // valid=false
    wx.tafErr = "No TAF nearby";
    wx.tafAt = time(nullptr);
    wx.version++;
    return true;
  }
  strlcpy(t.icao, parsed["icaoId"] | "", sizeof(t.icao));
  t.distNm = doc["taf_distance_nm"] | 0.0f;
  t.validTo = parsed["validTimeTo"] | 0;

  // Walk the groups, carrying forward anything a BECMG/TEMPO group doesn't restate.
  bool hasVis = false;
  float vis = 0;
  int ceil = CEIL_NONE;
  int wdir = WIND_NONE, wspd = -1, wgst = 0;
  for (JsonObjectConst g : parsed["fcsts"].as<JsonArrayConst>()) {
    if (t.n >= TAF_MAX) break;
    const char* chg = g["fcstChange"] | "";
    bool base = !(strcmp(chg, "TEMPO") == 0 || strncmp(chg, "PROB", 4) == 0);
    float gv;
    bool gHasVis = parseVis(g["visib"], gv);
    bool ceilStated;
    int gCeil = tafCeiling(g, ceilStated);
    int gDir = windDir(g["wdir"]);
    bool windStated = g["wspd"].is<int>();

    TafPeriod& p = t.p[t.n++];
    p.from = g["timeFrom"] | 0;
    p.to = g["timeTo"] | 0;
    p.base = base;
    bool pv = gHasVis ? true : hasVis;
    float v = gHasVis ? gv : vis;
    int c = ceilStated ? gCeil : ceil;
    p.cat = catFromVisCeil(pv, v, c);
    p.wdir = windStated ? gDir : wdir;
    p.wspd = windStated ? (g["wspd"] | -1) : wspd;
    p.wgst = windStated ? (g["wgst"] | 0) : wgst;
    if (base) {
      hasVis = pv;
      vis = v;
      ceil = c;
      wdir = p.wdir;
      wspd = p.wspd;
      wgst = p.wgst;
    }
  }
  t.valid = t.n > 0;
  wx.taf = t;
  wx.tafErr = "";
  wx.tafAt = time(nullptr);
  wx.version++;
  return true;
}

// ---------- Outlook (Open-Meteo daily forecast) ----------

// WMO weather interpretation codes -> icon
static Icon iconForWmo(int code, bool day) {
  if (code == 0) return day ? IC_SUN : IC_MOON;
  if (code <= 2) return day ? IC_PARTLY_DAY : IC_PARTLY_NIGHT;
  if (code == 3) return IC_CLOUD;
  if (code == 45 || code == 48) return IC_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return IC_SNOW;
  if (code >= 95) return IC_STORM;
  if (code >= 51) return IC_RAIN;
  return IC_CLOUD;
}

bool fetchOutlook() {
  if (!settings.hasForecastLoc()) {
    wx.outlook = Outlook();
    wx.outlookErr = "";
    return true;
  }
  char url[400];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&current=temperature_2m,weather_code,is_day&timezone=auto&forecast_days=%d&temperature_unit=%s",
           settings.fcLat, settings.fcLon, OUTLOOK_DAYS, settings.celsius ? "celsius" : "fahrenheit");
  JsonDocument doc;
  String err;
  if (!httpGetJson(url, doc, nullptr, err, false)) {
    wx.outlookErr = err;
    return false;
  }
  Outlook o;
  JsonObjectConst cur = doc["current"];
  if (cur["temperature_2m"].is<float>()) {
    o.hasNow = true;
    o.nowTemp = lroundf(cur["temperature_2m"].as<float>());
    o.nowIcon = iconForWmo(cur["weather_code"] | 3, (cur["is_day"] | 1) == 1);
  }
  JsonObjectConst d = doc["daily"];
  JsonArrayConst dates = d["time"];
  for (size_t i = 0; i < dates.size() && o.n < OUTLOOK_DAYS; i++) {
    int Y, M, D;
    if (sscanf(dates[i] | "", "%d-%d-%d", &Y, &M, &D) != 3) continue;
    OutlookDay& od = o.day[o.n++];
    od.y = Y;
    od.m = M;
    od.d = D;
    od.wday = (uint8_t)(((daysFromCivil(Y, M, D) % 7) + 11) % 7);  // 1970-01-01 was a Thursday
    od.hi = lroundf(d["temperature_2m_max"][i] | 0.0f);
    od.lo = lroundf(d["temperature_2m_min"][i] | 0.0f);
    od.pop = d["precipitation_probability_max"][i].is<int>() ? d["precipitation_probability_max"][i].as<int>() : -1;
    od.icon = iconForWmo(d["weather_code"][i] | 3, true);
  }
  o.valid = o.n > 0;
  wx.outlook = o;
  wx.outlookErr = o.valid ? "" : "No forecast data";
  wx.outlookAt = time(nullptr);
  wx.version++;
  return true;
}
