#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum ClockMode : uint8_t { CLOCK_12H = 0, CLOCK_24H = 1, CLOCK_ZULU = 2 };

#define MAX_AIRPORTS 10

struct Settings {
  String airports = "";           // "KPAO,KSJC,..." — each gets a METAR page; first gets the TAF
  String fcName = "";             // forecast place, short (e.g. "Palo Alto")
  String fcLabel = "";            // forecast place, full (e.g. "Palo Alto, California 94301")
  double fcLat = NAN, fcLon = NAN;
  String tz = "UTC0";             // POSIX TZ string
  uint8_t clockMode = CLOCK_12H;
  uint8_t bright = 40;            // 1..255
  uint8_t nightBright = 8;        // 0..255 (0 = off at night)
  uint8_t nightStart = 22;        // local hour; equal to nightEnd disables
  uint8_t nightEnd = 6;
  uint8_t pageSecs = 8;
  bool pgFlights = true, pgMetar = true, pgTaf = true, pgOutlook = true;
  bool celsius = false;           // forecast temperatures
  String api = "https://www.tachbase.com/api";

  // Nearby flights
  String adsbUrl = "";            // PiAware/dump1090 aircraft.json; empty = adsb.lol around the location
  uint8_t radiusNm = 10;
  uint16_t minAlt = 100, maxAlt = 45000;  // feet; minAlt 0 includes aircraft on the ground
  uint8_t maxFlights = 5;
  String aeroKey = "";            // FlightAware AeroAPI key (never sent back to the browser)
  uint16_t aeroBudget = 1800;     // AeroAPI lookups per month ($10 feeder credit / $0.005 = 2000)

  bool hasForecastLoc() const { return !isnan(fcLat) && !isnan(fcLon); }
  bool configured() const { return airports.length() || hasForecastLoc() || adsbUrl.length(); }
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsToJson(JsonObject o);
// Applies fields present in `in`; returns false and fills err on invalid input.
bool settingsFromJson(JsonObjectConst in, String& err);
String firstAirport();
