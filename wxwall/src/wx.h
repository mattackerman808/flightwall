#pragma once
#include <Arduino.h>
#include <time.h>

enum Cat : uint8_t { CAT_UNK = 0, CAT_VFR, CAT_MVFR, CAT_IFR, CAT_LIFR };

#define WIND_VRB -1
#define WIND_NONE -2
#define CEIL_NONE -1

struct Metar {
  bool valid = false;
  char icao[6] = "";
  Cat cat = CAT_UNK;
  bool hasTemp = false;
  float tempC = 0, dewC = 0;
  int wdir = WIND_NONE, wspd = -1, wgst = 0;
  bool hasVis = false;
  float vis = 0;
  float alt = 0;
  int ceil = CEIL_NONE;
  char wx[16] = "";
  time_t obs = 0;
  char raw[200] = "";
};

struct TafPeriod {
  time_t from = 0, to = 0;
  bool base = true;  // false for TEMPO / PROB groups
  Cat cat = CAT_UNK;
  int wdir = WIND_NONE, wspd = -1, wgst = 0;
};

#define TAF_MAX 16
struct Taf {
  bool valid = false;
  char icao[6] = "";
  float distNm = 0;  // >0 when the TAF is from a nearby airport
  time_t validTo = 0;
  int n = 0;
  TafPeriod p[TAF_MAX];
};

enum Icon : uint8_t { IC_SUN, IC_MOON, IC_PARTLY_DAY, IC_PARTLY_NIGHT, IC_CLOUD, IC_RAIN, IC_SNOW, IC_STORM, IC_FOG };

struct OutlookDay {
  int16_t y = 0;
  uint8_t m = 0, d = 0, wday = 0;  // local date at the forecast location; wday 0 = Sunday
  int hi = 0, lo = 0;               // in the configured unit
  int pop = -1;
  Icon icon = IC_CLOUD;
};

#define OUTLOOK_DAYS 5
struct Outlook {
  bool valid = false;
  int n = 0;
  OutlookDay day[OUTLOOK_DAYS];
  bool hasNow = false;
  int nowTemp = 0;
  Icon nowIcon = IC_CLOUD;
};

#define AREA_MAX 10
struct WxState {
  Metar area[AREA_MAX];  // same order as settings.airports
  int nArea = 0;
  Taf taf;
  Outlook outlook;
  uint32_t version = 0;  // bumps on every successful update
  time_t metarAt = 0, tafAt = 0, outlookAt = 0;  // wall-clock fetch times
  String metarErr, tafErr, outlookErr;
};

extern WxState wx;

const Metar* primaryMetar();  // first airport in the list
bool fetchMetars();   // all airports, one request (Tachbase)
bool fetchTaf();      // first airport, or nearest TAF (Tachbase)
bool fetchOutlook();  // daily forecast at the forecast location (Open-Meteo)

Cat catFromVisCeil(bool hasVis, float vis, int ceil);
const char* catName(Cat c);
time_t parseIsoUtc(const char* s);
