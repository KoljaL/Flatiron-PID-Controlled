#include "max6675_reader.h"
#include "config.h"

namespace Max6675Reader
{
  void begin()
  {
    pinMode(MAX_SCK, OUTPUT);
    pinMode(MAX_CS, OUTPUT);
    pinMode(MAX_SO, INPUT);
    digitalWrite(MAX_SCK, LOW);
    digitalWrite(MAX_CS, HIGH);
  }

  Reading read()
  {
    digitalWrite(MAX_CS, LOW);
    delayMicroseconds(1);

    uint16_t raw = 0;
    for (uint8_t bit = 0; bit < 16; ++bit)
    {
      digitalWrite(MAX_SCK, LOW);
      delayMicroseconds(1);
      raw = static_cast<uint16_t>((raw << 1) | (digitalRead(MAX_SO) ? 1 : 0));
      digitalWrite(MAX_SCK, HIGH);
      delayMicroseconds(1);
    }
    digitalWrite(MAX_CS, HIGH);
    digitalWrite(MAX_SCK, LOW);

    const bool valid = (raw & 0x8006) == 0;
    const double celsius = static_cast<double>((raw >> 3) & 0x0FFF) * 0.25;
    return {raw, celsius, valid};
  }
}
