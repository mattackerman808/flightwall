#pragma once
#include <Arduino.h>

void webBegin();
void webLoop();

// Implemented in main.cpp
void appSettingsChanged();
void appRequestRefresh();
void appPreviewBrightness(uint8_t b);
void appForgetWifi();
