#include "settings.h"
#include <Preferences.h>

Settings settings;
static Preferences prefs;

static bool validIdent(const String& s) {
  if (s.length() < 3 || s.length() > 4) return false;
  for (char c : s)
    if (!isalnum((unsigned char)c)) return false;
  return true;
}

// Normalizes "kpao, ksjc  KSFO" -> "KPAO,KSJC,KSFO" (deduplicated).
static bool normalizeList(const String& in, String& out, String& err) {
  out = "";
  String cur;
  int count = 0;
  for (size_t i = 0; i <= in.length(); i++) {
    char c = i < in.length() ? in[i] : ',';
    if (c == ',' || c == ' ' || c == ';' || c == '\n') {
      if (cur.length()) {
        cur.toUpperCase();
        if (!validIdent(cur)) {
          err = "Invalid airport: " + cur;
          return false;
        }
        if (("," + out + ",").indexOf("," + cur + ",") < 0) {
          if (++count > MAX_AIRPORTS) {
            err = "At most " + String(MAX_AIRPORTS) + " airports";
            return false;
          }
          if (out.length()) out += ",";
          out += cur;
        }
        cur = "";
      }
    } else {
      cur += c;
    }
  }
  return true;
}

void settingsLoad() {
  prefs.begin("wxwall", true);
  Settings d;
  if (prefs.isKey("airports")) {
    settings.airports = prefs.getString("airports", d.airports);
  } else {
    // v0.1/v0.2 stored a home airport plus "others"; merge them, home first.
    String merged, err;
    normalizeList(prefs.getString("home", "") + "," + prefs.getString("others", ""), merged, err);
    settings.airports = merged;
  }
  settings.fcName = prefs.getString("fcName", d.fcName);
  settings.fcLabel = prefs.getString("fcLabel", d.fcLabel);
  settings.fcLat = prefs.getDouble("fcLat", d.fcLat);
  settings.fcLon = prefs.getDouble("fcLon", d.fcLon);
  settings.tz = prefs.getString("tz", d.tz);
  settings.clockMode = prefs.getUChar("clock", d.clockMode);
  settings.bright = prefs.getUChar("bright", d.bright);
  settings.nightBright = prefs.getUChar("nbright", d.nightBright);
  settings.nightStart = prefs.getUChar("nstart", d.nightStart);
  settings.nightEnd = prefs.getUChar("nend", d.nightEnd);
  settings.pageSecs = prefs.getUChar("secs", d.pageSecs);
  settings.pgFlights = prefs.getBool("pgF", d.pgFlights);
  settings.pgMetar = prefs.getBool("pgM", d.pgMetar);
  settings.pgTaf = prefs.getBool("pgT", d.pgTaf);
  settings.pgOutlook = prefs.getBool("pgO", d.pgOutlook);
  settings.celsius = prefs.getBool("celsius", d.celsius);
  settings.api = prefs.getString("api", d.api);
  settings.adsbUrl = prefs.getString("adsbUrl", d.adsbUrl);
  settings.radiusNm = prefs.getUChar("radius", d.radiusNm);
  settings.minAlt = prefs.getUShort("minAlt", d.minAlt);
  settings.maxAlt = prefs.getUShort("maxAlt", d.maxAlt);
  settings.maxFlights = prefs.getUChar("maxFl", d.maxFlights);
  settings.aeroKey = prefs.getString("aeroKey", d.aeroKey);
  settings.aeroBudget = prefs.getUShort("aeroBud", d.aeroBudget);
  prefs.end();
}

void settingsSave() {
  prefs.begin("wxwall", false);
  prefs.putString("airports", settings.airports);
  prefs.remove("home");
  prefs.remove("others");
  prefs.putString("fcName", settings.fcName);
  prefs.putString("fcLabel", settings.fcLabel);
  prefs.putDouble("fcLat", settings.fcLat);
  prefs.putDouble("fcLon", settings.fcLon);
  prefs.putString("tz", settings.tz);
  prefs.putUChar("clock", settings.clockMode);
  prefs.putUChar("bright", settings.bright);
  prefs.putUChar("nbright", settings.nightBright);
  prefs.putUChar("nstart", settings.nightStart);
  prefs.putUChar("nend", settings.nightEnd);
  prefs.putUChar("secs", settings.pageSecs);
  prefs.putBool("pgF", settings.pgFlights);
  prefs.putBool("pgM", settings.pgMetar);
  prefs.putBool("pgT", settings.pgTaf);
  prefs.remove("pgA");  // area page removed in v0.5.3
  prefs.putBool("pgO", settings.pgOutlook);
  prefs.putBool("celsius", settings.celsius);
  prefs.remove("inverse");  // v0.4.0 setting, since removed
  prefs.putString("api", settings.api);
  prefs.putString("adsbUrl", settings.adsbUrl);
  prefs.putUChar("radius", settings.radiusNm);
  prefs.putUShort("minAlt", settings.minAlt);
  prefs.putUShort("maxAlt", settings.maxAlt);
  prefs.putUChar("maxFl", settings.maxFlights);
  prefs.putString("aeroKey", settings.aeroKey);
  prefs.putUShort("aeroBud", settings.aeroBudget);
  prefs.remove("aeroCap");
  prefs.end();
}

void settingsToJson(JsonObject o) {
  o["configured"] = settings.configured();
  o["airports"] = settings.airports;
  JsonObject f = o["forecast"].to<JsonObject>();
  if (settings.hasForecastLoc()) {
    f["name"] = settings.fcName;
    f["label"] = settings.fcLabel;
    f["lat"] = settings.fcLat;
    f["lon"] = settings.fcLon;
  }
  o["tz"] = settings.tz;
  o["clock"] = settings.clockMode;
  o["bright"] = settings.bright;
  o["nightBright"] = settings.nightBright;
  o["nightStart"] = settings.nightStart;
  o["nightEnd"] = settings.nightEnd;
  o["pageSecs"] = settings.pageSecs;
  JsonObject p = o["pages"].to<JsonObject>();
  p["flights"] = settings.pgFlights;
  p["metar"] = settings.pgMetar;
  p["taf"] = settings.pgTaf;
  p["outlook"] = settings.pgOutlook;
  o["celsius"] = settings.celsius;
  o["api"] = settings.api;
  JsonObject fl = o["flights"].to<JsonObject>();
  fl["adsbUrl"] = settings.adsbUrl;
  fl["radiusNm"] = settings.radiusNm;
  fl["minAlt"] = settings.minAlt;
  fl["maxAlt"] = settings.maxAlt;
  fl["maxFlights"] = settings.maxFlights;
  fl["aeroKeySet"] = settings.aeroKey.length() > 0;
  fl["aeroBudget"] = settings.aeroBudget;
}

bool settingsFromJson(JsonObjectConst in, String& err) {
  Settings s = settings;
  if (in["airports"].is<const char*>()) {
    if (!normalizeList(in["airports"].as<String>(), s.airports, err)) return false;
  }
  if (!in["forecast"].isNull()) {
    JsonObjectConst f = in["forecast"];
    if (f["lat"].is<double>() && f["lon"].is<double>()) {
      double lat = f["lat"], lon = f["lon"];
      String name = f["name"] | "";
      String label = f["label"] | "";
      name.trim();
      label.trim();
      if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || !name.length() || name.length() > 40 ||
          label.length() > 80) {
        err = "Invalid forecast location";
        return false;
      }
      s.fcLat = lat;
      s.fcLon = lon;
      s.fcName = name;
      s.fcLabel = label.length() ? label : name;
    } else {  // empty object clears the location
      s.fcLat = s.fcLon = NAN;
      s.fcName = s.fcLabel = "";
    }
  }
  if (in["tz"].is<const char*>()) {
    String tz = in["tz"].as<String>();
    tz.trim();
    if (tz.length() < 3 || tz.length() > 60) {
      err = "Invalid time zone";
      return false;
    }
    s.tz = tz;
  }
  if (in["clock"].is<int>()) s.clockMode = constrain(in["clock"].as<int>(), 0, 2);
  if (in["bright"].is<int>()) s.bright = constrain(in["bright"].as<int>(), 1, 255);
  if (in["nightBright"].is<int>()) s.nightBright = constrain(in["nightBright"].as<int>(), 0, 255);
  if (in["nightStart"].is<int>()) s.nightStart = constrain(in["nightStart"].as<int>(), 0, 23);
  if (in["nightEnd"].is<int>()) s.nightEnd = constrain(in["nightEnd"].as<int>(), 0, 23);
  if (in["pageSecs"].is<int>()) s.pageSecs = constrain(in["pageSecs"].as<int>(), 3, 120);
  JsonObjectConst p = in["pages"];
  if (!p.isNull()) {
    if (p["flights"].is<bool>()) s.pgFlights = p["flights"];
    if (p["metar"].is<bool>()) s.pgMetar = p["metar"];
    if (p["taf"].is<bool>()) s.pgTaf = p["taf"];
    if (p["outlook"].is<bool>()) s.pgOutlook = p["outlook"];
  }
  if (in["celsius"].is<bool>()) s.celsius = in["celsius"];
  if (in["api"].is<const char*>()) {
    String a = in["api"].as<String>();
    a.trim();
    while (a.endsWith("/")) a.remove(a.length() - 1);
    if (!a.startsWith("http://") && !a.startsWith("https://")) {
      err = "API URL must start with http:// or https://";
      return false;
    }
    s.api = a;
  }
  JsonObjectConst fl = in["flights"];
  if (!fl.isNull()) {
    if (fl["adsbUrl"].is<const char*>()) {
      String u = fl["adsbUrl"].as<String>();
      u.trim();
      if (u.length() && !u.startsWith("http://") && !u.startsWith("https://")) {
        err = "PiAware URL must start with http://";
        return false;
      }
      s.adsbUrl = u;
    }
    if (fl["radiusNm"].is<int>()) s.radiusNm = constrain(fl["radiusNm"].as<int>(), 1, 100);
    if (fl["minAlt"].is<int>()) s.minAlt = constrain(fl["minAlt"].as<int>(), 0, 60000);
    if (fl["maxAlt"].is<int>()) s.maxAlt = constrain(fl["maxAlt"].as<int>(), 0, 60000);
    if (fl["maxFlights"].is<int>()) s.maxFlights = constrain(fl["maxFlights"].as<int>(), 1, 8);
    if (fl["aeroBudget"].is<int>()) s.aeroBudget = constrain(fl["aeroBudget"].as<int>(), 0, 60000);
    if (fl["aeroKey"].is<const char*>()) {
      String k = fl["aeroKey"].as<String>();
      k.trim();
      if (k.length() > 128) {
        err = "AeroAPI key is too long";
        return false;
      }
      if (k.length()) s.aeroKey = k;
    }
    if (fl["aeroKeyClear"] | false) s.aeroKey = "";
  }
  settings = s;
  return true;
}

String firstAirport() {
  int comma = settings.airports.indexOf(',');
  return comma < 0 ? settings.airports : settings.airports.substring(0, comma);
}
