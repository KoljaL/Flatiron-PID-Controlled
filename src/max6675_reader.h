#pragma once

#include <Arduino.h>

namespace Max6675Reader
{
  struct Reading
  {
    uint16_t raw;
    double celsius;
    bool valid;
  };

  void begin();
  Reading read();
}
