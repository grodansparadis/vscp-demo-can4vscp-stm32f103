# Hardware and wiring

## Parts

| Part | Note |
|------|------|
| STM32F103C8T6 "Blue Pill" board | 8 MHz crystal, LED on PC13 |
| CAN transceiver for 3.3 V | SN65HVD230, MCP2562 (with VIO at 3.3 V) or similar |
| Push button | Between PA0 and ground |
| LED and 330 Ω - 1 kΩ resistor | Optional status LED on PB12 |
| ST-Link V2 | For flashing and debugging |
| USB to serial adapter, 3.3 V | Optional, for the debug output |
| USB CAN adapter | For the PC side, any adapter that Linux SocketCAN supports |

## Connections

| Blue Pill | Connect to |
|-----------|-----------|
| PB8 (CAN RX) | Transceiver RXD (called R or CRX on some boards) |
| PB9 (CAN TX) | Transceiver TXD (called D or CTX on some boards) |
| 3.3 V | Transceiver VCC (3.3 V types) |
| GND | Transceiver GND, and the ground of the CAN bus |
| PA0 | Button, other side of the button to GND |
| PB12 | Status LED anode; cathode through the resistor to GND |
| PA9 (USART1 TX) | RX of the USB to serial adapter |
| PA13, PA14, GND, 3.3 V | SWDIO, SWCLK, GND, 3.3 V of the ST-Link |

The transceiver connects to the bus with CANH and CANL.

Notes:

- **CAN pins.** CAN is remapped from its default pins PA11/PA12 to PB8/PB9.
  PA11/PA12 are the USB pins of the Blue Pill, and the board has a pull-up
  resistor on PA12 for USB that is better kept away from the transceiver.
- **5 V transceivers.** PB8 and PB9 tolerate 5 V, so a 5 V transceiver such
  as TJA1050 or MCP2551 works if it accepts 3.3 V as a high level on TXD
  (most do). Power it from the 5 V pin.
- **The LED on PC13** is lit when the pin is low. `blinky.c` takes care of
  that (`LED_PIN_ON`).
- **Power.** Do not power the board from the ST-Link and from USB at the same
  time unless you know your board handles it.

## The CAN bus

- The bit rate is 125 kbit/s, the default for CAN4VSCP.
- Put a 120 Ω resistor between CANH and CANL at **both ends** of the bus.
  Many transceiver boards have one on board; then you need one more at the
  other end. Measured between CANH and CANL with the power off, a correctly
  terminated bus shows about 60 Ω.
- Connect the grounds of all nodes.
- At least two nodes must be on the bus. A CAN node that gets no acknowledge
  from another node repeats its frame forever.

## Bit timing

The CAN controller is clocked from APB1 at 36 MHz (8 MHz crystal, PLL × 9,
72 MHz system clock).

| Setting | Value |
|---------|-------|
| Prescaler | 18 (time quantum 500 ns) |
| Bit segment 1 | 13 tq |
| Bit segment 2 | 2 tq |
| Synchronisation jump width | 1 tq |
| Bit time | 16 tq = 8 µs, 125 kbit/s |
| Sample point | 87.5 % |

Other **NON STANDARD** bit rates, if your bus does not run at 125 kbit/s:

| Bit rate | Prescaler | Bit segment 1 | Bit segment 2 | Sample point |
|----------|-----------|---------------|---------------|--------------|
| 500 kbit/s | 4 | 15 tq | 2 tq | 88.9 % |
| 250 kbit/s | 9 | 13 tq | 2 tq | 87.5 % |
| 125 kbit/s | 18 | 13 tq | 2 tq | 87.5 % |
| 50 kbit/s | 45 | 13 tq | 2 tq | 87.5 % |

Change the bit rate in `firmware/firmware.ioc` with STM32CubeMX (Connectivity,
CAN, Parameter Settings) and generate the code again, or edit `MX_CAN_Init()`
in `firmware/Core/Src/can.c`.

[Document version](./bottom-copyright.md ':include')
