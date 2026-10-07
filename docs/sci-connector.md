# SCI connector plan: microSD, W5500 and Wi-Fi

**Standalone storage update:** [storage-transports.md](storage-transports.md)
describes the implementation for one SCI microSD board in place of the network
board. SCI storage reserves the whole port: current K-UI refuses network probes
while that card is selected. Keep storage on **SCIF** for the first Wi-Fi test.
The shared-bus wiring/software below remains a separate plan; it is not required
for standalone SCIF/SCI/IDE storage testing.

**The multi-device connector remains a plan.** Network drivers support a W5500
with chip select on GPIO7 (the usual point) or GPIO6, and the Wi-Fi board on
GPIO6 or GPIO7 ([FTP server](ftp.md), [Wi-Fi](wifi.md)). The Wi-Fi hardware path
is untested. Simultaneous SCI storage and networking is not implemented, and the
extra GPIO solder points still need confirmation against the owner's VA1 board.

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
| 5 V | the drive connector's 5 V pins (A3/B3), or the Robot Retro power supply's 5 V (its fan header, on version 1.1 or later, needs no soldering) |

Check every point with a multimeter before soldering. The owner's photos
of both sides of the VA1 board (sent 2026-09-28) show R115, R140 (beside
IC105) and CE113 on the underside. R122 and RA101 are not yet identified
in them.

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
| 9 | Ready (GPIO5) | 10 kΩ pull-up to 3.3 V. The W5500 interrupt is active low; the Wi-Fi READY signal changes level each time it is ready for another transfer |
| 10 | Reset (GPIO0), active low | 10 kΩ pull-up to 3.3 V |

The pull-ups keep every device deselected and running while the console
boots and while a game runs. The microSD card sits on the same SCLK, MOSI
and MISO lines with its own chip select, GPIO7, also pulled up. GPIO7 is
what KallistiOS's SD-over-SCI driver uses, so the SD card gets it and the
network board would move to GPIO6. A W5500 or Wi-Fi board used alone can keep
GPIO7 as its select. This layout does not enable shared-bus support in software.

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

A W5500 module with its own 3.3 V regulator (it has a 5V pin) can take
5 V from pin 1 instead. The owner's W5500 works that way (2026-09-29),
powered from the Robot Retro supply's 5 V.

### Wi-Fi: Seeed XIAO ESP32-C5

Dual-band (2.4 and 5 GHz) Wi-Fi 6 with WPA3, 21 × 17.8 mm, powered from
5 V. It needs K-UI's own firmware, loaded over its USB-C port. Firmware-update
messages exist in the link protocol, but K-UI does not yet expose an update
workflow. Pins D2 and D3 are ESP32-C5 strapping pins (GPIO25 and
GPIO7), so they are left unused.

| XIAO ESP32-C5 | Connector |
| --- | --- |
| D8 (GPIO8) | pin 5, SCLK |
| D10 (GPIO10) | pin 6, MOSI |
| D9 (GPIO9) | pin 7, MISO |
| D0 (GPIO1) | pin 8, network chip select (Dreamcast GPIO6, or GPIO7 when used alone) |
| D1 (GPIO0) | pin 9, ready (optional; exact Dreamcast GPIO5 solder point unconfirmed) |
| D4 (GPIO23) | pin 10, reset request (optional; leave unconnected for the first test) |
| 5V | pin 1 |
| GND | pin 2 |

- **Do not connect the XIAO's 3V3 pin.** It makes its own 3.3 V; tying it to
  the console's 3.3 V would put two regulators against each other.
- **Antenna.** The board uses an external U.FL antenna. Attach it while the
  board is unpowered. For 5 GHz testing, use an antenna specified for both
  2.4 and 5 GHz; the supplied antenna's frequency range has not been verified
  here. Keep the antenna outside the console's metal shielding.
- **Power.** Put a capacitor of about 220 µF next to its 5V pin to cover the
  short current spikes when it transmits. The 5V pin is also the XIAO's USB
  power line. For the first test, power it from USB alone with the Dreamcast
  connections disconnected. Unplug USB before connecting the console's 5 V.
  A single diode in the console's lead should not be treated as complete
  isolation between two connected power supplies.
- The XIAO's reset button is not on its pins. The reset-request line asks
  K-UI's firmware to restart itself; the ESP32-C5's own watchdog covers a
  hang. Current K-UI does not drive this request line, so D4 can remain
  unconnected. With D1 omitted, the driver uses a 20 ms wait for each transfer;
  that fallback is host-tested but its physical timing remains unvalidated.
- **Firmware:** [firmware/kui-wifi](../firmware/kui-wifi/README.md).
  [Flash and bench-test it on Arch](wifi-flash-arch.md) before installation.
- **Sharing MISO:** whether the XIAO releases MISO when deselected still needs
  to be measured on hardware. Do not assume that adding a second chip select
  makes the board safe to share with an SCI SD card.

## Software status

1. The network SCI layer is written (`src/dreamcast/sci_port.c`): chip selects
   on GPIO7 and GPIO6, clock rates, and READY on GPIO5. The storage worker runs
   one network operation at a time. The reset request is not driven yet.
2. The W5500 on GPIO6 is implemented; the driver tries GPIO7 first.
3. Standalone microSD storage on GPIO7 is implemented in K-UI and the detached
   game loader. Sharing SCI with a network adapter remains future work; the
   present reservation guard keeps the storage card's port untouched.
4. The XIAO firmware, K-UI driver, Wi-Fi setup page, and FTP socket backend are
   implemented and host-tested. Firmware-update messages exist; the K-UI update
   workflow and all board-level acceptance tests remain to be done.

Everything up to the console itself can be host-tested first, as the W5500
and FTP code was.

## Sources

- [W5500 SCI wiring, VA1 solder points](https://github.com/williamdsw/dreamcast-w5500-serial-ethernet-adapter-schemes)
- [SCI-SPI mod notes, DC-SWAT](http://www.dc-swat.ru/blog/hardware/1143.html) (spare RA101 GPIOs and SD power advice; read through a search summary, as the page itself is blocked from the build environment)
- [iceGDROM riser board](https://github.com/zeldin/iceGDROM) (drive connector pinout, including its 5 V pins)
- [XIAO ESP32-C5 pin map](https://github.com/espressif/arduino-esp32/blob/master/variants/XIAO_ESP32C5/pins_arduino.h)
- [XIAO ESP32-C5 board description](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/seeed/xiao_esp32c5/doc/index.rst)
- [ESP32-C5 strapping pins](https://www.espboards.dev/blog/esp32-strapping-pins/)
- [XIAO ESP32-C5 getting started, Seeed](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/) (pin map, external antenna, and 5V/VBUS input/output)
