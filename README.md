# vscp-demo-can4vscp-stm32f103

[![Build](https://github.com/grodansparadis/vscp-demo-can4vscp-stm32f103/actions/workflows/build.yml/badge.svg)](https://github.com/grodansparadis/vscp-demo-can4vscp-stm32f103/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Last commit](https://img.shields.io/github/last-commit/grodansparadis/vscp-demo-can4vscp-stm32f103)

The [VSCP](https://www.vscp.org) blinky demo for the STM32F103 "Blue Pill" as a
**VSCP Level I node on CAN** (CAN4VSCP) at **125 kbit/s**.

Blinky is the smallest useful VSCP node: an LED that blinks, a button that
sends events, a few registers and a small decision matrix. The same demo
exists for other boards and transports, for example
[STM32F401 + WIZnet IP20 over TCP/IP](https://github.com/grodansparadis/vscp-demo-link-stm32f401-wiz-ip20-blinky).
Use this project to learn how a Level I node works, or as the starting point
for your own CAN4VSCP node.

| | |
|---|---|
| MCU | STM32F103C8T6 (Blue Pill), 64 KB flash, 20 KB RAM |
| Transport | CAN, 125 kbit/s, extended frames (CAN4VSCP) |
| VSCP stack | [vscp-firmware](https://github.com/grodansparadis/vscp-firmware) Level I (`vscp-firmware.c`), as a git submodule |
| Tools | STM32CubeMX project, CMake, GNU Arm Embedded Toolchain |
| Size | About 19 KB flash, 2.5 KB RAM (release build) |

More documentation:

- [Documentation site source](docs/) (docsify)
- [Hardware and wiring](docs/hardware.md)
- [How the firmware works](docs/firmware.md)
- [Registers, events and the decision matrix](docs/registers.md)
- [Testing the node from a PC](docs/testing.md)

To preview the docsify site locally, serve the repository's `docs` directory
over HTTP, for example with `python3 -m http.server 8000 --directory docs`,
and open <http://localhost:8000/>. The site loads docsify and its theme from
CDNs and needs an internet connection.

## What the node does

- Blinks the on-board LED. The interval is a register (default 500 ms).
- Sends events when the button is pressed and released.
- Runs a millisecond counter that can be read from registers.
- Has a decision matrix with four rows, so events from other nodes can turn
  the LED on and off, start and stop the blinking and clear the counter.
- Finds its own nickname on the bus when it is started for the first time,
  and keeps it, together with the settings, in flash.
- Sends a heartbeat event every minute.

## Hardware in short

You need a Blue Pill, a 3.3 V CAN transceiver (for example SN65HVD230), a
push button and an ST-Link. The details are in [docs/hardware.md](docs/hardware.md).

| Pin | Function |
|-----|----------|
| PB8 | CAN RX (to the transceiver's RXD) |
| PB9 | CAN TX (to the transceiver's TXD) |
| PC13 | Blinky LED (on the board, on when low) |
| PA0 | Button to ground (internal pull-up) |
| PB12 | Status LED, optional (to ground through a resistor) |
| PA9 / PA10 | USART1 TX/RX, debug output at 115200 baud |
| PA13 / PA14 | SWDIO / SWCLK for the ST-Link |

## Build

Install the tools (Debian/Ubuntu):

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi cmake make
```

Get the source. The VSCP firmware is a submodule, so clone with
`--recurse-submodules`:

```bash
git clone --recurse-submodules https://github.com/grodansparadis/vscp-demo-can4vscp-stm32f103.git
cd vscp-demo-can4vscp-stm32f103/firmware
```

Build:

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This gives `build/firmware.elf`, `build/firmware.hex` and `build/firmware.bin`.
Use `-DCMAKE_BUILD_TYPE=Debug` for a build to step through in a debugger.

Ready-built firmware is attached to each run of the
[Build workflow](https://github.com/grodansparadis/vscp-demo-can4vscp-stm32f103/actions/workflows/build.yml).

## Flash

With an ST-Link connected to SWDIO, SWCLK, GND and 3.3 V:

```bash
# stlink-tools (sudo apt install stlink-tools)
st-flash --reset write build/firmware.bin 0x08000000

# or OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "program build/firmware.elf verify reset exit"
```

## First start

1. Connect the node to a CAN bus that runs at 125 kbit/s, has a 120 Ω
   terminator at each end and has **at least one other node** (a USB CAN
   adapter is fine). A lone CAN node gets no acknowledge and can not send.
2. Power the board. The status LED blinks while the node searches for a free
   nickname. This takes about six seconds when no segment controller is
   present.
3. The status LED goes steady and the blinky LED starts to blink. The node
   is active and has announced itself with a "new node online" event.

To give the node a new nickname, hold the button for two seconds. It forgets
its nickname and searches again.

With a USB to serial adapter on PA9 you see this at 115200 baud:

```text
VSCP blinky, CAN4VSCP, STM32F103 - version 0.1.0
GUID: FD:00:02:05:D9:FF:33:38:34:59:4E:43:17:51:16:00
Nickname: 0xFF
Active, nickname: 0x01
```

Continue with [Testing the node from a PC](docs/testing.md).

## Project layout

```text
firmware/
  firmware.ioc            STM32CubeMX project (clock, pins, CAN bit timing)
  CMakeLists.txt          Build file; the user sources are listed here
  Core/                   Code generated by STM32CubeMX (main.c, can.c, ...)
  Drivers/                STM32 HAL and CMSIS, copied by STM32CubeMX
  blinky.c / blinky.h     The node: LED, button, registers, decision matrix
                          and the callbacks of the VSCP firmware
  vscp-can.c / .h         VSCP events to and from CAN frames
  persist.c / .h          Settings in the last flash page
  vscp-projdefs.h         Project configuration (version, debug output)
  third-party/vscp-firmware   The VSCP firmware (submodule)
tests/                    Tests of the node logic that run on a PC
docs/                     Documentation
```

## Tests

The node logic is tested on a PC, with a stand-in for the STM32 HAL. The test
plays the CAN bus, the button and the clock, and checks nickname discovery,
register access, the button events, the decision matrix and that settings
survive a restart.

```bash
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## License

MIT, see [LICENSE](LICENSE). The STM32 HAL and CMSIS in `firmware/Drivers`
have their own licenses, see the files there.
