# Testing the node from a PC

You need a CAN adapter on the PC, connected to the same bus as the node. The
examples use Linux with SocketCAN and assume the adapter shows up as `can0`
and that the node got nickname 1.

## Bring up the CAN interface

```bash
sudo ip link set can0 type can bitrate 125000
sudo ip link set can0 up
```

## Look at the traffic with can-utils

```bash
sudo apt install can-utils
candump can0
```

A VSCP Level I event is one extended CAN frame. The 29-bit id holds the
priority, the VSCP class, the VSCP type and the nickname of the sender:

```text
bit 28-26  priority (0 is the highest)
bit 25     hard coded id (not used here)
bit 24-16  class
bit 15-8   type
bit 7-0    nickname of the sender
```

Reset the node (or power it) and you see it come alive:

```text
can0  000002FF   [1]  00          probe for a segment controller, sent three times
can0  000002FF   [1]  01          probe for nickname 1, sent three times
can0  00000201   [1]  01          new node online: "I am nickname 1"
```

Then press and release the button:

```text
can0  0C1E0501   [3]  01 01 01    TURN-ON (class 30, type 5) from nickname 1
can0  0C141901   [3]  01 01 01    START   (class 20, type 25)
can0  0C141801   [3]  01 01 01    STOP    (class 20, type 24)
```

Once a minute there is a heartbeat:

```text
can0  1C140901   [3]  00 00 00    heartbeat (class 20, type 9)
```

### Read and write registers by hand

Registers are read and written with CLASS1.PROTOCOL events (class 0). The
first data byte is the nickname of the node you talk to.

Read register 3, the control register (type 9, read register):

```bash
cansend can0 0C000900#0103
```

```text
can0  0C000900   [2]  01 03       read register 3 of nickname 1
can0  0C000A01   [2]  03 CE       answer from nickname 1: register 3 holds 0xCE
```

Write 100 to register 5, the low byte of the blink interval (type 11, write
register). The node answers with the new content:

```bash
cansend can0 0C000B00#010564
```

```text
can0  0C000B00   [3]  01 05 64
can0  0C000A01   [2]  05 64
```

The blink interval is now 0x0164 = 356 ms. Write 0 to register 4 as well and
it is 100 ms.

Stop the blinking and turn the LED on through the status register. The node
tells the bus that the LED changed with an ON event:

```bash
cansend can0 0C000B00#01034E     # control = 0x4E: blink off
cansend can0 0C000B00#010202     # status bit 1: LED on
```

```text
can0  0C140301   [3]  00 00 00    ON (class 20, type 3)
can0  0C000A01   [2]  02 02
```

## The VSCP command line tools

[vscp-cli](https://github.com/grodansparadis/vscp-cli) does the same with
less typing and knows the VSCP classes and types.

### Install on Linux

On Debian or Ubuntu, build and install the tools from source:

```bash
sudo apt install build-essential cmake git libmosquitto-dev libexpat1-dev libssl-dev libcurl4-openssl-dev
git clone https://github.com/grodansparadis/vscp-cli.git
cd vscp-cli
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
sudo cmake --install build
vscp-send --version
```

The build needs CMake 3.16 or later, a C++17 compiler and an internet
connection to fetch dependencies.

Alternatively, download the Debian package for your architecture from a
successful run on the [vscp-cli Actions page](https://github.com/grodansparadis/vscp-cli/actions).
GitHub requires you to sign in to download artifacts. Extract the artifact,
then verify and install it, substituting the actual file names:

```bash
sha256sum -c SHA256SUMS-vscp-cli-VERSION-linux-ARCH
sudo apt install ./vscp-cli_VERSION_ARCH.deb
vscp-send --version
```

See the
[vscp-cli installation guide](https://github.com/grodansparadis/vscp-cli/blob/main/docs/installing.md)
for package names and other platforms. SocketCAN is Linux-only.

### Use the tools

```bash
# Show events as VSCP events
vscp-dump -i socketcan --device can0

# Find the nodes on the bus
vscp-scan -i socketcan --device can0

# Read the thirteen application registers of node 1
vscp-reg -i socketcan --device can0 -N 1 read 0 13

# Blink every 100 ms
vscp-reg -i socketcan --device can0 -N 1 write 4 0 100

# Show what the node says about itself and fetch its MDF
vscp-info -i socketcan --device can0 -N 1 --mdf

# Send a TURN-ON event (class 30, type 5; data: index 0, zone 1, subzone 2)
vscp-send -i socketcan --device can0 30 5 0 1 2
```

## VSCP Works

[VSCP Works](https://github.com/grodansparadis/vscp-works-qt) is a graphical
tool for Linux, Windows and macOS. Add a SocketCAN connection for `can0`
(or a CANAL driver connection on Windows), open a session to watch and send
events, or scan for nodes and open the node's configuration. VSCP Works
fetches the MDF from the URL the node reports and shows the registers and
the decision matrix with names and help texts.

## If nothing happens

- **The status LED keeps blinking and `candump` shows nothing.** The node can
  not send. Check that TXD and RXD are not swapped, that the bus is
  terminated and that the PC interface is up at 125 kbit/s. Remember that a
  node alone on the bus can not send.
- **`candump` shows the same probe again and again, without pause.** No other
  node acknowledges the frame. `ip -details -statistics link show can0`
  tells if the PC interface is up and error free.
- **The node answers but with error frames now and then.** Check the
  termination (about 60 Ω between CANH and CANL with the power off) and the
  ground connection.
- **Check the debug output** on PA9 at 115200 baud. It shows the GUID, the
  nickname and when the node becomes active.

[Document version](./bottom-copyright.md ':include')
