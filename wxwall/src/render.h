#pragma once
#include <Arduino.h>

enum PageType : uint8_t { PG_FLIGHT, PG_METAR, PG_TAF, PG_OUTLOOK };

struct Page {
  PageType type;
  uint8_t index;  // FLIGHT: aircraft; METAR: airport
};

bool displayBegin();
void displaySetBrightness(uint8_t b);

// Builds the rotation from settings + available data. Returns count.
int buildPages(Page* out, int max);
void drawPage(const Page& p);

// Full-screen status messages (up to 5 short lines, TomThumb font).
void drawMessage(const char* title, const char* l1 = nullptr, const char* l2 = nullptr, const char* l3 = nullptr,
                 const char* l4 = nullptr);
