# Ethernet connection test

Network retains its read-only adapter inspection and adds a temporary DHCP and
local-gateway test. It runs on the existing I/O worker and does not overlap a rip,
VMU write, storage test, or music-file load. It uses upstream KOS BBA/LAN drivers
and independently authored packet validation.

The connection test performs DHCP DISCOVER/OFFER/REQUEST/ACK, validates its
transaction ID and MAC address, checks the assigned address for an ARP conflict,
then requests the local gateway's MAC and sends an ICMP echo. It reports DHCP,
ARP and ICMP results separately. A router that filters ping may still be usable;
no response is reported as an unproven/failed ping, not as proof the adapter is
broken. A valid lease without an advertised gateway is displayed as such.
DNS server addresses are displayed, but DNS lookup and Internet access are not
tested. Dial-up modem support is not implemented.

This is a **temporary diagnostic session**, not a persistent networking service.
It never rewrites flash or saved addresses. It refuses to take over any running
interface, restores the previous packet-input callback, stops only the driver it
started, and unregisters only a device it registered. The acquired lease is not
retained by K-UI after the test; its server lease expires normally. A validated
static-address entry point exists for integration, but no inactive static editor
is exposed in the menu.

DHCP offer and ACK waits are individually limited to 15 seconds, ARP conflict
checking to 3 seconds, and gateway ARP/ICMP to 5 seconds each. B cancels between
protocol iterations. Pinned KOS's BBA startup has its own bounded 10-second link
wait, and LAN startup waits 4 seconds; cancellation takes effect after that
upstream call returns. The UI remains responsive throughout.

The engine rejects truncated/fragmented packets, wrong transactions, mismatched
hardware addresses, invalid IPv4/UDP/ICMP checksums, malformed DHCP options,
invalid masks, off-subnet gateways and address conflicts. DHCP option overload is
not implemented; such replies are ignored rather than interpreted incompletely.
The receive callback copies a bounded frame into a four-entry queue and leaves
all parsing to the worker. Queue drops are reported.

## Host and console checks

Host protocol tests cover DHCP success/NAK/deadlines, conflict detection, gateway
ARP/echo matching, every packet truncation boundary and malformed option lengths.
[Host evidence](evidence/m15-network-maintenance-host-2026-09-23.json) records these checks.
Lifecycle stubs cover preexisting interface refusal, callback/config preservation,
owned versus borrowed driver cleanup, cancellation and init/start/transmit errors.
TX storage is explicitly aligned to 32 bytes for the BBA driver. Neither driver
is polled from the test: BBA has its own RX worker and LAN an IRQ receiver.
They do not prove driver behavior on real hardware.

On a Dreamcast with BBA or LAN adapter, first run inspection, then the connection
test with a cable connected to a DHCP router. Save the report. Repeat with the
cable disconnected, and cancel one attempt. No full disc dump or new CD is
needed. If no Ethernet adapter is installed, the expected result is an explicit
no-adapter message. Hardware acceptance remains pending.

Primary implementation references:

- [Pinned KOS network interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/net.h)
- [Pinned KOS input callback](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/net/net_input.c)
- [Pinned BBA driver](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/network/broadband_adapter.c)
- [Pinned LAN driver](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/network/lan_adapter.c)
- [DHCP RFC 2131](https://www.rfc-editor.org/rfc/rfc2131), [options RFC 2132](https://www.rfc-editor.org/rfc/rfc2132), [ARP RFC 826](https://www.rfc-editor.org/rfc/rfc826), [ICMP RFC 792](https://www.rfc-editor.org/rfc/rfc792)
