#pragma once

namespace HardwareTest
{
  // Interaktiver Test aller Komponenten (OLED, MAX6675, SSR, Drehencoder).
  // Kehrt nie zurueck; muss in setup() aufgerufen werden.
  void run();
}
