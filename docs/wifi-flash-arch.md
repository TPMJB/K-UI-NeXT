# Flash the Seeed XIAO ESP32-C5 on Arch Linux

Use the **ESP32-C5** firmware for the Seeed XIAO ESP32-C5. The C6 image is for a
different chip. These steps flash and test the board over USB before connecting
it to the Dreamcast. The firmware has automated tests; its physical link to the
Dreamcast still needs hardware validation.

## 1. Prepare the board and tools

Keep the board disconnected from the Dreamcast for this first test. If it is
already installed, disconnect its Dreamcast power and signal connections before
USB programming. The XIAO's **5V** pin is its USB VBUS input/output, so do not
connect the Dreamcast's 5V supply and a powered USB cable together.

Attach the external antenna to the board's U.FL connector while the board is
unpowered. Use a USB-C **data** cable. Start with your normal 2.4 GHz network;
for a 5 GHz test, use an antenna specified for both 2.4 and 5 GHz. The board's
radio supports both bands, but the frequency range of the antenna supplied with
your particular kit has not been verified here.

Install the Arch packages:

```sh
sudo pacman -Syu esptool python-pyserial
python -m esptool --help
```

Current Arch packages provide esptool 5.x with ESP32-C5 support. No Arduino IDE,
ESP-IDF build, AUR package, or system-wide `pip install` is needed to flash the
prebuilt image.

## 2. Find the USB port

Plug the board into the computer and list serial devices:

```sh
python -m serial.tools.list_ports -v
```

Look for the Espressif USB JTAG/serial device, usually `/dev/ttyACM0`. If there
are several ports, compare the list with the board unplugged and plugged in.
Set the actual port for the commands below:

```sh
KUI_WIFI_PORT=/dev/ttyACM0
ls -l "$KUI_WIFI_PORT"
```

If opening the port gives **Permission denied**, add your login to Arch's
serial-device group:

```sh
sudo usermod -aG uucp "$USER"
```

Log out and back in, then set `KUI_WIFI_PORT` again in your new terminal.
You can check that `id -nG` includes `uucp`. Do not run the flashing tool with
`sudo` as the normal setup.

## 3. Check and flash the complete image

Extract the **kui-wifi-esp32c5** firmware download, then open a terminal in the
folder containing these three files:

- `kui-wifi-esp32c5.bin`: complete merged image for initial USB flashing.
- `kui-wifi-esp32c5-update.bin`: application image for the later K-UI update
  mechanism; do not use this one for initial flashing.
- `SHA256SUMS`: download checksums.

If using a larger test pack, these files may be in its `firmware/` folder.
Check both binaries and continue only if they show **OK**:

```sh
sha256sum -c SHA256SUMS
```

Close any program that has the serial port open, then flash:

```sh
python -m esptool --chip esp32c5 --port "$KUI_WIFI_PORT" --baud 460800 \
  write-flash 0x0 kui-wifi-esp32c5.bin
```

**Use offset `0x0` for this merged image.** It already contains the bootloader,
partition table, and app at their required offsets. Instructions for flashing a
standalone ESP32-C5 bootloader at `0x2000` are for a different file layout.
The complete image replaces the board's current firmware and may clear saved
Wi-Fi settings. A separate full-chip erase is not required for this first load.
Esptool verifies the written data as part of `write-flash`; check that the
command finishes successfully. Press **RESET** once, with **BOOT** released,
if the app does not start after flashing.

If esptool cannot connect:

1. Close any serial terminal. Hold the board's **BOOT/B** button, press and
   release **RESET**, then release **BOOT/B**. Alternatively, hold **BOOT/B**
   while plugging the board into USB, then release it.
2. Run the port listing again; its number may have changed. Update
   `KUI_WIFI_PORT` if necessary.
3. Retry the command. For an unstable connection, change `--baud 460800` to
   `--baud 115200` and try another data cable or USB port.

If a manually entered download mode keeps being disturbed by automatic reset,
use the same flash command with `--before no-reset` before `write-flash`.
Afterward, release BOOT and press RESET to start the firmware.

## 4. Test Wi-Fi over USB

List the port again if necessary, then open the firmware console:

```sh
python -m serial.tools.miniterm --raw --eol CR "$KUI_WIFI_PORT" 115200
```

The firmware uses the chip's native USB Serial/JTAG console. `115200` is a
conventional terminal setting; no external UART adapter is needed. Press Enter
for the `kui-wifi>` prompt. Type these commands **inside that console**, replacing
the network and password:

```text
version
band 2.4
scan
join "My Wi-Fi" "Replace with your password"
status
```

Look for **Wi-Fi online**, a nonzero **Address**, a channel, and signal strength.
**Dreamcast not connected** is expected during this USB-only test. The network
and chosen band are saved on the board. The clock requires internet access and
may take a little longer to appear; it is not required for the first join test.

In another computer terminal, ping the address printed by `status`. Replace this
example with that address:

```sh
ping -c 4 192.168.1.123
```

Then run `reboot` in the board console. If USB disconnects, reopen miniterm on the
newly listed port. `status` should return to **online** without entering the
password again. This tests joining, addressing, basic reachability, and saved
credentials; it does not yet test SCI transfers or K-UI FTP.

For both bands use `band auto`; for a separate 5 GHz test use `band 5`, then
`scan`, `join`, and `status`. `forget` disconnects and clears saved credentials.
Type `help` to list commands. Exit miniterm with **Ctrl+]** before reflashing.

After the bench test, unplug USB before connecting the Dreamcast wiring and its
5V supply. Use the [SCI connector plan](sci-connector.md) and the
[K-UI Wi-Fi page instructions](wifi.md) for the console-side test.

## References

- [Arch esptool package](https://archlinux.org/packages/extra/any/esptool/)
  and [python-pyserial package](https://archlinux.org/packages/extra/any/python-pyserial/).
- [Espressif: Linux serial access and the Arch uucp group](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html).
- [Espressif: ESP32-C5 flashing commands](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/esptool/basic-commands.html)
  and [flash verification](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/esptool/advanced-commands.html).
- [Espressif: ESP32-C5 native USB console](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/usb-serial-jtag-console.html)
  and [flashing troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/troubleshooting.html).
- [Seeed: XIAO ESP32-C5 hardware overview and pin map](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/).
- [pySerial: serial port listing and miniterm](https://pyserial.readthedocs.io/en/latest/tools.html).
