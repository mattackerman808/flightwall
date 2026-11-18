#include "render.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "assets_logos.h"
#include "board.h"
#include "flights.h"
#include "settings.h"
#include "wx.h"

static MatrixPanel_I2S_DMA* dma = nullptr;

// Everything is drawn inside a 1px margin so nothing sits on the panel's edge.
// Page code uses inner coordinates: x 0..W-1, y 0..H-1.
#define MX 1
#define MY 1
static const int W = PANEL_W - 2 * MX;  // 126
static const int H = PANEL_H - 2 * MY;  // 62

static void pPixel(int x, int y, uint16_t c) { dma->drawPixel(x + MX, y + MY, c); }
static void pLine(int x0, int y0, int x1, int y1, uint16_t c) { dma->drawLine(x0 + MX, y0 + MY, x1 + MX, y1 + MY, c); }
static void pVLine(int x, int y, int h, uint16_t c) { dma->drawFastVLine(x + MX, y + MY, h, c); }
static void pHLine(int x, int y, int w, uint16_t c) { dma->drawFastHLine(x + MX, y + MY, w, c); }
static void pFillRect(int x, int y, int w, int h, uint16_t c) { dma->fillRect(x + MX, y + MY, w, h, c); }
static void pFillCircle(int x, int y, int r, uint16_t c) { dma->fillCircle(x + MX, y + MY, r, c); }
static void pFillTri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  dma->fillTriangle(x0 + MX, y0 + MY, x1 + MX, y1 + MY, x2 + MX, y2 + MY, c);
}

// ---------- colors ----------
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return MatrixPanel_I2S_DMA::color565(r, g, b); }
static const uint16_t C_WHITE = 0xFFFF;
#define C_GRAY rgb(120, 120, 120)
#define C_DIM rgb(40, 40, 40)
#define C_AMBER rgb(255, 150, 0)
#define C_YELLOW rgb(255, 210, 0)
#define C_WARM rgb(255, 120, 40)
#define C_COOL rgb(90, 170, 255)
#define C_RAIN rgb(60, 120, 255)

static uint16_t catColor(Cat c) {
  switch (c) {
    case CAT_VFR: return rgb(0, 210, 0);
    case CAT_MVFR: return rgb(40, 100, 255);
    case CAT_IFR: return rgb(240, 0, 0);
    case CAT_LIFR: return rgb(230, 0, 230);
    default: return C_GRAY;
  }
}

// ---------- text ----------
// TINY: TomThumb 3x5 (4px advance). SM: built-in 5x7 (6px). LG: built-in x2 (12px, 14px tall).
enum Font : uint8_t { TINY, SM, LG };

static int adv(Font f) { return f == TINY ? 4 : f == SM ? 6 : 12; }
static int textW(Font f, const char* s) { return strlen(s) * adv(f) - (f == LG ? 2 : 1); }

static int text(Font f, int x, int yTop, const char* s, uint16_t c) {
  dma->setTextColor(c);
  if (f == TINY) {
    dma->setFont(&TomThumb);
    dma->setTextSize(1);
    dma->setCursor(x + MX, yTop + MY + 5);
  } else {
    dma->setFont(nullptr);
    dma->setTextSize(f == LG ? 2 : 1);
    dma->setCursor(x + MX, yTop + MY);
  }
  dma->print(s);
  return x + strlen(s) * adv(f);
}
static int wSmallHalf(const char* s) { return (int)strlen(s) * 2; }
#define C_DIM_TEXT rgb(90, 90, 90)
static void textRight(Font f, int yTop, const char* s, uint16_t c) { text(f, W - textW(f, s), yTop, s, c); }
static void textCenter(Font f, int cx, int yTop, const char* s, uint16_t c) { text(f, cx - textW(f, s) / 2, yTop, s, c); }

// ---------- time formatting ----------
static bool timeValid() { return time(nullptr) > 1700000000; }

static void fmtClock(time_t t, char* out, size_t n) {
  struct tm tmv;
  if (settings.clockMode == CLOCK_ZULU) {
    gmtime_r(&t, &tmv);
    snprintf(out, n, "%02d%02dZ", tmv.tm_hour, tmv.tm_min);
  } else {
    localtime_r(&t, &tmv);
    if (settings.clockMode == CLOCK_24H) {
      snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else {
      int h = tmv.tm_hour % 12;
      snprintf(out, n, "%d:%02d%s", h ? h : 12, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
    }
  }
}

static void fmtHour(time_t t, char* out, size_t n) {
  struct tm tmv;
  if (settings.clockMode == CLOCK_ZULU) {
    gmtime_r(&t, &tmv);
    snprintf(out, n, "%02dZ", tmv.tm_hour);
  } else {
    localtime_r(&t, &tmv);
    if (settings.clockMode == CLOCK_24H) {
      snprintf(out, n, "%02d", tmv.tm_hour);
    } else {
      int h = tmv.tm_hour % 12;
      snprintf(out, n, "%d%c", h ? h : 12, tmv.tm_hour < 12 ? 'A' : 'P');
    }
  }
}

// ---------- weather formatting ----------
static void fmtWind(int dir, int spd, char* out, size_t n) {
  if (spd < 0) snprintf(out, n, "---");
  else if (spd == 0) snprintf(out, n, "CALM");
  else if (dir == WIND_VRB) snprintf(out, n, "VRB@%02d", spd);
  else if (dir == WIND_NONE) snprintf(out, n, "---@%02d", spd);
  else snprintf(out, n, "%03d@%02d", dir, spd);
}

// Wind with gust in amber; returns end x.
static int drawWind(Font f, int x, int y, int dir, int spd, int gst) {
  char buf[16];
  fmtWind(dir, spd, buf, sizeof(buf));
  x = text(f, x, y, buf, C_WHITE);
  if (gst > 0 && spd > 0) {
    snprintf(buf, sizeof(buf), "G%02d", gst);
    x = text(f, x, y, buf, C_AMBER);
  }
  return x;
}

static void fmtVis(const Metar& m, char* out, size_t n) {
  if (!m.hasVis) snprintf(out, n, "--SM");
  else if (m.vis >= 10) snprintf(out, n, "10SM");
  else if (m.vis == (int)m.vis) snprintf(out, n, "%dSM", (int)m.vis);
  else if (m.vis >= 1) snprintf(out, n, "%.1fSM", m.vis);
  else snprintf(out, n, "%.2gSM", m.vis);
}

static void fmtTempC(float c, char* out, size_t n) {
  int v = lroundf(c);
  if (v < 0) snprintf(out, n, "M%02d", -v);
  else snprintf(out, n, "%02d", v);
}

// ---------- icons (16x16) ----------
static void cloud16(int x, int y, int dy, uint16_t c) {
  pFillCircle(x + 5, y + 9 + dy, 3, c);
  pFillCircle(x + 10, y + 7 + dy, 4, c);
  pFillRect(x + 2, y + 9 + dy, 13, 4, c);
}

static void sun16(int x, int y, int cx, int cy, int r, uint16_t c) {
  cx += x;
  cy += y;
  pFillCircle(cx, cy, r, c);
  int a = r + 2, b = r + 3, d = (r + 2) * 7 / 10, e = (r + 3) * 7 / 10;
  pLine(cx, cy - a, cx, cy - b, c);
  pLine(cx, cy + a, cx, cy + b, c);
  pLine(cx - a, cy, cx - b, cy, c);
  pLine(cx + a, cy, cx + b, cy, c);
  pLine(cx - d, cy - d, cx - e, cy - e, c);
  pLine(cx + d, cy - d, cx + e, cy - e, c);
  pLine(cx - d, cy + d, cx - e, cy + e, c);
  pLine(cx + d, cy + d, cx + e, cy + e, c);
}

static void moon16(int x, int y, int cx, int cy, int r) {
  pFillCircle(x + cx, y + cy, r, rgb(230, 230, 180));
  pFillCircle(x + cx + r / 2 + 1, y + cy - r / 3, r - 1, 0);
}

static void drawIcon(Icon ic, int x, int y) {
  uint16_t light = rgb(210, 210, 210), dark = rgb(140, 140, 150);
  switch (ic) {
    case IC_SUN: sun16(x, y, 8, 8, 4, C_YELLOW); break;
    case IC_MOON: moon16(x, y, 7, 8, 6); break;
    case IC_PARTLY_DAY:
      sun16(x, y, 6, 5, 3, C_YELLOW);
      cloud16(x, y, 3, light);
      break;
    case IC_PARTLY_NIGHT:
      moon16(x, y, 6, 5, 4);
      cloud16(x, y, 3, light);
      break;
    case IC_CLOUD: cloud16(x, y, 1, light); break;
    case IC_RAIN:
      cloud16(x, y, -2, dark);
      for (int i = 0; i < 3; i++) pLine(x + 4 + i * 4, y + 12, x + 3 + i * 4, y + 15, C_RAIN);
      break;
    case IC_SNOW:
      cloud16(x, y, -2, dark);
      for (int i = 0; i < 3; i++) {
        pPixel(x + 4 + i * 4, y + 12, C_WHITE);
        pPixel(x + 6 + i * 4, y + 15, C_WHITE);
      }
      break;
    case IC_STORM:
      cloud16(x, y, -2, dark);
      pLine(x + 9, y + 10, x + 6, y + 13, C_YELLOW);
      pLine(x + 6, y + 13, x + 10, y + 13, C_YELLOW);
      pLine(x + 10, y + 13, x + 7, y + 15, C_YELLOW);
      break;
    case IC_FOG:
      for (int i = 0; i < 4; i++) pHLine(x + 1 + (i % 2) * 2, y + 3 + i * 3, 12, rgb(160, 160, 160));
      break;
  }
}

// ---------- pages ----------

static void drawMetar(const Metar& m) {
  char buf[32], buf2[24];
  uint16_t cc = catColor(m.cat);
  text(LG, 0, 0, m.icao, m.valid ? cc : C_GRAY);
  if (!m.valid) {
    text(SM, 0, 24, "NO METAR", C_AMBER);
    text(SM, 0, 34, "CHECK THE AIRPORT ID", C_GRAY);
    return;
  }
  textRight(LG, 0, catName(m.cat), cc);

  drawWind(LG, 0, 17, m.wdir, m.wspd, m.wgst);

  // Visibility + weather, ceiling right-aligned.
  fmtVis(m, buf, sizeof(buf));
  if (m.ceil == CEIL_NONE) snprintf(buf2, sizeof(buf2), "NO CIG");
  else snprintf(buf2, sizeof(buf2), "CIG %d", m.ceil);
  int x = text(SM, 0, 35, buf, C_WHITE);
  if (m.wx[0] && x + 6 + textW(SM, m.wx) + 8 + textW(SM, buf2) <= W) text(SM, x + 6, 35, m.wx, C_COOL);
  textRight(SM, 35, buf2, m.ceil == CEIL_NONE ? C_GRAY : C_WHITE);

  // Temp/dew + altimeter.
  if (m.hasTemp) {
    char t[8], d[8];
    fmtTempC(m.tempC, t, sizeof(t));
    fmtTempC(m.dewC, d, sizeof(d));
    snprintf(buf, sizeof(buf), "%s/%s", t, d);
  } else {
    snprintf(buf, sizeof(buf), "--/--");
  }
  text(SM, 0, 45, buf, C_WHITE);
  if (m.alt > 0) {
    snprintf(buf, sizeof(buf), "A%.2f", m.alt);
    textRight(SM, 45, buf, C_WHITE);
  }

  // Observation time/age + clock.
  if (m.obs) {
    struct tm z;
    gmtime_r(&m.obs, &z);
    if (timeValid()) {
      long mins = (time(nullptr) - m.obs) / 60;
      snprintf(buf, sizeof(buf), "%02d%02dZ %ldM AGO", z.tm_hour, z.tm_min, mins < 0 ? 0 : mins);
    } else {
      snprintf(buf, sizeof(buf), "%02d%02dZ", z.tm_hour, z.tm_min);
    }
    long ageMin = timeValid() ? (time(nullptr) - m.obs) / 60 : 0;
    text(SM, 0, 55, buf, ageMin > 90 ? C_AMBER : C_GRAY);
  }
  if (timeValid()) {
    fmtClock(time(nullptr), buf, sizeof(buf));
    textRight(SM, 55, buf, C_GRAY);
  }
}

static int tafBaseAt(const Taf& t, time_t when) {
  int found = -1;
  for (int i = 0; i < t.n; i++)
    if (t.p[i].base && t.p[i].from <= when) found = i;
  return found;
}

static void drawTaf() {
  const Taf& t = wx.taf;
  char buf[32];
  if (!t.valid) {
    text(SM, 0, 0, "TAF", C_WHITE);
    text(SM, 0, 24, "NO TAF AVAILABLE", C_GRAY);
    return;
  }
  snprintf(buf, sizeof(buf), "TAF %s", t.icao);
  text(SM, 0, 0, buf, C_WHITE);
  if (t.distNm > 0.5f) snprintf(buf, sizeof(buf), "%dNM AWAY", (int)lroundf(t.distNm));
  else snprintf(buf, sizeof(buf), "NEXT 24H");
  textRight(SM, 0, buf, C_GRAY);
  if (!timeValid()) {
    text(SM, 0, 24, "WAITING FOR TIME", C_GRAY);
    return;
  }

  // 24h timeline: base conditions on top, TEMPO/PROB underneath.
  time_t now = time(nullptr);
  const int step = 24 * 3600 / W;  // ~686s per column
  for (int px = 0; px < W; px++) {
    time_t when = now + (time_t)px * step;
    uint16_t c = C_DIM;
    if (when < t.validTo) {
      int b = tafBaseAt(t, when);
      if (b >= 0) c = catColor(t.p[b].cat);
    }
    pVLine(px, 10, 12, c);
    Cat worst = CAT_UNK;
    for (int i = 0; i < t.n; i++)
      if (!t.p[i].base && t.p[i].from <= when && when < t.p[i].to && t.p[i].cat > worst) worst = t.p[i].cat;
    if (worst != CAT_UNK) pVLine(px, 23, 3, catColor(worst));
  }
  text(SM, 0, 29, "NOW", C_GRAY);
  for (int k = 1; k <= 3; k++) {
    int px = k * W / 4;
    pVLine(px, 26, 2, C_GRAY);
    fmtHour(now + (time_t)px * step, buf, sizeof(buf));
    textCenter(SM, px, 29, buf, C_GRAY);
  }

  // Upcoming changes.
  int shown = 0;
  for (int i = 0; i < t.n && shown < 3; i++) {
    const TafPeriod& p = t.p[i];
    if (!p.base || p.from <= now || p.from >= t.validTo) continue;
    int y = 38 + shown * 8;
    fmtHour(p.from, buf, sizeof(buf));
    text(SM, 0, y, buf, C_WHITE);
    text(SM, 24, y, catName(p.cat), catColor(p.cat));
    drawWind(SM, 54, y, p.wdir, p.wspd, p.wgst);
    shown++;
  }
  if (!shown) text(SM, 0, 38, tafBaseAt(t, now) >= 0 ? "NO CHANGES" : "TAF EXPIRED", C_GRAY);
}

static void drawOutlook() {
  const Outlook& o = wx.outlook;
  char buf[32];
  // Header: place name left, current conditions right.
  String name = settings.fcName;
  name.toUpperCase();
  int rightW = 0;
  if (o.hasNow) {
    snprintf(buf, sizeof(buf), "NOW %d", o.nowTemp);
    rightW = textW(SM, buf);
    textRight(SM, 0, buf, C_WHITE);
  }
  int maxChars = (W - rightW - 6) / 6;
  if ((int)name.length() > maxChars) name = name.substring(0, maxChars);
  text(SM, 0, 0, name.c_str(), C_AMBER);
  if (!o.valid) {
    text(SM, 0, 24, wx.outlookErr.length() ? "FORECAST ERROR" : "LOADING FORECAST", C_GRAY);
    return;
  }
  static const char* DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  for (int col = 0; col < 4 && col < o.n; col++) {
    const OutlookDay& d = o.day[col];
    const int colW = W / 4;  // 31
    int x0 = (W - 4 * colW) / 2 + col * colW, cx = x0 + colW / 2;
    textCenter(SM, cx, 10, col == 0 ? "TDY" : DOW[d.wday % 7], C_WHITE);
    drawIcon(d.icon, cx - 8, 19);
    snprintf(buf, sizeof(buf), "%d", d.hi);
    if (textW(LG, buf) <= colW - 1) textCenter(LG, cx, 37, buf, C_WARM);
    else textCenter(SM, cx, 40, buf, C_WARM);
    snprintf(buf, sizeof(buf), "%d", d.lo);
    if (d.pop >= 10) {
      text(SM, x0 + 1, 55, buf, C_COOL);
      char pp[8];
      snprintf(pp, sizeof(pp), "%d%%", d.pop);
      text(TINY, x0 + colW - 1 - textW(TINY, pp), 56, pp, C_RAIN);
    } else {
      textCenter(SM, cx, 55, buf, C_COOL);
    }
  }
}

// ---------- flights ----------

// Draws a 32x32 airline logo (palette-encoded, from assets_logos.h). Returns false if none.
static bool drawLogo(const char* code, int x, int y) {
  int lo = 0, hi = LOGO_COUNT - 1, idx = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strncmp(code, LOGO_INDEX[mid].code, 3);
    if (c == 0) {
      idx = mid;
      break;
    }
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  if (idx < 0) return false;
  // Format (tools/make_assets.py): n, n x u16 RGB565 palette, then runs of ((len-1)<<4 | index).
  const uint8_t* p = LOGO_DATA + LOGO_INDEX[idx].offset;
  uint8_t n = *p++;
  uint16_t pal[16] = {0};
  for (int k = 0; k < n && k < 16; k++, p += 2) pal[k] = p[0] | (p[1] << 8);
  const int N = LOGO_SIZE * LOGO_SIZE;
  for (int i = 0; i < N;) {
    uint8_t b = *p++;
    uint16_t c = pal[(b & 0x0F) % 16];
    for (int k = 0; k <= (b >> 4) && i < N; k++, i++)
      if (c) pPixel(x + i % LOGO_SIZE, y + i / LOGO_SIZE, c);
  }
  return true;
}

// Plane silhouette centered at (cx,cy), nose pointing along `track` (degrees true).
static void drawPlane(int cx, int cy, int track, uint16_t col) {
  static const int8_t T[][6] = {
      {-2, -10, 2, -10, 0, -14},  // nose
      {-2, -10, 2, -10, 2, 10},   // fuselage
      {-2, -10, 2, 10, -2, 10},
      {0, -4, 14, 4, 0, 1},       // right wing
      {0, -4, -14, 4, 0, 1},      // left wing
      {0, 7, 6, 12, 0, 10},       // tail
      {0, 7, -6, 12, 0, 10},
  };
  float a = (track < 0 ? 0 : track) * (float)M_PI / 180.0f, s = sinf(a), c = cosf(a);
  auto rx = [&](int x, int y) { return cx + (int)lroundf(x * c - y * s); };
  auto ry = [&](int x, int y) { return cy + (int)lroundf(x * s + y * c); };
  for (auto& t : T)
    pFillTri(rx(t[0], t[1]), ry(t[0], t[1]), rx(t[2], t[3]), ry(t[2], t[3]), rx(t[4], t[5]), ry(t[4], t[5]), col);
}

// ASCII uppercase copy, truncated to maxChars.
static void upperAscii(const char* in, char* out, int maxChars) {
  int n = 0;
  for (; *in && n < maxChars; in++) {
    unsigned char ch = *in;
    if (ch < 32 || ch > 126) continue;
    out[n++] = toupper(ch);
  }
  while (n && out[n - 1] == ' ') n--;
  out[n] = 0;
}

static const char* compass8(int deg) {
  static const char* D[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return D[((deg + 22) % 360) / 45];
}

static void drawFlight(int idx) {
  char buf[40];
  if (idx >= flights.n) {
    text(SM, 0, 24, "NO NEARBY FLIGHTS", C_GRAY);
    return;
  }
  const Flight& f = flights.f[idx];
  const RouteInfo* r = routeFor(f.cs);

  char op[4] = "";
  if (r && r->op[0]) strlcpy(op, r->op, sizeof(op));
  else if (isalpha(f.cs[0]) && isalpha(f.cs[1]) && isalpha(f.cs[2]) && isdigit(f.cs[3])) strlcpy(op, f.cs, sizeof(op));
  if (!(op[0] && drawLogo(op, 0, 0))) drawPlane(16, 16, f.track, rgb(200, 200, 210));

  // Flight number (IATA when known), airline name, type + registration.
  const char* id = (r && r->flight[0]) ? r->flight : f.cs;
  if (textW(LG, id) <= W - 36) text(LG, 36, 0, id, C_WHITE);
  else text(SM, 36, 4, id, C_WHITE);
  const char* name = op[0] ? airlineName(op) : nullptr;
  if (name) {
    upperAscii(name, buf, (W - 36) / 6);
    text(SM, 36, 17, buf, C_AMBER);
  }
  if (r && (r->type[0] || r->reg[0])) {
    snprintf(buf, sizeof(buf), "%s%s%s", r->type, r->type[0] && r->reg[0] ? " " : "", r->reg);
    upperAscii(buf, buf, (W - 36) / 6);
    text(SM, 36, name ? 26 : 17, buf, C_GRAY);
  }

  // Route.
  if (r && r->routeOk) {
    int wo = textW(LG, r->orig), wd = textW(LG, r->dest), aw = 14, gap = 5;
    int x = (W - (wo + gap + aw + gap + wd)) / 2;
    text(LG, x, 34, r->orig, C_WHITE);
    int ax = x + wo + gap;
    pHLine(ax, 40, aw - 3, C_GRAY);
    pHLine(ax, 41, aw - 3, C_GRAY);
    pFillTri(ax + aw - 4, 37, ax + aw - 4, 44, ax + aw, 40, C_GRAY);
    int dx = ax + aw + gap;
    text(LG, dx, 34, r->dest, C_WHITE);
    upperAscii(r->origCity, buf, 15);
    if (buf[0]) textCenter(TINY, max(x + wo / 2, wSmallHalf(buf)), 49, buf, C_GRAY);
    upperAscii(r->destCity, buf, 15);
    if (buf[0]) textCenter(TINY, min(dx + wd / 2, W - wSmallHalf(buf)), 49, buf, C_GRAY);
  } else {
    const char* msg = !r ? "LOOKING UP ROUTE..." : (r->state == RT_ERROR ? "ROUTE LOOKUP FAILED" : "NO ROUTE INFO");
    textCenter(SM, W / 2, 38, msg, C_DIM_TEXT);
  }

  // Altitude + trend, speed, distance/bearing.
  if (f.ground) snprintf(buf, sizeof(buf), "GROUND");
  else if (f.alt >= 18000) snprintf(buf, sizeof(buf), "FL%03d", (f.alt + 50) / 100);
  else snprintf(buf, sizeof(buf), "%dFT", (f.alt + 50) / 100 * 100);
  int x = text(SM, 0, 55, buf, C_WHITE);
  if (!f.ground && abs(f.rate) >= 300) {
    int ax = x + 1;
    if (f.rate > 0) pFillTri(ax, 60, ax + 4, 60, ax + 2, 56, rgb(0, 210, 0));
    else pFillTri(ax, 56, ax + 4, 56, ax + 2, 60, C_AMBER);
    x += 7;
  }
  if (f.gs >= 0) {
    snprintf(buf, sizeof(buf), "%dKT", f.gs);
    text(SM, x + 5, 55, buf, C_WHITE);
  }
  if (f.dist < 10) snprintf(buf, sizeof(buf), "%.1fNM %s", f.dist, compass8(f.brg));
  else snprintf(buf, sizeof(buf), "%dNM %s", (int)lroundf(f.dist), compass8(f.brg));
  textRight(SM, 55, buf, C_GRAY);
}

// ---------- public ----------

bool displayBegin() {
  HUB75_I2S_CFG::i2s_pins pins = {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, PIN_A,
                                  PIN_B,  PIN_C,  PIN_D,  PIN_E,  PIN_LAT, PIN_OE, PIN_CLK};
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, 1, pins);
  cfg.i2sspeed = HUB75_I2S_CFG::HZ_20M;
  cfg.latch_blanking = 4;
  cfg.double_buff = true;
  cfg.clkphase = false;
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) return false;
  dma->setTextWrap(false);
  dma->setBrightness8(settings.bright);
  dma->fillScreen(0);
  dma->flipDMABuffer();
  dma->fillScreen(0);
  return true;
}

void displaySetBrightness(uint8_t b) {
  if (dma) dma->setBrightness8(b);
}

static String airportAt(int i) {
  int start = 0;
  for (int k = 0; k < i; k++) {
    start = settings.airports.indexOf(',', start);
    if (start < 0) return "";
    start++;
  }
  int end = settings.airports.indexOf(',', start);
  return settings.airports.substring(start, end < 0 ? settings.airports.length() : end);
}

static int airportCount() {
  if (!settings.airports.length()) return 0;
  int n = 1;
  for (char c : settings.airports) n += c == ',';
  return n;
}

int buildPages(Page* out, int max) {
  int n = 0;
  auto add = [&](PageType t, uint8_t idx) {
    if (n < max) out[n++] = {t, idx};
  };
  if (settings.pgFlights)
    for (int i = 0; i < flights.n; i++) add(PG_FLIGHT, i);
  int na = airportCount();
  for (int i = 0; i < na; i++) {
    if (settings.pgMetar) add(PG_METAR, i);
    if (i == 0 && settings.pgTaf) add(PG_TAF, 0);
  }
  if (settings.pgOutlook && settings.hasForecastLoc()) add(PG_OUTLOOK, 0);
  if (!n) {
    if (na) add(PG_METAR, 0);
    else if (settings.hasForecastLoc()) add(PG_OUTLOOK, 0);
  }
  return n;
}

void drawPage(const Page& p) {
  if (!dma) return;
  dma->fillScreen(0);
  switch (p.type) {
    case PG_METAR: {
      String id = airportAt(p.index);
      if (p.index < wx.nArea && id == wx.area[p.index].icao) {
        drawMetar(wx.area[p.index]);
      } else {
        Metar blank;  // not fetched yet
        strlcpy(blank.icao, id.c_str(), sizeof(blank.icao));
        drawMetar(blank);
      }
      break;
    }
    case PG_FLIGHT: drawFlight(p.index); break;
    case PG_TAF: drawTaf(); break;
    case PG_OUTLOOK: drawOutlook(); break;
  }
  dma->flipDMABuffer();
}

void drawMessage(const char* title, const char* l1, const char* l2, const char* l3, const char* l4) {
  if (!dma) return;
  dma->fillScreen(0);
  text(SM, 0, 0, title, C_AMBER);
  const char* lines[] = {l1, l2, l3, l4};
  for (int i = 0; i < 4; i++)
    if (lines[i]) text(SM, 0, 14 + i * 12, lines[i], C_WHITE);
  dma->flipDMABuffer();
}
