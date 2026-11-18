#include "flights.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include "assets_airlines.h"
#include "settings.h"

FlightsState flights;

static const int CACHE_SIZE = 32;
static RouteInfo cache[CACHE_SIZE];
static int cacheNext = 0;

// ---------- AeroAPI budget ----------
// Usage is persisted so restarts don't reset it. Each /flights/{ident} call with max_pages=1 is one
// billed result set. Calls are paced across the month and kept under the Personal tier's 10/min.
static const uint32_t AERO_MIN_GAP_MS = 8000;
static uint32_t lastAeroMs = 0;
static int usageMonth = -1, usageDay = -1;  // yyyymm, yday
static uint16_t monthAtDayStart = 0;
static bool usageLoaded = false;

static void usageSave() {
  Preferences p;
  p.begin("wxusage", false);
  p.putInt("month", usageMonth);
  p.putInt("day", usageDay);
  p.putUShort("mcount", flights.aeroMonth);
  p.putUShort("dcount", flights.aeroToday);
  p.putUShort("mstart", monthAtDayStart);
  p.end();
}

// Rolls day/month counters and recomputes today's allowance. Needs valid time.
static bool usageTick() {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  int ym = (lt.tm_year + 1900) * 100 + lt.tm_mon + 1;
  if (!usageLoaded) {
    Preferences p;
    p.begin("wxusage", true);
    usageMonth = p.getInt("month", -1);
    usageDay = p.getInt("day", -1);
    flights.aeroMonth = p.getUShort("mcount", 0);
    flights.aeroToday = p.getUShort("dcount", 0);
    monthAtDayStart = p.getUShort("mstart", 0);
    p.end();
    usageLoaded = true;
  }
  bool changed = false;
  if (ym != usageMonth) {
    usageMonth = ym;
    flights.aeroMonth = 0;
    usageDay = -1;
    changed = true;
  }
  if (lt.tm_yday != usageDay) {
    usageDay = lt.tm_yday;
    flights.aeroToday = 0;
    monthAtDayStart = flights.aeroMonth;
    changed = true;
  }
  static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y = lt.tm_year + 1900;
  int dim = DAYS[lt.tm_mon] + (lt.tm_mon == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0));
  int daysLeft = dim - lt.tm_mday + 1;
  int remaining = max(0, (int)settings.aeroBudget - (int)monthAtDayStart);
  flights.aeroAllowedToday = remaining / daysLeft;
  if (changed) usageSave();
  return true;
}

static bool aeroAllowed() {
  if (!settings.aeroKey.length() || !usageTick()) return false;
  if (lastAeroMs && millis() - lastAeroMs < AERO_MIN_GAP_MS) return false;
  return flights.aeroMonth < settings.aeroBudget && flights.aeroToday < flights.aeroAllowedToday;
}

static void aeroCounted() {
  lastAeroMs = millis();
  flights.aeroToday++;
  flights.aeroMonth++;
  usageSave();
}

// ---------- geo ----------
static double rad(double d) { return d * M_PI / 180.0; }

static double distNm(double la1, double lo1, double la2, double lo2) {
  double dLat = rad(la2 - la1), dLon = rad(lo2 - lo1);
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(rad(la1)) * cos(rad(la2)) * sin(dLon / 2) * sin(dLon / 2);
  return 3440.065 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

static int bearingDeg(double la1, double lo1, double la2, double lo2) {
  double y = sin(rad(lo2 - lo1)) * cos(rad(la2));
  double x = cos(rad(la1)) * sin(rad(la2)) - sin(rad(la1)) * cos(rad(la2)) * cos(rad(lo2 - lo1));
  return ((int)lround(atan2(y, x) * 180.0 / M_PI) + 360) % 360;
}

// ---------- HTTP ----------
struct Http {
  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;
};

static int httpGet(Http& h, const String& url, const char* apiKey = nullptr) {
  h.tls.setInsecure();
  h.http.setUserAgent("WxWall/" FW_VERSION " (ESP32-S3)");
  h.http.setTimeout(12000);
  h.http.useHTTP10(true);
  bool ok = url.startsWith("https://") ? h.http.begin(h.tls, url) : h.http.begin(h.plain, url);
  if (!ok) return -100;
  if (apiKey) h.http.addHeader("x-apikey", apiKey);
  return h.http.GET();
}

static String httpErr(int code) {
  if (code == -100) return "bad URL";
  return code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
}

static bool getJson(const String& url, JsonDocument& doc, const JsonDocument* filter, String& err,
                    const char* apiKey = nullptr) {
  Http h;
  int code = httpGet(h, url, apiKey);
  if (code != 200) {
    err = httpErr(code);
    h.http.end();
    return false;
  }
  DeserializationError de = filter ? deserializeJson(doc, h.http.getStream(), DeserializationOption::Filter(*filter))
                                   : deserializeJson(doc, h.http.getStream());
  h.http.end();
  if (de) {
    err = String("JSON: ") + de.c_str();
    return false;
  }
  err = "";
  return true;
}

// ---------- center ----------
void flightsResetCenter() {
  flights.hasCenter = false;
  flights.n = 0;
}

static bool resolveCenter() {
  if (flights.hasCenter) return true;
  if (settings.adsbUrl.length()) {
    // PiAware/dump1090 publishes receiver.json next to aircraft.json.
    String u = settings.adsbUrl;
    u = u.substring(0, u.lastIndexOf('/') + 1) + "receiver.json";
    JsonDocument d;
    String err;
    if (getJson(u, d, nullptr, err) && d["lat"].is<double>() && d["lon"].is<double>()) {
      flights.ctrLat = d["lat"];
      flights.ctrLon = d["lon"];
      flights.centerSource = "receiver";
      flights.hasCenter = true;
      return true;
    }
  }
  if (settings.hasForecastLoc()) {
    flights.ctrLat = settings.fcLat;
    flights.ctrLon = settings.fcLon;
    flights.centerSource = "forecast location";
    flights.hasCenter = true;
    return true;
  }
  return false;
}

// ---------- positions ----------

static void trimCopy(char* dst, size_t n, const char* src) {
  while (*src == ' ') src++;
  strlcpy(dst, src, n);
  for (int i = strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

bool fetchFlights() {
  if (!settings.pgFlights) {
    flights.n = 0;
    flights.err = "";
    return true;
  }
  if (!resolveCenter()) {
    flights.n = 0;
    flights.err = "Set a PiAware URL or a location";
    return true;
  }
  bool local = settings.adsbUrl.length() > 0;
  static uint32_t lastRemote = 0;
  if (!local && lastRemote && millis() - lastRemote < 30000) return true;  // be polite to adsb.lol

  String url;
  if (local) {
    url = settings.adsbUrl;
  } else {
    char b[128];
    snprintf(b, sizeof(b), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d", flights.ctrLat, flights.ctrLon,
             max((int)settings.radiusNm, 5));
    url = b;
    lastRemote = millis();
  }

  Http h;
  int code = httpGet(h, url);
  if (code != 200) {
    flights.err = httpErr(code);
    h.http.end();
    return false;
  }
  Stream& s = h.http.getStream();
  s.setTimeout(4000);
  if (!s.find(local ? "\"aircraft\"" : "\"ac\"") || !s.find("[")) {
    flights.err = "No aircraft list in response";
    h.http.end();
    return false;
  }

  JsonDocument filter;
  for (const char* k : {"hex", "flight", "alt_baro", "gs", "track", "baro_rate", "geom_rate", "lat", "lon"})
    filter[k] = true;

  // Nearest N candidates, sorted by distance.
  Flight cand[FLIGHTS_MAX];
  int nc = 0, limit = constrain((int)settings.maxFlights, 1, FLIGHTS_MAX);
  JsonDocument doc;
  do {
    if (deserializeJson(doc, s, DeserializationOption::Filter(filter))) break;
    if (!doc["lat"].is<float>() || !doc["lon"].is<float>()) continue;
    char cs[10];
    trimCopy(cs, sizeof(cs), doc["flight"] | "");
    if (!cs[0]) continue;
    bool ground = doc["alt_baro"].is<const char*>();
    int alt = ground ? 0 : (doc["alt_baro"] | 0);
    if (ground ? settings.minAlt > 0 : (alt < settings.minAlt || alt > settings.maxAlt)) continue;
    float lat = doc["lat"], lon = doc["lon"];
    float d = distNm(flights.ctrLat, flights.ctrLon, lat, lon);
    if (d > settings.radiusNm) continue;
    if (nc == limit && d >= cand[nc - 1].dist) continue;
    Flight f;
    strlcpy(f.hex, doc["hex"] | "", sizeof(f.hex));
    strlcpy(f.cs, cs, sizeof(f.cs));
    f.alt = alt;
    f.ground = ground;
    f.gs = doc["gs"].is<float>() ? lroundf(doc["gs"].as<float>()) : -1;
    f.track = doc["track"].is<float>() ? lroundf(doc["track"].as<float>()) : -1;
    f.rate = doc["baro_rate"].is<int>() ? doc["baro_rate"].as<int>() : (doc["geom_rate"] | 0);
    f.lat = lat;
    f.lon = lon;
    f.dist = d;
    f.brg = bearingDeg(flights.ctrLat, flights.ctrLon, lat, lon);
    int pos = nc < limit ? nc++ : nc - 1;
    while (pos > 0 && cand[pos - 1].dist > d) {
      cand[pos] = cand[pos - 1];
      pos--;
    }
    cand[pos] = f;
  } while (s.findUntil(",", "]"));
  h.http.end();

  // Keep the previous order for aircraft still present so pages don't jump around.
  Flight next[FLIGHTS_MAX];
  bool used[FLIGHTS_MAX] = {false};
  int nn = 0;
  for (int i = 0; i < flights.n; i++)
    for (int j = 0; j < nc; j++)
      if (!used[j] && !strcmp(flights.f[i].hex, cand[j].hex)) {
        next[nn++] = cand[j];
        used[j] = true;
        break;
      }
  for (int j = 0; j < nc; j++)
    if (!used[j]) next[nn++] = cand[j];
  memcpy(flights.f, next, sizeof(next));
  flights.n = nn;
  flights.err = "";
  flights.fetchedAt = time(nullptr);
  return true;
}

// ---------- routes ----------

static uint32_t ttlFor(const RouteInfo& r) {
  switch (r.state) {
    case RT_OK: return 3UL * 3600 * 1000;
    case RT_NOT_FOUND: return 30UL * 60 * 1000;
    default: return 5UL * 60 * 1000;
  }
}

static RouteInfo* findCache(const char* cs) {
  for (RouteInfo& r : cache)
    if (r.cs[0] && !strcmp(r.cs, cs)) return &r;
  return nullptr;
}

const RouteInfo* routeFor(const char* cs) {
  RouteInfo* r = findCache(cs);
  return (r && r->state != RT_PENDING) ? r : nullptr;
}

const char* airlineName(const char* icao) {
  int lo = 0, hi = AIRLINE_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strncmp(icao, AIRLINES[mid].code, 3);
    if (c == 0) return AIRLINES[mid].name;
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return nullptr;
}

static bool airlineCallsign(const char* cs) {
  return strlen(cs) >= 4 && isalpha(cs[0]) && isalpha(cs[1]) && isalpha(cs[2]) && isdigit(cs[3]);
}

static void copyAirport(JsonObjectConst a, char* code, size_t cn, char* city, size_t ctn) {
  const char* c = a["code_iata"] | (const char*)nullptr;
  if (!c) c = a["code_icao"] | (const char*)nullptr;
  if (!c) c = a["code"] | "";
  strlcpy(code, c, cn);
  strlcpy(city, a["city"] | "", ctn);
}

static bool lookupAeroApi(const Flight& f, RouteInfo& r) {
  bool reg = f.cs[0] == 'N' && isdigit(f.cs[1]);
  String url = "https://aeroapi.flightaware.com/aeroapi/flights/" + String(f.cs) +
               "?ident_type=" + (reg ? "registration" : "designator") + "&max_pages=1";
  JsonDocument filter;
  JsonObject ff = filter["flights"][0].to<JsonObject>();
  for (const char* k : {"ident_iata", "operator_icao", "registration", "aircraft_type", "actual_off", "actual_on",
                        "progress_percent"})
    ff[k] = true;
  for (const char* side : {"origin", "destination"})
    for (const char* k : {"code_iata", "code_icao", "code", "city"}) ff[side][k] = true;
  filter["title"] = true;  // error responses

  JsonDocument doc;
  String err;
  aeroCounted();
  if (!getJson(url, doc, &filter, err, settings.aeroKey.c_str())) {
    flights.aeroErr = err;
    r.state = err == "HTTP 404" ? RT_NOT_FOUND : RT_ERROR;
    return false;
  }
  flights.aeroErr = "";
  // Pick the leg that is in the air right now.
  JsonObjectConst pick;
  for (JsonObjectConst fl : doc["flights"].as<JsonArrayConst>()) {
    bool off = !fl["actual_off"].isNull(), on = !fl["actual_on"].isNull();
    int pct = fl["progress_percent"] | -1;
    if ((off && !on) || (pct > 0 && pct < 100)) {
      pick = fl;
      break;
    }
  }
  if (pick.isNull()) {
    r.state = RT_NOT_FOUND;
    return true;
  }
  r.state = RT_OK;
  r.src = RS_AEROAPI;
  strlcpy(r.flight, pick["ident_iata"] | "", sizeof(r.flight));
  strlcpy(r.op, pick["operator_icao"] | "", sizeof(r.op));
  strlcpy(r.type, pick["aircraft_type"] | "", sizeof(r.type));
  strlcpy(r.reg, pick["registration"] | "", sizeof(r.reg));
  if (!pick["origin"].isNull()) copyAirport(pick["origin"], r.orig, sizeof(r.orig), r.origCity, sizeof(r.origCity));
  if (!pick["destination"].isNull())
    copyAirport(pick["destination"], r.dest, sizeof(r.dest), r.destCity, sizeof(r.destCity));
  r.routeOk = r.orig[0] && r.dest[0];
  return true;
}

static bool lookupAdsbdb(const Flight& f, RouteInfo& r) {
  String err;
  // Aircraft type + registration by ICAO hex.
  {
    JsonDocument filter;
    filter["response"]["aircraft"]["icao_type"] = true;
    filter["response"]["aircraft"]["registration"] = true;
    JsonDocument doc;
    if (getJson("https://api.adsbdb.com/v0/aircraft/" + String(f.hex), doc, &filter, err)) {
      strlcpy(r.type, doc["response"]["aircraft"]["icao_type"] | "", sizeof(r.type));
      strlcpy(r.reg, doc["response"]["aircraft"]["registration"] | "", sizeof(r.reg));
    }
  }
  if (!airlineCallsign(f.cs)) {
    r.state = r.type[0] ? RT_OK : RT_NOT_FOUND;
    r.src = RS_ADSBDB;
    return true;
  }
  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["callsign_iata"] = true;
  fr["airline"]["icao"] = true;
  for (const char* side : {"origin", "destination"})
    for (const char* k : {"iata_code", "icao_code", "municipality", "latitude", "longitude"}) fr[side][k] = true;
  JsonDocument doc;
  if (!getJson("https://api.adsbdb.com/v0/callsign/" + String(f.cs), doc, &filter, err)) {
    r.state = err == "HTTP 404" ? RT_NOT_FOUND : RT_ERROR;
    return err == "HTTP 404";
  }
  JsonObjectConst route = doc["response"]["flightroute"];
  if (route.isNull()) {
    r.state = r.type[0] ? RT_OK : RT_NOT_FOUND;
    r.src = RS_ADSBDB;
    return true;
  }
  r.state = RT_OK;
  r.src = RS_ADSBDB;
  strlcpy(r.flight, route["callsign_iata"] | "", sizeof(r.flight));
  strlcpy(r.op, route["airline"]["icao"] | "", sizeof(r.op));
  JsonObjectConst o = route["origin"], d = route["destination"];
  strlcpy(r.orig, o["iata_code"] | (o["icao_code"] | ""), sizeof(r.orig));
  strlcpy(r.dest, d["iata_code"] | (d["icao_code"] | ""), sizeof(r.dest));
  strlcpy(r.origCity, o["municipality"] | "", sizeof(r.origCity));
  strlcpy(r.destCity, d["municipality"] | "", sizeof(r.destCity));
  // adsbdb routes can be stale: only trust them if the aircraft is roughly between the two airports.
  double olat = o["latitude"] | 0.0, olon = o["longitude"] | 0.0, dlat = d["latitude"] | 0.0, dlon = d["longitude"] | 0.0;
  double direct = distNm(olat, olon, dlat, dlon);
  double via = distNm(olat, olon, f.lat, f.lon) + distNm(f.lat, f.lon, dlat, dlon);
  r.routeOk = r.orig[0] && r.dest[0] && direct > 1 && via <= direct * 1.25 + 100;
  return true;
}

bool fetchRoutes() {
  if (!settings.pgFlights) return true;
  usageTick();
  for (int i = 0; i < flights.n; i++) {
    const Flight& f = flights.f[i];
    RouteInfo* r = findCache(f.cs);
    if (r && millis() - r->at < ttlFor(*r)) continue;
    if (!r) {
      r = &cache[cacheNext];
      cacheNext = (cacheNext + 1) % CACHE_SIZE;
    }
    RouteInfo fresh;
    strlcpy(fresh.cs, f.cs, sizeof(fresh.cs));
    // Free source first; spend an AeroAPI lookup only when it can add a route adsbdb couldn't.
    bool ok = lookupAdsbdb(f, fresh);
    if (airlineCallsign(f.cs) && !(fresh.state == RT_OK && fresh.routeOk) && aeroAllowed()) {
      RouteInfo a;
      strlcpy(a.cs, f.cs, sizeof(a.cs));
      if (lookupAeroApi(f, a) && a.state == RT_OK) {
        if (!a.type[0]) strlcpy(a.type, fresh.type, sizeof(a.type));
        if (!a.reg[0]) strlcpy(a.reg, fresh.reg, sizeof(a.reg));
        fresh = a;
        ok = true;
      }
    }
    if (fresh.state == RT_PENDING) fresh.state = RT_ERROR;
    fresh.at = millis();
    *r = fresh;
    return ok;  // one lookup per call keeps the web UI and display responsive
  }
  return true;
}

void flightsToJson(JsonArray a) {
  for (int i = 0; i < flights.n; i++) {
    const Flight& f = flights.f[i];
    JsonObject o = a.add<JsonObject>();
    o["cs"] = f.cs;
    o["hex"] = f.hex;
    o["alt"] = f.ground ? -1 : f.alt;
    o["gs"] = f.gs;
    o["dist"] = roundf(f.dist * 10) / 10;
    o["brg"] = f.brg;
    const RouteInfo* r = routeFor(f.cs);
    if (r) {
      static const char* SRC[] = {"", "aeroapi", "adsbdb"};
      static const char* ST[] = {"pending", "ok", "not found", "error"};
      o["route"] = r->routeOk ? String(r->orig) + "-" + r->dest : String("");
      o["flight"] = r->flight;
      o["type"] = r->type;
      o["reg"] = r->reg;
      o["src"] = SRC[r->src];
      o["state"] = ST[r->state];
    }
  }
}
