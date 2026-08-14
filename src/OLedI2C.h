#ifndef OLED_I2C_H
#define OLED_I2C_H

#include <Arduino.h>

class OLedI2C
{
public:
  void init();
  void clear();
  void sendString(const char *text, uint8_t col, uint8_t row);

private:
  void command(uint8_t value);
  void data(uint8_t value);
  void setCursor(uint8_t col, uint8_t row);
};

#endif

