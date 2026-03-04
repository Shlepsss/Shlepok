# Shlepok
## ATMEGA328 compatible program that have:

Simple calculator with float results support

.TXT text reader from SD card

## Program features:
Scrolling in long text files

Only 3 buttons need to use device.

Uses 22KB Flash and 1.5KB RAM.

Uses I<sup>2</sup>C 128x64 monochrome OLED display.

## Pins (if on bare chip):
If on Arduino plate, then skip power pins.

### Power:
```
VCC - AVCC
GND - AGND
VCC - 10k - RESET
```

### SD:
```
D9 - CS (Chip Select)
D11 - MOSI
D12- MISO
D13 - SD clock (SCK)
```

### Buttons:
```
D5 - BTN UP
D6 - BTN DOWN
D7 - BTN SET
```

### OLED:
```
A5 - SCL
A4 - SDA
```

Thanks to `@AlexGyver`
