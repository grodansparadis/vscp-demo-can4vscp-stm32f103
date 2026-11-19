# Getting started

You need a Blue Pill, a 3.3 V CAN transceiver, a push button, an ST-Link
and a second CAN node or USB CAN adapter. See [hardware and wiring](hardware.md)
for connections and bus termination.

## Build

Install the tools on Debian/Ubuntu:

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi cmake make
```

Clone the repository with its VSCP firmware submodule and build:

```bash
git clone --recurse-submodules https://github.com/grodansparadis/vscp-demo-can4vscp-stm32f103-blinky.git
cd vscp-demo-can4vscp-stm32f103-blinky/firmware
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The build produces `build/firmware.elf`, `build/firmware.hex` and
`build/firmware.bin`. Use `-DCMAKE_BUILD_TYPE=Debug` for debugging.

## Flash

Connect the ST-Link to SWDIO, SWCLK, GND and 3.3 V:

```bash
# With stlink-tools
st-flash --reset write build/firmware.bin 0x08000000

# Or with OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "program build/firmware.elf verify reset exit"
```

## First start

1. Connect the node to a 125 kbit/s CAN bus with a 120-ohm terminator at
   each end and at least one other node to acknowledge frames.
2. Power the board. The status LED blinks during nickname discovery, which
   takes about six seconds without a segment controller.
3. The status LED goes steady and the blinky LED starts blinking when the
   node becomes active.

Hold the button for two seconds to forget the nickname and start discovery
again. Continue with [testing the node from a PC](testing.md).

[Document version](./bottom-copyright.md ':include')
