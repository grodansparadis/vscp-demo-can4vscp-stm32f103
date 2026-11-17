# Registers, events and the decision matrix

The node follows the blinky module description file (MDF),
<https://eurosource.se/blinky0.json>. Tools such as VSCP Works read the MDF
from the URL the node reports in its standard registers and then show the
registers below with names and descriptions.

## Application registers (page 0)

| Register | Name | Access | Default | Stored | Description |
|----------|------|--------|---------|--------|-------------|
| 0 | Zone | rw | 0 | yes | Zone of the node |
| 1 | Subzone | rw | 0 | yes | Subzone of the node |
| 2 | Status | rw | | no | Bit 1: LED is on. Bit 0: button is pressed. Writing bit 1 turns the LED on or off |
| 3 | Control | rw | 0xCE | yes | See below |
| 4 | Blink interval MSB | rw | 0x01 | yes | Blink interval in milliseconds, high byte. Default 500 ms. Interval zero stops the blinking |
| 5 | Blink interval LSB | rw | 0xF4 | yes | Blink interval in milliseconds, low byte |
| 6 | Millisecond counter, byte 3 (MSB) | rw | | no | 32-bit counter that counts milliseconds when enabled in the control register |
| 7 | Millisecond counter, byte 2 | rw | | no | |
| 8 | Millisecond counter, byte 1 | rw | | no | |
| 9 | Millisecond counter, byte 0 (LSB) | rw | | no | Writing any value to any of registers 6 - 9 clears the counter |
| 10 | Button data byte 0 | rw | 1 | yes | Data byte 0 of the TURN-ON button event only; START/STOP reserve byte 0 and send zero |
| 11 | Button zone | rw | 1 | yes | Zone (data byte 1) of the button events |
| 12 | Button subzone | rw | 1 | yes | Subzone (data byte 2) of the button events |
| 0x60 - 0x7F | Decision matrix | rw | 0 | yes | Four rows of eight bytes |

"Stored" registers are saved in flash about a second after they were last
changed, and are restored when the node starts.

### Control register (3)

| Bit | Default | Meaning when set |
|-----|---------|------------------|
| 7 | 1 | Blink the LED |
| 6 | 1 | The button sends events |
| 3 | 1 | Send TURN-ON when the button is pressed |
| 2 | 1 | Send START when the button is pressed |
| 1 | 1 | Send STOP when the button is released |
| 0 | 0 | Run the millisecond counter |

### Restore the defaults

Write 0x55 and then, within one second, 0xAA to standard register 0xA2. The
application registers and the decision matrix return to their defaults. The
nickname is kept.

## Standard registers

The standard registers (0x80 - 0xFF) are handled by `vscp-firmware.c`. The
ones that say something about this node:

| Register | Content |
|----------|---------|
| 0x84 - 0x88 | User id, five bytes you can use as you like (stored) |
| 0x91 | Nickname |
| 0x94 - 0x96 | Firmware version (0.1.0) |
| 0x97 | Bootloader algorithm: 0xFF, the node has no bootloader |
| 0x98 | Buffer size: 8 |
| 0x99 | Register pages used: 1 |
| 0xD0 - 0xDF | GUID, most significant byte first |
| 0xE0 - 0xFF | MDF URL: `eurosource.se/blinky0.json` |

### GUID

The GUID is built from the 96-bit unique id of the MCU, in the range VSCP has
reserved for that:

```text
FD:00:02:uu:uu:uu:uu:uu:uu:uu:uu:uu:uu:uu:uu:00
```

`FD:00:02` says "ST microcontroller with unique id", the twelve `uu` bytes
are the unique id. No two boards get the same GUID and nothing has to be
programmed per board.

## Events the node sends

| Event | Class | Type | Data | When |
|-------|-------|------|------|------|
| New node online | 0 CLASS1.PROTOCOL | 2 | nickname | When the node becomes active, and as a probe during nickname discovery |
| Heartbeat | 20 CLASS1.INFORMATION | 9 | 0, zone, subzone | Every minute |
| ON | 20 CLASS1.INFORMATION | 3 | 0, zone, subzone | The LED was turned on by a register write or a decision matrix action |
| OFF | 20 CLASS1.INFORMATION | 4 | 0, zone, subzone | The LED was turned off in the same way |
| TURN-ON | 30 CLASS1.CONTROL | 5 | byte 0, button zone, button subzone | Button pressed (control bit 3) |
| START | 20 CLASS1.INFORMATION | 25 | 0, button zone, button subzone | Button pressed (control bit 2) |
| STOP | 20 CLASS1.INFORMATION | 24 | 0, button zone, button subzone | Button released (control bit 1) |

The periodic blink does not send ON and OFF events. With the default
interval that would be two events every second from every blinky node.

START and STOP are button-state notifications, not CLASS1.CONTROL commands. For both events, data byte 0 is reserved and must be zero; register 10 applies only to the TURN-ON event.\n\nThe node also answers the CLASS1.PROTOCOL events addressed to it: register
read and write, page read and write, who is there, decision matrix info,
set and drop nickname, and so on. That is all done by `vscp-firmware.c`.

## Decision matrix

The decision matrix lets events on the bus trigger actions in the node,
without any programming. It has four rows in registers 0x60 - 0x7F. A row is
eight bytes:

| Byte | Name | Meaning |
|------|------|---------|
| 0 | oaddr | Nickname of the sender, used if flag bit 6 is set |
| 1 | flags | See below |
| 2 | class mask | Bits of the class that must match the class filter |
| 3 | class filter | Class to match (low eight bits) |
| 4 | type mask | Bits of the type that must match the type filter |
| 5 | type filter | Type to match |
| 6 | action | What to do |
| 7 | action parameter | Argument for the action |

Flags:

| Bit | Meaning when set |
|-----|------------------|
| 7 | The row is enabled |
| 6 | The sender must be `oaddr` |
| 4 | The zone of the event (data byte 1) must be the zone of the node, or 255 |
| 3 | The subzone of the event (data byte 2) must be the subzone of the node, or 255 |
| 1 | Bit 8 of the class mask |
| 0 | Bit 8 of the class filter |

A mask bit of zero means "do not care". Mask 0xFF and filter 30 matches class
30 only; mask 0 matches every class.

Actions:

| Code | Action | Parameter |
|------|--------|-----------|
| 0 | No operation | |
| 1 | Set the LED | 0 = off, 1 = on, 2 = toggle |
| 2 | Turn the LED on | |
| 3 | Turn the LED off | |
| 4 | Toggle the LED | |
| 5 | Start blinking (sets control bit 7) | |
| 6 | Stop blinking (clears control bit 7) | |
| 7 | Clear the millisecond counter | |

### Example

Let TURN-ON (class 30, type 5) from any node turn the LED on, and TURN-OFF
(class 30, type 6) turn it off:

| Register | Row 0 | Row 1 (register + 8) |
|----------|-------|-------|
| 0x60 oaddr | 0 | 0 |
| 0x61 flags | 0x80 | 0x80 |
| 0x62 class mask | 0xFF | 0xFF |
| 0x63 class filter | 30 | 30 |
| 0x64 type mask | 0xFF | 0xFF |
| 0x65 type filter | 5 | 6 |
| 0x66 action | 2 | 3 |
| 0x67 parameter | 0 | 0 |

Stop the blinking first (clear bit 7 of the control register), or the LED
will be toggled again by the blink function. With two blinky nodes on the
bus, the button of one now controls the LED of the other.
