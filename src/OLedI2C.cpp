#include "OLedI2C.h"

#include <Wire.h>

namespace
{
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t OLED_COMMAND_MODE = 0x80;
constexpr uint8_t OLED_DATA_MODE = 0x40;
}

void OLedI2C::command(uint8_t value)
{
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write(OLED_COMMAND_MODE);
  Wire.write(value);
  Wire.endTransmission();
  delay(10);
}

void OLedI2C::data(uint8_t value)
{
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write(OLED_DATA_MODE);
  Wire.write(value);
  Wire.endTransmission();
}

void OLedI2C::init()
{
  delay(100);
  command(0x2A);
  command(0x71);
  command(0x5C);
  command(0x28);
  command(0x08);
  command(0x2A);
  command(0x79);
  command(0xD5);
  command(0x70);
  command(0x78);
  command(0x08);
  command(0x06);
  command(0x2A);
  command(0x79);
  command(0x72);
  command(0x00);
  command(0xDA);
  command(0x10);
  command(0x81);
  command(0xFF);
  command(0xDB);
  command(0x30);
  command(0xDC);
  command(0x03);
  command(0x78);
  command(0x28);
  command(0x2A);
  command(0x06);
  command(0x08);
  command(0x28);
  command(0x01);
  command(0x80);
  delay(100);
  command(0x0C);
}

void OLedI2C::setCursor(uint8_t col, uint8_t row)
{
  static const uint8_t rowOffsets[] = {0x00, 0x40};
  command(0x80 | (col + rowOffsets[row > 0 ? 1 : 0]));
}

void OLedI2C::clear()
{
  command(0x01);
}

void OLedI2C::sendString(const char *text, uint8_t col, uint8_t row)
{
  setCursor(col, row);
  while (*text)
  {
    data(static_cast<uint8_t>(*text++));
  }
}

