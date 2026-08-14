#ifndef SINOVOX_H
#define SINOVOX_H

#include <Arduino.h>

class SinoVox
{
public:
  // Sentence storage is deliberately capped at 200 bytes.  The generated
  // Sinovox parameter prefix still fits comfortably in the module's frame.
  static constexpr size_t MAX_TEXT_BYTES = 200;

  enum class Result
  {
    Ok,
    Busy,
    TooLong
  };

  SinoVox(uint8_t busyPin, uint8_t resetPin);

  void begin();
  void reset();
  void service();
  bool ready();
  bool speaking() const;
  Result speak(const char *text);
  Result stop();

  static const char *resultText(Result result);

private:
  static constexpr size_t MAX_FRAME_TEXT_BYTES = 253;

  uint8_t busyPin_;
  uint8_t resetPin_;
  bool speaking_;
  bool busySeen_;
};

#endif
