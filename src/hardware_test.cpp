#include "hardware_test.h"
#include "config.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <max6675.h>

namespace
{
  constexpr unsigned long SSR_MAX_ON_MS = 5000; // SSR schaltet nach 5 s selbst ab
  constexpr double SSR_ABORT_TEMP = 60.0;       // SSR-Test wird darueber gesperrt

  U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
  MAX6675 &thermocouple()
  {
    static MAX6675 instance(MAX_SCK, MAX_CS, MAX_SO);
    return instance;
  }

  void ssrSet(bool on)
  {
    digitalWrite(SSR_PIN, on ? HIGH : LOW);
  }

  // Zeigt kurz Testmuster, damit alle Pixel pruefbar sind
  void displayPatternTest()
  {
    display.clearBuffer();
    display.drawBox(0, 0, 128, 32);
    display.sendBuffer();
    delay(700);
    display.clearBuffer();
    display.drawFrame(0, 0, 128, 32);
    display.drawLine(0, 0, 127, 31);
    display.drawLine(127, 0, 0, 31);
    display.sendBuffer();
    delay(700);
  }
}

namespace HardwareTest
{
  void run()
  {
    Serial.println();
    Serial.println("=== HARDWARETEST ===");
    Serial.println("MAX6675,millis,status,temp_c");
    thermocouple();

    pinMode(SSR_PIN, OUTPUT);
    ssrSet(false);
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);

    Wire.begin(OLED_SDA, OLED_SCL);
    uint8_t oledAddr = 0;
    for (uint8_t addr = 1; addr < 127; addr++)
    {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0)
      {
        Serial.printf("I2C: Geraet bei 0x%02X\n", addr);
        if (oledAddr == 0 && (addr == 0x3C || addr == 0x3D))
          oledAddr = addr;
      }
    }
    if (oledAddr == 0)
      Serial.println("I2C: kein Display gefunden -> Verkabelung SDA/SCL/VCC/GND pruefen");
    bool displayOk = oledAddr != 0;
    if (displayOk)
    {
      display.setI2CAddress(oledAddr << 1);
      display.begin();
    }
    Serial.println(displayOk ? "OLED: OK" : "OLED: FEHLER");
    if (displayOk)
    {
      Serial.println("OLED: Initialisierung erfolgreich");
      display.setContrast(255);
      displayPatternTest();
    }

    bool lastClk = digitalRead(ENC_CLK);
    bool rawSw = digitalRead(ENC_SW);
    bool stableSw = rawSw;
    unsigned long swChangedAt = millis();
    bool ssrOn = false;
    unsigned long ssrOnSince = 0;
    unsigned long lastSample = 0;
    unsigned long lastDraw = 0;
    long encCount = 0;
    bool tempOk = false;
    double temp = NAN;
    int dtRaw = 0;

    while (true)
    {
      unsigned long now = millis();

      // Encoder: Flanke an CLK, Richtung ueber den analogen DT-Pegel.
      bool clk = digitalRead(ENC_CLK);
      if (clk != lastClk && clk == LOW)
      {
        dtRaw = analogRead(ENC_DT);
        bool dt = dtRaw > ENC_ANALOG_THRESHOLD;
        encCount += dt ? -1 : 1;
        Serial.printf("Encoder: %ld (DT raw=%d)\n", encCount, dtRaw);
      }
      lastClk = clk;

      // Taster: nicht blockierend entprellen und nur eine stabile LOW-Flanke auswerten
      bool sw = digitalRead(ENC_SW);
      if (sw != rawSw)
      {
        rawSw = sw;
        swChangedAt = now;
      }
      if (rawSw != stableSw && now - swChangedAt >= 30)
      {
        stableSw = rawSw;
        if (stableSw == LOW)
        {
          bool allowed = tempOk && temp < SSR_ABORT_TEMP;
          ssrOn = allowed && !ssrOn;
          ssrSet(ssrOn);
          if (ssrOn)
            ssrOnSince = now;
          Serial.println(ssrOn ? "SSR: AN" : (allowed ? "SSR: AUS" :
                                               (tempOk ? "SSR: gesperrt (zu heiss)" :
                                                         "SSR: gesperrt (Sensor ungueltig)")));
        }
      }

      // SSR-Sicherheit: Zeitlimit und Uebertemperatur
      if (ssrOn && (now - ssrOnSince >= SSR_MAX_ON_MS || (tempOk && temp >= SSR_ABORT_TEMP)))
      {
        ssrOn = false;
        ssrSet(false);
        Serial.println("SSR: AUS (Timeout/Temperatur)");
      }

      // Temperatursensor
      if (now - lastSample >= SENSOR_INTERVAL)
      {
        lastSample = now;
        temp = thermocouple().readCelsius();
        tempOk = isfinite(temp) && temp >= 0.0 && temp <= 1024.0;
        Serial.printf("MAX6675,%lu,%s,%.2f\n", now,
                      tempOk ? "OK" : "UNGUELTIG", temp);
      }

      // Anzeige + Seriell einmal pro Sekunde (Display 4x/s)
      if (now - lastDraw >= 250)
      {
        lastDraw = now;
        if (displayOk)
        {
          char line[32];
          display.clearBuffer();
          display.setFont(u8g2_font_6x10_tf);
          display.drawStr(0, 8, "HARDWARETEST");
          snprintf(line, sizeof(line), "T: %s  E: %ld", tempOk ? String(temp, 1).c_str() : "FEHLER", encCount);
          display.drawStr(0, 18, line);
          snprintf(line, sizeof(line), "SW:%s SSR:%s",
                   digitalRead(ENC_SW) == LOW ? "AN" : "AUS", ssrOn ? "AN" : "AUS");
          display.drawStr(0, 28, line);
          display.sendBuffer();
        }
        static unsigned long lastSerial = 0;
        if (now - lastSerial >= 1000)
        {
          lastSerial = now;
          Serial.printf("Teststatus: Enc=%ld | SW=%s | SSR=%s\n", encCount,
                        digitalRead(ENC_SW) == LOW ? "gedrueckt" : "los", ssrOn ? "AN" : "AUS");
        }
      }

      yield();
    }
  }
}
