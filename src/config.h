#pragma once
#include <Arduino.h>

// Pinbelegung fuer NodeMCU v2 (ESP8266). Die D-Nummern sind die Aufdrucke
// auf dem Board; in Klammern stehen die zugehoerigen ESP8266-GPIOs.
// Boot-Strapping-Pins und Anschlussdetails sind in der README beschrieben.

// SSR-Eingang: schaltet die Heizung ueber Zeitfenster-PWM.
// GPIO15 muss beim Start LOW sein; damit bleibt das SSR waehrend des Bootens
// ausgeschaltet. Den Zustand LOW haelt der Board-Pulldown, extern ggf. 10 kOhm
// nach GND vorsehen. Die Firmware setzt den Ausgang zu Beginn ebenfalls LOW.
#define SSR_PIN 15 // D8 (GPIO15)

// MAX6675-Thermoelement-Wandler: SPI-Takt, Chip-Select und serielle Daten
// vom Wandler zum ESP (SO = MISO). Der MAX6675 benoetigt keine MOSI-Leitung.
#define MAX_SCK 14 // D5 (GPIO14), SPI-Takt zum MAX6675
#define MAX_CS 12  // D6 (GPIO12), Auswahl des MAX6675
#define MAX_SO 13  // D7 (GPIO13), Messdaten vom MAX6675

// OLED-Display: I2C-Daten- und Taktleitung fuer Wire.begin(SDA, SCL).
#define OLED_SDA 4 // D2 (GPIO4), I2C-Datenleitung
#define OLED_SCL 5 // D1 (GPIO5), I2C-Taktleitung

// Drehencoder: CLK wird digital als Flankensignal gelesen; DT wird analog an
// A0 ausgewertet. Fuer CLK und DT sind stabile HIGH-Pegel erforderlich.
#define ENC_CLK 16 // D0 (GPIO16), Encoder-Takt/Flankeneingang
#define ENC_DT A0  // A0, Encoder-Datenrichtung, analog gelesen

// Encoder-Taster gegen GND. INPUT_PULLUP haelt den Eingang im Ruhezustand
// HIGH. GPIO0 ist zugleich FLASH/Boot-Strap-Pin: beim Einschalten muss der
// Taster losgelassen sein, sonst startet der ESP8266 im Flash-Modus.
#define ENC_SW 0 // D3 (GPIO0), Encoder-Taster / FLASH-Taste

// ADC-Schwelle (10-Bit: 0..1023) fuer die DT-Pegelerkennung.
#define ENC_ANALOG_THRESHOLD 512

// Regelung
constexpr unsigned long WINDOW_SIZE = 1000; // Slow-PWM Fenster in ms
constexpr double SETPOINT_MIN = 0.0;
constexpr double SETPOINT_NORMAL_MAX = 190.0;
constexpr double SETPOINT_ABSOLUTE_MAX = 200.0;
constexpr double MAX_TEMP = 215.0;
constexpr double AUTOTUNE_MAX_TEMP = 120.0;
constexpr double AUTOTUNE_MAX_START_TEMP = 40.0;
constexpr double AUTOTUNE_SETPOINT_MIN = 20.0;
constexpr double AUTOTUNE_SETPOINT_MAX = 80.0;
constexpr unsigned long AUTOTUNE_TIMEOUT = 10UL * 60UL * 1000UL;
constexpr double AUTOTUNE_KP_MAX = 20.0;
constexpr double AUTOTUNE_KI_MAX = 1.0;
constexpr double AUTOTUNE_KD_MAX = 500.0;
constexpr double FAULT_RESET_MAX_TEMP = 60.0;
constexpr unsigned long SENSOR_TIMEOUT = 750;
constexpr unsigned long SENSOR_INTERVAL = 250; // MAX6675 braucht ~220 ms
// Ist der Abstand zwischen zwei update()-Aufrufen so gross, wurde der Loop blockiert
// (z. B. WLAN). Das ist kein Sensorfehler: SSR aus und sofort neu messen.
constexpr unsigned long LOOP_STALL_THRESHOLD = 400;

// Langzeitdiagramm: 2 h bei einem Punkt alle 5 s. Hochaufgeloeste Testdaten
// werden separat ueber die serielle Schnittstelle mit 100-ms-Zeitbasis erfasst.
constexpr int HISTORY_SIZE = 1440;
constexpr unsigned long HISTORY_INTERVAL = 5000;
constexpr unsigned long DIAGNOSTIC_INTERVAL = 100;
