#include "ui.h"
#include "config.h"
#include "controller.h"
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <U8g2lib.h>

namespace
{
  U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
  bool lastClk = true;
  unsigned long lastDisplayUpdate = 0;
  unsigned long lastDisplayInitAttempt = 0;
  bool displayInitAttempted = false;
  bool displayReady = false;
  bool displayUnavailableReported = false;
  bool startupScreenActive = true;
  bool rawButton = HIGH;
  bool stableButton = HIGH;
  unsigned long buttonChangedAt = 0;

  void initializeDisplay(unsigned long now)
  {
    constexpr unsigned long DISPLAY_RETRY_INTERVAL = 1000;
    if (displayReady ||
        (displayInitAttempted && now - lastDisplayInitAttempt < DISPLAY_RETRY_INTERVAL))
      return;

    displayInitAttempted = true;
    lastDisplayInitAttempt = now;

    Wire.beginTransmission(0x3C);
    const uint8_t status = Wire.endTransmission();
    if (status != 0)
    {
      if (!displayUnavailableReported)
      {
        Serial.printf("OLED nicht erreichbar (I2C-Status %u); Initialisierung wird wiederholt\n", status);
        displayUnavailableReported = true;
      }
      return;
    }

    display.setI2CAddress(0x3C << 1);
    displayReady = display.begin();
    if (!displayReady)
    {
      Serial.println("OLED-Initialisierung fehlgeschlagen; erneuter Versuch folgt");
      return;
    }

    display.setContrast(255);
    if (displayUnavailableReported)
      Serial.println("OLED wieder erreichbar");
    displayUnavailableReported = false;
    lastDisplayUpdate = now - 250;
  }

  void readButton(unsigned long now)
  {
    const bool reading = digitalRead(ENC_SW);
    if (reading != rawButton)
    {
      rawButton = reading;
      buttonChangedAt = now;
    }
    if (rawButton != stableButton && now - buttonChangedAt >= 30)
    {
      stableButton = rawButton;
      if (stableButton == LOW)
      {
        startupScreenActive = false;
        Controller::localButtonPressed();
      }
    }
  }

  void readEncoder()
  {
    bool clk = digitalRead(ENC_CLK);
    if (clk != lastClk && clk == LOW)
    {
      startupScreenActive = false;
      bool dt = analogRead(ENC_DT) > ENC_ANALOG_THRESHOLD;
      Controller::adjustSetpoint(dt ? -5 : 5);
    }
    lastClk = clk;
  }

  void drawDisplay()
  {
    if (!displayReady)
      return;

    char line[32];
    display.clearBuffer();
    if (startupScreenActive)
    {
      display.setFont(u8g2_font_5x7_tf);
      display.drawStr(0, 6, "STARTCHECK");
      if (Controller::hasFault())
      {
        snprintf(line, sizeof(line), "Fehler: %s", Controller::faultReasonText());
        display.drawStr(0, 12, line);
      }
      else if (Controller::hasValidReading())
        display.drawStr(0, 12, "Sensor: OK");
      else
        display.drawStr(0, 12, "Sensor: pruefe...");
      display.drawStr(0, 18, WiFi.status() == WL_CONNECTED ? "WLAN: verbunden" : "WLAN: verbindet...");
      display.drawStr(0, 24, "Heizung: AUS bis Freigabe");
      display.drawStr(0, 30, "Drehen/Druecken: weiter");
    }
    else
    {
      display.setFont(u8g2_font_6x10_tf);
      if (Controller::hasFault())
      {
        display.setFont(u8g2_font_5x7_tf);
        display.drawStr(0, 6, "HEIZUNG AUS - FEHLER VERRIEGELT");
        snprintf(line, sizeof(line), "%s", Controller::faultReasonText());
        display.drawStr(0, 14, line);
        if (Controller::hasValidReading())
          snprintf(line, sizeof(line), "Sensor jetzt OK: %.1f C", Controller::temperature());
        else
          snprintf(line, sizeof(line), "Sensor: %s", Controller::sensorStatusText());
        display.drawStr(0, 22, line);
        display.drawStr(0, 30, "Taster: quittieren (<60 C)");
      }
      else
      {
        display.setFont(u8g2_font_6x10_tf);
        if (Controller::hasValidReading())
          snprintf(line, sizeof(line), "Ist: %.1f C", Controller::temperature());
        else
          snprintf(line, sizeof(line), "Ist: %s", Controller::sensorStatusText());
        display.drawStr(0, 8, line);
        snprintf(line, sizeof(line), "Soll: %.0f C", Controller::setpoint());
        display.drawStr(0, 18, line);
        if (Controller::isTuning())
        {
          snprintf(line, sizeof(line), "TUNE: %.0f%%", Controller::outputPercent());
          display.drawStr(0, 28, line);
        }
        else if (!Controller::isHeaterEnabled())
          display.drawStr(0, 28, "AUSGANG: 0% (AUS)");
        else
        {
          snprintf(line, sizeof(line), "AUSGANG: %.0f%%", Controller::outputPercent());
          display.drawStr(0, 28, line);
        }
      }
    }
    display.sendBuffer();
  }
}

namespace UI
{
  void begin()
  {
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);
    Wire.begin(OLED_SDA, OLED_SCL);
    initializeDisplay(millis());
    lastClk = digitalRead(ENC_CLK);
    rawButton = stableButton = digitalRead(ENC_SW);
    buttonChangedAt = millis();
  }

  void update()
  {
    readEncoder();
    unsigned long now = millis();
    initializeDisplay(now);
    readButton(now);
    if (now - lastDisplayUpdate >= 250)
    {
      drawDisplay();
      lastDisplayUpdate = now;
    }
  }
}
