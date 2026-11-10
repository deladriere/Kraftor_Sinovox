# Kraftor Sinovox

SAMD21 firmware v1.3 for Polaxis Kraftor with a Sino Vox board.

- [Download the UF2 firmware](https://github.com/deladriere/Kraftor_Sinovox/releases/latest)
- [Open Sinovox Designer](https://deladriere.github.io/Sinovox_designer/)
- [Designer user manual](https://deladriere.github.io/Sinovox_designer/user_manual.html)
- [Kraftor guide](https://www.polaxis.be/sinovox-serial-midi-for-kraftor/)

Kraftor Sinovox is firmware for Kraftor driving a Sinovox speech module.
It accepts Pinyin or English text over USB
serial, stores sentences in RAM or optional I2C FRAM, and recalls them from
USB MIDI or TRS MIDI.

The firmware generates the Sinovox parameter tags. Send or store only the text
to speak; do not add the `[i...][t...][s...][m...][v...]` prefix yourself.

## Hardware

| Function                  | Connection                                      |
| ------------------------- | ----------------------------------------------- |
| Sinovox data              | Kraftor `Serial5` TX to Sinovox RX, 115200 baud |
| Sinovox reset             | D4, active low                                  |
| Sinovox busy              | D11, HIGH while speaking, LOW when ready        |
| 16x2 character OLED       | I2C `0x3C`, optional product variant            |
| RC256 FRAM                | I2C `0x50`, optional                            |
| DIN MIDI                  | `Serial1`, 31250 baud                           |
| MIDI channel DIP switches | D2, D3, D6, D7                                  |

The OLED is probed at startup. Serial, MIDI, FRAM and speech continue to work
when the display is not installed.

## Speech settings

The generated Sinovox message is:

```text
[iLANGUAGE][h2][tPITCH][sSPEED][mSPEAKER][vVOLUME] text
```

At startup, the firmware waits briefly for the Sinovox module, identifies
itself in English, and announces the MIDI channel selected by DIP1-DIP4 (or
"MIDI omni"). The user's selected language remains unchanged.

Settings and ranges:

| Setting     | Range                           | Default |
| ----------- | ------------------------------- | ------- |
| Language    | `0` English, `1` Chinese/Pinyin | `1`     |
| Pitch       | `0`-`10`                        | `5`     |
| Speed       | `0`-`10`                        | `5`     |
| Voice index | `0`-`5`                         | `0`     |
| Volume      | `0`-`10`                        | `10`    |

Voice indices select the Sinovox speaker IDs `3, 51, 52, 53, 54, 55`.

Sentences are limited to 200 ASCII bytes. If a new sentence or MIDI note
arrives while Sinovox is speaking, the firmware returns `!ERR busy`. `!STOP`
resets the speech module and interrupts the current utterance.

## Serial commands

Plain text is spoken immediately when Return is pressed. CR, LF and CR+LF line
endings are accepted. Commands start with `!`:

```text
hello world
!INFO?
!VOICE?
!VOICE 0 5 5 0 10
!STOP
!SET R 0 0 hello world
!GET R 0
!PLAY R 0
!ERASE R 0
!DUMP R
!SET F 0 1 ni3hao3
!GET F 0
!PLAY F 0
!ERASE F 0
!DUMP F
```

The five `!VOICE` values are language, pitch, speed, voice index and volume.
Each `!SET` command includes the sentence language after the slot: `0` for
English or `1` for Chinese/Pinyin. `!GET` and `!DUMP` replies use
`!SENT STORE SLOT LANGUAGE TEXT`.
RAM provides 20 volatile slots. An installed RC256 provides 100 persistent
FRAM slots with 256-byte spacing. Each slot stores one language byte, one
length byte, up to 200 ASCII text bytes and 54 bytes reserved for later use.
RAM slots store the same language information alongside their temporary text.

## MIDI

The four DIP switches are read once at startup. Value 0 selects Omni; values
1-15 select that MIDI channel. Reset or power-cycle after changing them. The
same channel selection applies to USB MIDI and DIN MIDI.

| MIDI input   | Result                                               |
| ------------ | ---------------------------------------------------- |
| Notes 28-47  | Play RAM slots 0-19                                  |
| Notes 48-127 | Play FRAM slots 0-79                                 |
| CC 1         | Voice index 0-5                                      |
| CC 2         | Speed 0-10                                           |
| CC 3         | Volume 0-10                                          |
| CC 4         | Language: values 0-63 English, 64-127 Chinese/Pinyin |
| CC 5         | Pitch 0-10                                           |

FRAM slots 80-99 remain available through USB serial commands but cannot fit
in the MIDI note-number range.

Voice, speed, volume and pitch changes apply to the next utterance. Direct text
uses the current language, while stored RAM and FRAM sentences always use the
language saved with that slot. These controls do not alter speech already in
progress.

## Build and upload

The project includes the Kraftor board variant under `variants/kraftor`.
It defines the mikroBUS `Serial5` UART, so no changes to the installed
Arduino framework are needed. PlatformIO and library versions are pinned.

Back up existing FRAM phrases before changing firmware: v1.3 initializes
FRAM when its header does not match this firmware's format.

From this directory:

```sh
pio run
pio run -t upload
```

The post-build helper creates `Kraftor_Sinovox_1.3.uf2` at application address `0x2000`.
A normal `pio run` does not write to connected hardware. To install, enter
Kraftor's UF2 bootloader and copy `Kraftor_Sinovox_1.3.uf2` to its drive, or explicitly
run `pio run -t upload`.

## Release validation

The publication build is checked against the existing v1.3 UF2 payload.
This release packages the existing firmware; no new physical speech, MIDI,
or FRAM test has been performed as part of publication.

## Third-party components

The Kraftor variant derives from Arduino's SAMD variant and retains its
LGPL-2.1-or-later notices. `uf2conv.py` and `uf2families.json` are the existing
Microsoft UF2 conversion tools and retain their original notices.
Arduino core and MIDI libraries are installed by PlatformIO under their
respective upstream licences.
