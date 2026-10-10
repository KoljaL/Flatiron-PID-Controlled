#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "secrets.h" // WIFI_SSID / WIFI_PASSWORD, siehe include/secrets.example.h
#include "config.h"
#include "controller.h"
#include "ui.h"
#include "web_interface.h"
// #include "hardware_test.h"

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // HardwareTest::run();

  Controller::begin();
  UI::begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // Regelung/Heizung laufen währenddessen weiter, damit die Sicherheitsabschaltung greift
  while (WiFi.status() != WL_CONNECTED)
  {
    Controller::update();
    UI::update();
    delay(10);
  }
  Serial.println(WiFi.localIP());

  WebInterface::begin();
}

void loop()
{
  Controller::update();
  UI::update();
}
