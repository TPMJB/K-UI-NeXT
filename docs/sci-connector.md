# SCI connector plan: microSD, W5500 and Wi-Fi

**Plan; only the W5500 part exists today.** This is the wiring to solder once,
so that a microSD card, the W5500 and later a Wi-Fi board all plug into the
same SCI port. Today's build still expects the W5500 alone, with its chip
select on GPIO7 ([FTP server](ftp.md)). The software for everything else
below is planned, not written. Solder-point details are still to be checked
against photos of the owner's VA1 board before anyone solders.

## One bus, several chip selects

The SH-4's SCI port, run as SPI, is one shared bus: clock, data out and data
in go to every device, and each device gets its own chip-select line. The
SCI's clock rests high between transfers (SPI mode 3). The W5500, SD cards
and the ESP32-C5 all accept that. (TI's CC3135 was considered and set aside:
TI documents its SPI as mode 0 only.)

## Console solder points (VA1)

From the published W5500 SCI wiring and the SCI-SPI mod notes:

| Signal | Solder point |
| --- | --- |
| GND | CE113, left side |
| 3.3 V | CE113, right side (+) |
| MISO (SCI RXD) | R115 |
| MOSI (SCI TXD) | R122 |
| SCLK (SCI SCK) | R140 |
| GPIO7 (PA7) | RA101, the usual chip-select point |
| GPIO0, GPIO5, GPIO6 | other RA101 pins; which pin is which is still to be confirmed |
| 5 V | the drive connector's 5 V pins (A3/B3) or another point chosen from photos |

Check every point with a multimeter before soldering.

## The connector

A keyed 10-pin connector (for example a 2×5 shrouded header) takes one
network board, either the W5500 or the Wi-Fi board:

| Pin | Signal | Notes |
| --- | --- | --- |
| 1 | 5 V | powers the Wi-Fi board |
| 2 | GND | |
| 3 | 3.3 V | powers the W5500 |
| 4 | GND | |
| 5 | SCLK | shared |
| 6 | MOSI | shared |
| 7 | MISO | shared |
| 8 | Network chip select (GPIO6) | 10 kΩ pull-up to 3.3 V |
| 9 | Ready (GPIO5), active low | 10 kΩ pull-up to 3.3 V |
| 10 | Reset (GPIO0), active low | 10 kΩ pull-up to 3.3 V |

The pull-ups keep every device deselected and running while the console
boots and while a game runs. The microSD card sits on the same SCLK, MOSI
and MISO lines with its own chip select, GPIO7, also pulled up. GPIO7 is
what KallistiOS's SD-over-SCI driver uses, so the SD card gets it and the
W5500 moves to GPIO6.

## The devices

### microSD

An Adafruit Micro SD breakout, 3V only (#4682), or a bare microSD socket
with a 10 µF and a 100 nF capacitor at the card. No level shifter: the SCI
pins are 3.3 V. Avoid the blue 6-pin Arduino modules; their buffer never
releases MISO and their regulator expects 5 V. The SCI-SPI mod notes
recommend powering the card from 5 V through a small 5 V to 3.3 V DC-DC
converter rather than the console's 3.3 V supply.

### W5500

| W5500 module | Connector |
| --- | --- |
| MOSI, MISO, SCLK | pins 6, 7, 5 |
| SCSn | pin 8, network chip select |
| INTn | pin 9, ready (optional; K-UI polls) |
| RSTn | pin 10, reset |
| 3.3 V, GND | pins 3, 4 |

### Wi-Fi: Seeed XIAO ESP32-C5

Dual-band (2.4 and 5 GHz) Wi-Fi 6 with WPA3, 21 × 17.8 mm, powered from
5 V. It needs K-UI's own firmware, loaded once over its USB-C port; after
that, K-UI updates it. Pins D2 and D3 are ESP32-C5 strapping pins (GPIO25 and
GPIO7), so they are left unused.

| XIAO ESP32-C5 | Connector |
| --- | --- |
| D8 (GPIO8) | pin 5, SCLK |
| D10 (GPIO10) | pin 6, MOSI |
| D9 (GPIO9) | pin 7, MISO |
| D0 (GPIO1) | pin 8, network chip select |
| D1 (GPIO0) | pin 9, ready |
| D4 (GPIO23) | pin 10, reset request (optional) |
| 5V | pin 1 |
| GND | pin 2 |

- **Do not connect the XIAO's 3V3 pin.** It makes its own 3.3 V; tying it to
  the console's 3.3 V would put two regulators against each other.
- **Antenna.** The board has a U.FL connector and no antenna of its own.
  Reviews say it ships with a 2.4 GHz antenna, so use a dual-band
  2.4/5 GHz U.FL antenna, placed outside the console's metal shielding (on
  the case, or through the modem bay with a U.FL to RP-SMA lead).
- **Power.** Put a capacitor of about 220 µF next to its 5V pin to cover the
  short current spikes when it transmits.
- The XIAO's reset button is not on its pins. The reset-request line asks
  K-UI's firmware to restart itself; the ESP32-C5's own watchdog covers a
  hang.

## Software still to write

1. A shared SCI bus layer: one owner for the SCI port, a lock so devices take
   turns, each device's own clock speed (SD cards start slowly), chip
   selects on GPIO7 and GPIO6, and the ready and reset lines.
2. The W5500 on GPIO6.
3. The microSD on GPIO7 in K-UI (through KallistiOS's SD-over-SCI driver)
   and in the game loader (an independent SCI reader).
4. The XIAO ESP32-C5 firmware: SPI in mode 3, Wi-Fi setup and reconnection,
   network connections of the same kind the W5500 provides (so the FTP
   server works over either board), and updates from K-UI. Then K-UI's
   driver for it and a Wi-Fi setup screen (network list, password with the
   on-screen keyboard).

Everything up to the console itself can be host-tested first, as the W5500
and FTP code was.

## Sources

- [W5500 SCI wiring, VA1 solder points](https://github.com/williamdsw/dreamcast-w5500-serial-ethernet-adapter-schemes)
- [SCI-SPI mod notes, DC-SWAT](http://www.dc-swat.ru/blog/hardware/1143.html) (spare RA101 GPIOs and SD power advice; read through a search summary, as the page itself is blocked from the build environment)
- [iceGDROM riser board](https://github.com/zeldin/iceGDROM) (drive connector pinout, including its 5 V pins)
- [XIAO ESP32-C5 pin map](https://github.com/espressif/arduino-esp32/blob/master/variants/XIAO_ESP32C5/pins_arduino.h)
- [XIAO ESP32-C5 board description](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/seeed/xiao_esp32c5/doc/index.rst)
- [ESP32-C5 strapping pins](https://www.espboards.dev/blog/esp32-strapping-pins/)
