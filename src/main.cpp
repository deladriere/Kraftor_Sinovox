#include "OLedI2C.h"
#include "SinoVox.h"

#include <Arduino.h>
#include <MIDI.h>
#include <USB-MIDI.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

namespace
{
constexpr char VERSION[] = "1.2";
constexpr char PRODUCT_MODEL[] = "SINOVOX";

constexpr uint8_t GREEN_LED = 12;
constexpr uint8_t RED_LED = 13;
constexpr uint8_t CS_PIN = 5;
constexpr uint8_t TTS_RESET_PIN = 4;
constexpr uint8_t TTS_BUSY_PIN = 11;
constexpr uint8_t DIP1 = 2;
constexpr uint8_t DIP2 = 3;
constexpr uint8_t DIP3 = 6;
constexpr uint8_t DIP4 = 7;

constexpr uint8_t MIDI_CC_VOICE = 1;
constexpr uint8_t MIDI_CC_SPEED = 2;
constexpr uint8_t MIDI_CC_VOLUME = 3;
constexpr uint8_t MIDI_CC_LANGUAGE = 4;
constexpr uint8_t MIDI_CC_PITCH = 5;

constexpr uint8_t RAM_SLOTS = 20;
constexpr uint8_t FRAM_SLOTS = 100;
constexpr size_t SENTENCE_MAX = SinoVox::MAX_TEXT_BYTES;
constexpr uint8_t RAM_BASE_NOTE = 28;
constexpr uint8_t FRAM_FIRST_MIDI_NOTE = RAM_BASE_NOTE + RAM_SLOTS;
constexpr uint8_t FRAM_MIDI_NOTE_SPAN = 128 - FRAM_FIRST_MIDI_NOTE;

constexpr uint8_t FRAM_I2C_ADDRESS = 0x50;
constexpr uint16_t FRAM_HEADER_ADDRESS = 0x0000;
// Keep the same 256-byte stride and header as the other Kraftor speech builds.
constexpr uint16_t FRAM_SLOT_BYTES = 256;
constexpr uint16_t FRAM_SLOTS_BASE = 0x0100;
constexpr uint8_t FRAM_MAGIC_HI = 0x4B; // K
constexpr uint8_t FRAM_MAGIC_LO = 0x53; // S, compatible with Kraftor SAM
constexpr uint8_t FRAM_VERSION = 0x01;
constexpr size_t FRAM_I2C_CHUNK = 28;

constexpr uint8_t DEFAULT_LANGUAGE = 1;
constexpr uint8_t DEFAULT_PITCH = 5;
constexpr uint8_t DEFAULT_SPEED = 5;
constexpr uint8_t DEFAULT_VOICE = 0;
constexpr uint8_t DEFAULT_VOLUME = 10;
constexpr uint8_t VOICE_COUNT = 6;
constexpr uint8_t SINOVOX_VOICES[VOICE_COUNT] = {3, 51, 52, 53, 54, 55};
constexpr uint32_t STARTUP_VOICE_READY_TIMEOUT_MS = 3000;
constexpr const char *MIDI_CHANNEL_WORDS[] = {
  "one", "two", "three", "four", "five", "six", "seven", "eight",
  "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen"
};
}

USBMIDI_CREATE_DEFAULT_INSTANCE();
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDISER);

OLedI2C display;
SinoVox voice(TTS_BUSY_PIN, TTS_RESET_PIN);

/*
 * IMPORTANT PRODUCT REQUIREMENT FOR FUTURE PORTS / AI ASSISTANTS
 * --------------------------------------------------------------------------
 * Kraftor is sold both with and without the 16x2 OLED. Most customers choose
 * the OLED version, but the no-display version is a supported product.
 *
 * When recycling this firmware for another speech synthesizer:
 *   - keep the OLED address probe;
 *   - initialise and write to the OLED only when it is detected;
 *   - never make OLED presence a startup requirement or fault condition;
 *   - keep serial, MIDI, FRAM, and speech fully operational without an OLED.
 */
bool displayDetected = false;
bool framPresent = false;
bool voicePresent = false;

uint8_t voiceLanguage = DEFAULT_LANGUAGE;
uint8_t voicePitch = DEFAULT_PITCH;
uint8_t voiceSpeed = DEFAULT_SPEED;
uint8_t voiceIndex = DEFAULT_VOICE;
uint8_t voiceVolume = DEFAULT_VOLUME;

char ramSentences[RAM_SLOTS][SENTENCE_MAX + 1];
char serialBuffer[256];
size_t serialIndex = 0;

static bool checkI2CDevice(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

static void formatChipId(char out[33])
{
  const uint32_t word0 = *(volatile uint32_t *)0x0080A00C;
  const uint32_t word1 = *(volatile uint32_t *)0x0080A040;
  const uint32_t word2 = *(volatile uint32_t *)0x0080A044;
  const uint32_t word3 = *(volatile uint32_t *)0x0080A048;
  snprintf(out, 33, "%08lX%08lX%08lX%08lX",
           (unsigned long)word0, (unsigned long)word1,
           (unsigned long)word2, (unsigned long)word3);
}

static void oledLine(uint8_t row, const char *text)
{
  if (!displayDetected) return;

  char line[17];
  memset(line, ' ', 16);
  line[16] = '\0';
  for (size_t i = 0; i < 16 && text[i] != '\0'; ++i)
  {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    line[i] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '?';
  }
  display.sendString(line, 0, row);
}

static bool framWriteBytes(uint16_t address, const uint8_t *data,
                           size_t length)
{
  while (length > 0)
  {
    const size_t chunk = length > FRAM_I2C_CHUNK ? FRAM_I2C_CHUNK : length;
    Wire.beginTransmission(FRAM_I2C_ADDRESS);
    Wire.write(static_cast<uint8_t>(address >> 8));
    Wire.write(static_cast<uint8_t>(address & 0xFF));
    const size_t written = Wire.write(data, chunk);
    const uint8_t busStatus = Wire.endTransmission();
    if (written != chunk || busStatus != 0)
    {
      return false;
    }
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  return true;
}

static bool framReadBytes(uint16_t address, uint8_t *data, size_t length)
{
  while (length > 0)
  {
    const size_t chunk = length > FRAM_I2C_CHUNK ? FRAM_I2C_CHUNK : length;
    Wire.beginTransmission(FRAM_I2C_ADDRESS);
    Wire.write(static_cast<uint8_t>(address >> 8));
    Wire.write(static_cast<uint8_t>(address & 0xFF));
    if (Wire.endTransmission(false) != 0)
    {
      return false;
    }
    if (Wire.requestFrom((int)FRAM_I2C_ADDRESS, (int)chunk) != chunk)
    {
      return false;
    }
    for (size_t i = 0; i < chunk; ++i)
    {
      if (!Wire.available()) return false;
      *data++ = Wire.read();
    }
    address += chunk;
    length -= chunk;
  }
  return true;
}

static uint16_t framSlotAddress(uint8_t slot)
{
  return FRAM_SLOTS_BASE + static_cast<uint16_t>(slot) * FRAM_SLOT_BYTES;
}

static bool framReadSentence(uint8_t slot, char *out, size_t outSize)
{
  if (!framPresent || slot >= FRAM_SLOTS || outSize == 0) return false;
  out[0] = '\0';

  uint8_t length = 0;
  if (!framReadBytes(framSlotAddress(slot), &length, 1)) return false;
  if (length == 0 || length == 0xFF) return true;
  if (length > SENTENCE_MAX) length = SENTENCE_MAX;
  if (length >= outSize) length = outSize - 1;
  if (!framReadBytes(framSlotAddress(slot) + 1,
                     reinterpret_cast<uint8_t *>(out), length))
  {
    return false;
  }
  out[length] = '\0';
  return true;
}

static bool framWriteSentence(uint8_t slot, const char *text)
{
  if (!framPresent || slot >= FRAM_SLOTS) return false;
  size_t length = strlen(text);
  if (length > SENTENCE_MAX) return false;

  uint8_t buffer[SENTENCE_MAX + 1];
  buffer[0] = static_cast<uint8_t>(length);
  memcpy(buffer + 1, text, length);
  if (!framWriteBytes(framSlotAddress(slot), buffer, length + 1)) return false;

  uint8_t verify = 0xFF;
  return framReadBytes(framSlotAddress(slot), &verify, 1) && verify == length;
}

static bool framEraseSentence(uint8_t slot)
{
  const uint8_t empty = 0;
  return framPresent && slot < FRAM_SLOTS &&
         framWriteBytes(framSlotAddress(slot), &empty, 1);
}

static bool framHeaderValid()
{
  uint8_t header[3];
  return framReadBytes(FRAM_HEADER_ADDRESS, header, sizeof(header)) &&
         header[0] == FRAM_MAGIC_HI && header[1] == FRAM_MAGIC_LO &&
         header[2] == FRAM_VERSION;
}

static bool framFormat()
{
  const uint8_t header[] = {FRAM_MAGIC_HI, FRAM_MAGIC_LO, FRAM_VERSION};
  if (!framWriteBytes(FRAM_HEADER_ADDRESS, header, sizeof(header))) return false;
  for (uint8_t slot = 0; slot < FRAM_SLOTS; ++slot)
  {
    if (!framEraseSentence(slot)) return false;
  }
  return true;
}

static bool textIsAscii(const char *text)
{
  for (; *text; ++text)
  {
    if (static_cast<uint8_t>(*text) > 0x7F) return false;
  }
  return true;
}

static void replyError(const char *message)
{
  Serial.print("!ERR ");
  Serial.println(message);
}

static bool speakText(const char *text)
{
  if (!voicePresent)
  {
    replyError("notts");
    oledLine(1, "TTS not found");
    return false;
  }
  if (strlen(text) > SENTENCE_MAX)
  {
    replyError("too-long");
    return false;
  }
  if (!textIsAscii(text))
  {
    replyError("ascii-required");
    return false;
  }
  if (!voice.ready())
  {
    replyError("busy");
    return false;
  }
  char message[256];
  const int written = snprintf(message, sizeof(message),
                               "[i%u][h2][t%u][s%u][m%u][v%u] %s",
                               voiceLanguage, voicePitch, voiceSpeed,
                               SINOVOX_VOICES[voiceIndex], voiceVolume, text);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(message))
  {
    replyError("too-long");
    return false;
  }

  const SinoVox::Result result = voice.speak(message);
  if (result != SinoVox::Result::Ok)
  {
    replyError(SinoVox::resultText(result));
    return false;
  }

  Serial.print("Speak: ");
  Serial.println(text);
  oledLine(1, text);
  return true;
}

static void announceStartup(uint8_t midiChannel)
{
  const uint32_t startedAt = millis();
  while (!voice.ready() && millis() - startedAt < STARTUP_VOICE_READY_TIMEOUT_MS)
  {
    delay(10);
  }

  char announcement[64];
  if (midiChannel == MIDI_CHANNEL_OMNI)
  {
    snprintf(announcement, sizeof(announcement), "Kraftor Sinovox. MIDI omni.");
  }
  else
  {
    snprintf(announcement, sizeof(announcement), "Kraftor Sinovox. MIDI channel %s.",
             MIDI_CHANNEL_WORDS[midiChannel - 1]);
  }

  // Identification speech is always English, without changing the user's
  // selected language for subsequent sentences.
  const uint8_t previousLanguage = voiceLanguage;
  voiceLanguage = 0;
  speakText(announcement);
  voiceLanguage = previousLanguage;
}

static bool playRamSlot(uint8_t slot)
{
  if (slot >= RAM_SLOTS) return false;
  if (ramSentences[slot][0] == '\0')
  {
    replyError("empty");
    oledLine(1, "(empty)");
    return false;
  }
  return speakText(ramSentences[slot]);
}

static bool playFramSlot(uint8_t slot)
{
  char text[SENTENCE_MAX + 1];
  if (!framReadSentence(slot, text, sizeof(text)))
  {
    replyError("read");
    return false;
  }
  if (text[0] == '\0')
  {
    replyError("empty");
    oledLine(1, "(empty)");
    return false;
  }
  return speakText(text);
}

static void handleNoteOn(byte channel, byte note, byte velocity)
{
  (void)channel;
  (void)velocity;

  char status[17];
  if (note >= RAM_BASE_NOTE && note < RAM_BASE_NOTE + RAM_SLOTS)
  {
    const uint8_t slot = note - RAM_BASE_NOTE;
    snprintf(status, sizeof(status), "MIDI %u R%u", note, slot);
    oledLine(0, status);
    playRamSlot(slot);
    return;
  }
  if (framPresent && note >= FRAM_FIRST_MIDI_NOTE)
  {
    const uint8_t slot = note - FRAM_FIRST_MIDI_NOTE;
    if (slot < FRAM_MIDI_NOTE_SPAN)
    {
      snprintf(status, sizeof(status), "MIDI %u F%u", note, slot);
      oledLine(0, status);
      playFramSlot(slot);
      return;
    }
  }

  Serial.print("MIDI note out of range: ");
  Serial.println(note);
  oledLine(1, "out of range");
}

static void handleNoteOff(byte channel, byte note, byte velocity)
{
  (void)channel;
  (void)note;
  (void)velocity;
}

static void handleControlChange(byte channel, byte control, byte value)
{
  (void)channel;
  if (control == MIDI_CC_VOICE)
  {
    voiceIndex = map(value, 0, 127, 0, VOICE_COUNT - 1);
  }
  else if (control == MIDI_CC_SPEED)
  {
    voiceSpeed = map(value, 0, 127, 0, 10);
  }
  else if (control == MIDI_CC_VOLUME)
  {
    voiceVolume = map(value, 0, 127, 0, 10);
  }
  else if (control == MIDI_CC_LANGUAGE)
  {
    voiceLanguage = value < 64 ? 0 : 1;
  }
  else if (control == MIDI_CC_PITCH)
  {
    // Sinovox reads [tPITCH] when the next utterance is submitted.
    voicePitch = map(value, 0, 127, 0, 10);
  }
}

static void skipSpaces(const char **cursor)
{
  while (**cursor == ' ' || **cursor == '\t') ++(*cursor);
}

static int parseInt(const char **cursor)
{
  skipSpaces(cursor);
  int value = 0;
  bool found = false;
  while (**cursor >= '0' && **cursor <= '9')
  {
    value = value * 10 + (**cursor - '0');
    ++(*cursor);
    found = true;
  }
  return found ? value : -1;
}

static char parseStore(const char **cursor)
{
  skipSpaces(cursor);
  const char store = **cursor;
  if (store == 'R' || store == 'F')
  {
    ++(*cursor);
    return store;
  }
  return 0;
}

static void replyInfo()
{
  char chipId[33];
  formatChipId(chipId);
  Serial.print("!INFO RAM=");
  Serial.print(RAM_SLOTS);
  Serial.print(" FRAM=");
  Serial.print(framPresent ? FRAM_SLOTS : 0);
  Serial.print(" RAMBASE=");
  Serial.print(RAM_BASE_NOTE);
  Serial.print(" MIDIFBASE=");
  Serial.print(framPresent ? FRAM_FIRST_MIDI_NOTE : 0);
  Serial.print(" MIDIFCNT=");
  Serial.print(framPresent ? FRAM_MIDI_NOTE_SPAN : 0);
  Serial.print(" MAX=");
  Serial.print(SENTENCE_MAX);
  Serial.print(" VER=");
  Serial.print(VERSION);
  Serial.print(" MODEL=");
  Serial.print(PRODUCT_MODEL);
  Serial.print(" TTS=");
  Serial.print(voicePresent ? 1 : 0);
  Serial.print(" OLED=");
  Serial.print(displayDetected ? 1 : 0);
  Serial.print(" ID=");
  Serial.println(chipId);
}

static void replyVoice()
{
  Serial.print("!VOICE LANGUAGE=");
  Serial.print(voiceLanguage);
  Serial.print(" PITCH=");
  Serial.print(voicePitch);
  Serial.print(" SPEED=");
  Serial.print(voiceSpeed);
  Serial.print(" VOICE=");
  Serial.print(voiceIndex);
  Serial.print(" SPEAKER=");
  Serial.print(SINOVOX_VOICES[voiceIndex]);
  Serial.print(" VOLUME=");
  Serial.println(voiceVolume);
}

static void replySentence(char store, int slot, const char *text)
{
  Serial.print("!SENT ");
  Serial.print(store);
  Serial.print(' ');
  Serial.print(slot);
  Serial.print(' ');
  Serial.println(text);
}

static void commandGet(char store, int slot)
{
  if (store == 'R')
  {
    if (slot < 0 || slot >= RAM_SLOTS) { replyError("range"); return; }
    replySentence(store, slot, ramSentences[slot]);
    return;
  }
  if (store == 'F')
  {
    if (!framPresent) { replyError("nofram"); return; }
    if (slot < 0 || slot >= FRAM_SLOTS) { replyError("range"); return; }
    char text[SENTENCE_MAX + 1];
    if (!framReadSentence(slot, text, sizeof(text))) { replyError("read"); return; }
    replySentence(store, slot, text);
    return;
  }
  replyError("store");
}

static void commandSet(char store, int slot, const char *text)
{
  const size_t length = strlen(text);
  if (length > SENTENCE_MAX) { replyError("too-long"); return; }
  if (!textIsAscii(text)) { replyError("ascii-required"); return; }

  if (store == 'R')
  {
    if (slot < 0 || slot >= RAM_SLOTS) { replyError("range"); return; }
    memcpy(ramSentences[slot], text, length + 1);
    Serial.println("!OK");
    return;
  }
  if (store == 'F')
  {
    if (!framPresent) { replyError("nofram"); return; }
    if (slot < 0 || slot >= FRAM_SLOTS) { replyError("range"); return; }
    if (!framWriteSentence(slot, text)) { replyError("write"); return; }
    Serial.println("!OK");
    return;
  }
  replyError("store");
}

static void commandErase(char store, int slot)
{
  if (store == 'R')
  {
    if (slot < 0 || slot >= RAM_SLOTS) { replyError("range"); return; }
    ramSentences[slot][0] = '\0';
    Serial.println("!OK");
    return;
  }
  if (store == 'F')
  {
    if (!framPresent) { replyError("nofram"); return; }
    if (slot < 0 || slot >= FRAM_SLOTS) { replyError("range"); return; }
    if (!framEraseSentence(slot)) { replyError("erase"); return; }
    Serial.println("!OK");
    return;
  }
  replyError("store");
}

static void commandPlay(char store, int slot)
{
  bool played = false;
  if (store == 'R')
  {
    if (slot < 0 || slot >= RAM_SLOTS) { replyError("range"); return; }
    played = playRamSlot(slot);
  }
  else if (store == 'F')
  {
    if (!framPresent) { replyError("nofram"); return; }
    if (slot < 0 || slot >= FRAM_SLOTS) { replyError("range"); return; }
    played = playFramSlot(slot);
  }
  else
  {
    replyError("store");
    return;
  }
  if (played) Serial.println("!OK");
}

static void serviceMidi()
{
  MIDI.read();
  MIDISER.read();
}

static void commandDump(char store)
{
  if (store == 'R')
  {
    for (uint8_t slot = 0; slot < RAM_SLOTS; ++slot)
    {
      replySentence(store, slot, ramSentences[slot]);
      serviceMidi();
    }
    Serial.println("!END R");
    return;
  }
  if (store == 'F')
  {
    if (!framPresent) { replyError("nofram"); return; }
    char text[SENTENCE_MAX + 1];
    for (uint8_t slot = 0; slot < FRAM_SLOTS; ++slot)
    {
      if (!framReadSentence(slot, text, sizeof(text))) text[0] = '\0';
      replySentence(store, slot, text);
      serviceMidi();
    }
    Serial.println("!END F");
    return;
  }
  replyError("store");
}

static void handleCommand(const char *line)
{
  const char *cursor = line + 1;
  skipSpaces(&cursor);

  if (strcmp(cursor, "INFO?") == 0) { replyInfo(); return; }
  if (strcmp(cursor, "VOICE?") == 0) { replyVoice(); return; }
  if (strcmp(cursor, "STOP") == 0)
  {
    if (!voicePresent) { replyError("notts"); return; }
    const SinoVox::Result result = voice.stop();
    if (result == SinoVox::Result::Ok) Serial.println("!OK");
    else replyError(SinoVox::resultText(result));
    return;
  }
  if (strncmp(cursor, "VOICE ", 6) == 0)
  {
    cursor += 6;
    const int language = parseInt(&cursor);
    const int pitch = parseInt(&cursor);
    const int speed = parseInt(&cursor);
    const int voiceChoice = parseInt(&cursor);
    const int volume = parseInt(&cursor);
    skipSpaces(&cursor);
    if (language < 0 || language > 1 || pitch < 0 || pitch > 10 ||
        speed < 0 || speed > 10 || voiceChoice < 0 ||
        voiceChoice >= VOICE_COUNT || volume < 0 || volume > 10 ||
        *cursor != '\0')
    {
      replyError("voice-range");
      return;
    }
    voiceLanguage = language;
    voicePitch = pitch;
    voiceSpeed = speed;
    voiceIndex = voiceChoice;
    voiceVolume = volume;
    Serial.println("!OK");
    return;
  }

  if (strncmp(cursor, "GET ", 4) == 0)
  {
    cursor += 4;
    const char store = parseStore(&cursor);
    const int slot = parseInt(&cursor);
    commandGet(store, slot);
    return;
  }
  if (strncmp(cursor, "SET ", 4) == 0)
  {
    cursor += 4;
    const char store = parseStore(&cursor);
    const int slot = parseInt(&cursor);
    skipSpaces(&cursor);
    commandSet(store, slot, cursor);
    return;
  }
  if (strncmp(cursor, "ERASE ", 6) == 0)
  {
    cursor += 6;
    const char store = parseStore(&cursor);
    const int slot = parseInt(&cursor);
    commandErase(store, slot);
    return;
  }
  if (strncmp(cursor, "PLAY ", 5) == 0)
  {
    cursor += 5;
    const char store = parseStore(&cursor);
    const int slot = parseInt(&cursor);
    commandPlay(store, slot);
    return;
  }
  if (strncmp(cursor, "DUMP ", 5) == 0)
  {
    cursor += 5;
    commandDump(parseStore(&cursor));
    return;
  }
  replyError("unknown");
}

static void processSerialLine()
{
  serialBuffer[serialIndex] = '\0';
  if (serialIndex > 0)
  {
    if (serialBuffer[0] == '!') handleCommand(serialBuffer);
    else speakText(serialBuffer);
  }
  serialIndex = 0;
  Serial.print("> ");
}

static void readSerial()
{
  while (Serial.available() > 0)
  {
    const char incoming = Serial.read();
    // Kraftor serial tools may send CR, LF, or CR+LF. Process the first line
    // ending and ignore the second one when the buffer is already empty.
    if (incoming == '\r' || incoming == '\n')
    {
      if (serialIndex > 0) processSerialLine();
      continue;
    }
    if (incoming == '\b' || incoming == 127)
    {
      if (serialIndex > 0) --serialIndex;
      continue;
    }
    if (serialIndex < sizeof(serialBuffer) - 1)
    {
      serialBuffer[serialIndex++] = incoming;
    }
  }
}

static uint8_t readMidiChannel()
{
  uint8_t value = 0;
  value |= (!digitalRead(DIP1)) << 3;
  value |= (!digitalRead(DIP2)) << 2;
  value |= (!digitalRead(DIP3)) << 1;
  value |= !digitalRead(DIP4);
  return value == 0 ? MIDI_CHANNEL_OMNI : value;
}

static void initPins()
{
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pinMode(DIP1, INPUT_PULLUP);
  pinMode(DIP2, INPUT_PULLUP);
  pinMode(DIP3, INPUT_PULLUP);
  pinMode(DIP4, INPUT_PULLUP);
}

void setup()
{
  initPins();
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);
  voice.begin();

  for (uint8_t slot = 0; slot < RAM_SLOTS; ++slot)
  {
    ramSentences[slot][0] = '\0';
  }

  // The OLED is present on most units, but the no-display product variant must
  // continue to provide serial, MIDI, FRAM, and speech features without it.
  displayDetected = checkI2CDevice(0x3C);
  if (displayDetected)
  {
    display.init();
    oledLine(0, "KRAFTOR SINOVOX");
    oledLine(1, "Starting...");
  }

  framPresent = checkI2CDevice(FRAM_I2C_ADDRESS);
  if (framPresent && !framHeaderValid())
  {
    Serial.println("FRAM header missing; formatting sentence bank");
    framPresent = framFormat();
  }

  // Sinovox has no identity-query command. Serial5, reset and BSY are ready;
  // physical module operation is confirmed by its BSY/completion response.
  voicePresent = true;

  const uint8_t midiChannel = readMidiChannel();
  MIDI.begin(midiChannel);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleControlChange);
  MIDISER.begin(midiChannel);
  MIDISER.setHandleNoteOn(handleNoteOn);
  MIDISER.setHandleNoteOff(handleNoteOff);
  MIDISER.setHandleControlChange(handleControlChange);

  Serial.println();
  Serial.print("Kraftor Sinovox ");
  Serial.println(VERSION);
  Serial.print("OLED: ");
  Serial.println(displayDetected ? "found" : "not installed");
  Serial.print("FRAM RC256: ");
  Serial.println(framPresent ? "found" : "not found");
  Serial.println("Sinovox: Serial5 115200");
  Serial.print("MIDI channel: ");
  if (midiChannel == MIDI_CHANNEL_OMNI) Serial.println("Omni");
  else Serial.println(midiChannel);
  replyInfo();
  Serial.println("Enter Sinovox Pinyin or English text, then press Return.");
  Serial.print("> ");

  char channelLine[17];
  if (midiChannel == MIDI_CHANNEL_OMNI) snprintf(channelLine, sizeof(channelLine), "MIDI OMNI");
  else snprintf(channelLine, sizeof(channelLine), "MIDI CH %u", midiChannel);
  oledLine(0, "SINOVOX READY");
  oledLine(1, channelLine);

  if (voicePresent)
  {
    announceStartup(midiChannel);
  }
}

void loop()
{
  readSerial();
  serviceMidi();
  voice.service();

  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, voice.speaking() ? HIGH : LOW);
}
