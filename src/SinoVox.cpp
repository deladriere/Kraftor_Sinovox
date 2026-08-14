#include "SinoVox.h"

// The Kraftor board package exposes the mikroBUS UART as Serial5. Serial1 is
// intentionally kept free for 31.25 kbaud DIN MIDI.
extern Uart Serial5;

SinoVox::SinoVox(uint8_t busyPin, uint8_t resetPin)
    : busyPin_(busyPin), resetPin_(resetPin), speaking_(false), busySeen_(false)
{
}

void SinoVox::begin()
{
  pinMode(busyPin_, INPUT_PULLUP);
  pinMode(resetPin_, OUTPUT);
  digitalWrite(resetPin_, HIGH);
  Serial5.begin(115200);
  reset();
}

void SinoVox::reset()
{
  digitalWrite(resetPin_, LOW);
  delay(50);
  digitalWrite(resetPin_, HIGH);
  delay(500);

  while (Serial5.available() > 0) Serial5.read();
  speaking_ = false;
  busySeen_ = false;
}

void SinoVox::service()
{
  while (Serial5.available() > 0)
  {
    // The module returns ASCII 'O' (0x4F) when synthesis is complete.
    if (Serial5.read() == 0x4F)
    {
      speaking_ = false;
      busySeen_ = false;
    }
  }

  // The original Polaxis Sinovox code treats BSY HIGH as speaking and LOW as
  // ready.  Also accept the physical HIGH-to-LOW cycle as completion so the
  // driver does not depend on the module's UART reply wire.
  const bool hardwareBusy = digitalRead(busyPin_) == HIGH;
  if (speaking_ && hardwareBusy) busySeen_ = true;
  if (speaking_ && busySeen_ && !hardwareBusy)
  {
    speaking_ = false;
    busySeen_ = false;
  }
}

bool SinoVox::ready()
{
  service();
  // BSY is HIGH during synthesis and LOW when the module can accept text.
  return !speaking_ && digitalRead(busyPin_) == LOW;
}

bool SinoVox::speaking() const
{
  return speaking_ || digitalRead(busyPin_) == HIGH;
}

SinoVox::Result SinoVox::speak(const char *text)
{
  if (!ready()) return Result::Busy;

  const size_t textLength = strlen(text);
  if (textLength > MAX_FRAME_TEXT_BYTES) return Result::TooLong;

  // Sinovox/XFS frame: FD, 16-bit payload length, command 01, encoding 00,
  // then text. The payload contains the two command/encoding bytes plus text.
  const uint16_t payloadLength = static_cast<uint16_t>(textLength + 2);
  Serial5.write(static_cast<uint8_t>(0xFD));
  Serial5.write(static_cast<uint8_t>(payloadLength >> 8));
  Serial5.write(static_cast<uint8_t>(payloadLength & 0xFF));
  Serial5.write(static_cast<uint8_t>(0x01));
  Serial5.write(static_cast<uint8_t>(0x00));
  Serial5.write(reinterpret_cast<const uint8_t *>(text), textLength);
  speaking_ = true;
  busySeen_ = false;
  return Result::Ok;
}

SinoVox::Result SinoVox::stop()
{
  reset();
  return Result::Ok;
}

const char *SinoVox::resultText(Result result)
{
  switch (result)
  {
    case Result::Ok: return "ok";
    case Result::Busy: return "busy";
    case Result::TooLong: return "too-long";
  }
  return "unknown";
}
