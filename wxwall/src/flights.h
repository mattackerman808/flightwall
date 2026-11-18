#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define FLIGHTS_MAX 8

struct Flight {
  char hex[8] = "";
  char cs[10] = "";  // callsign, trimmed
  int alt = 0;       // feet (baro); 0 when on ground
  bool ground = false;
  int gs = -1;       // knots
  int track = -1;    // degrees
  int rate = 0;      // ft/min
  float lat = 0, lon = 0;
  float dist = 0;    // NM from center
  int brg = 0;       // bearing from center to aircraft
};

enum RouteState : uint8_t { RT_PENDING = 0, RT_OK, RT_NOT_FOUND, RT_ERROR };
enum RouteSource : uint8_t { RS_NONE = 0, RS_AEROAPI, RS_ADSBDB };

struct RouteInfo {
  char cs[10] = "";
  RouteState state = RT_PENDING;
  RouteSource src = RS_NONE;
  bool routeOk = false;     // origin/destination present and plausible
  char flight[10] = "";     // IATA flight number, e.g. UA900
  char op[4] = "";          // operator ICAO, e.g. UAL
  char orig[5] = "", dest[5] = "";
  char origCity[24] = "", destCity[24] = "";
  char type[6] = "";        // ICAO aircraft type, e.g. B77W
  char reg[10] = "";
  uint32_t at = 0;          // millis() when fetched
};

struct FlightsState {
  Flight f[FLIGHTS_MAX];
  int n = 0;
  bool hasCenter = false;
  double ctrLat = 0, ctrLon = 0;
  const char* centerSource = "none";
  time_t fetchedAt = 0;
  String err;
  uint16_t aeroToday = 0, aeroMonth = 0;  // persisted in NVS
  uint16_t aeroAllowedToday = 0;
  String aeroErr;
};

extern FlightsState flights;

bool fetchFlights();   // positions (PiAware or adsb.lol)
bool fetchRoutes();    // one route lookup per call (AeroAPI or adsbdb)
const RouteInfo* routeFor(const char* cs);
const char* airlineName(const char* icao);
void flightsToJson(JsonArray a);
void flightsResetCenter();  // call when location/source settings change
