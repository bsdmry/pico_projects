# EZ DM-441B wireless display interface

Pico W firmware. Core0 runs Wi-Fi and a small SCPI command server on TCP 5025.
Core1 owns both SPI peripherals, the GPIO11 falling-edge interrupt, and a
10 ms LED sampling deadline driven by the Pico monotonic timer. GPIO12 is
reserved as an input. No SPI transactions run on Core0.

## Build

```
cmake -S . -B build -DPICO_BOARD=pico_w -DWIFI_SSID='your-network' -DWIFI_PASSWORD='your-password'
cmake --build build -j4
```

The existing SDK and compiler defaults are retained. The local verification
build uses placeholder credentials `build-test`; configure real credentials
before flashing. Credentials are compiled into the image and CMake cache.
USB stdio is enabled; UART stdio is disabled.

## Protocol

One TCP client at a time. Commands are case insensitive; an optional leading
colon is accepted. CR, LF and semicolon terminate commands. Responses end in LF.
Semicolon-separated commands must each use their full header (no relative
SCPI paths). This is a small SCPI-compatible command subset, not a full SCPI parser.

- `*IDN?`: `EZ DM-441B`
- `STAT?`, `STATUS?`, `READ?`, `FETC?`, `FETCH?`: unsigned decimal 64-bit saved status
- `*OPC?`: `1` (commands are synchronous; this does not trigger a fresh scan)

Reads return the last complete display scan and latest independent LED sample.
The initial display is zero until a complete valid scan arrives. If strobes stop,
the last valid display remains available. The protocol does not currently report
sample age or validity. Invalid BCD patterns and contradictory multi-digit or
multi-point selections do not replace the saved display.

## 64-bit status ABI

| Bits | Meaning |
| --- | --- |
| 0–15 | LED states in the exact SPI1 positions from connctions.txt; normalized so 1 means lit; bit 8 is zero |
| 16–58 | Unsigned decimal magnitude (DIGIT_1 is assumed most significant) |
| 59 | Overrange |
| 60 | Underrange |
| 61–62 | Decimal-point position: 0 after DIGIT_1, 1 after DIGIT_2, 2 after DIGIT_3, 3 after DIGIT_4 |
| 63 | Negative sign, copied from normalized SPI1 POL bit 9 |

The two-bit decimal field cannot distinguish all decimal points off from
DP_1 on; both encode zero. Multiple simultaneous decimal points invalidate a
scan. DIGIT_1..5 are assembled in positional order regardless of arrival order;
a repeated position or gap over 100 ms restarts assembly. A complete scan means
one occurrence of each position, not a hardware-synchronized ADC conversion.
UNDER/OVER and decimal signals are accumulated across that scan. Non-BCD
blank/error glyphs retain the previous valid reading; verify actual meter
behavior during range transitions and overload before relying on those states.

SPI1 LED bit positions:
0 Ohm, 1 MOhm, 2 V, 3 A, 4 kOhm, 5 mV, 6 mA, 7 R,
8 unused, 9 negative, 10 HOLD, 11 diode, 12 DC, 13 AC, 14 frequency, 15 hFE.
Bits 0–6 and 9 are inverted from the physical inputs.

Example host decoding:

```python
import socket
with socket.create_connection(('meter-ip', 5025), timeout=3) as sock:
    stream = sock.makefile('rwb', buffering=0)
    stream.write(b'*IDN?\n')
    print(stream.readline().decode().strip())
    stream.write(b'STAT?\n')
    status = int(stream.readline())
    magnitude = (status >> 16) & ((1 << 43) - 1)
    dp = (status >> 61) & 3
    value = magnitude / (10 ** (4 - dp))
    if status & (1 << 63):
        value = -value
    print(value, hex(status & 0xffff))
```

## Electrical and timing assumptions to verify on hardware

Use the pins and chain bit ordering in connctions.txt. GPIO5/GPIO9 are software
load controls connected to both registers' active-low shift/load inputs, not
ordinary SPI chip selects. Clock inhibit must be low and clear high externally.
The 74HC166 loads synchronously on a rising clock with load low, so the firmware
generates a separate load clock before each 16-bit SPI mode-0, MSB-first read.
See the [TI SN74HC166 datasheet](https://www.ti.com/lit/ds/symlink/sn74hc166.pdf).

SPI clock is 1 MHz. The GPIO handler loads the parallel inputs a few microseconds
after the strobe edge, then shifts the latched word. Confirm that BCD and digit
signals remain stable until that load edge, that the word is not shifted by one
bit, and that inter-strobe spacing exceeds the handler duration. Adjust load
delay/clock speed or use hardware-assisted capture if measured timing requires it.
The SPI1 read may be interrupted by SPI0 capture. A mutex protects the 64-bit
published snapshot from torn reads across cores; GPIO publication is serialized
with LED publication on Core1.

No hardware or live Wi-Fi/TCP validation has been performed. Verify DIGIT_1
significance, BCD_1 least-significant weighting, polarity, overload glyphs and
all decimal positions against the actual display.

## Host decoder tests

```
cc -std=c11 -Wall -Wextra -Werror -I . tests/test_decoder.c display_decoder.c -o /tmp/dm441b-test
/tmp/dm441b-test
```

Tests cover five-digit magnitudes, all decimal positions, range flags, LED
polarity, incomplete/repeated/timed-out scans and invalid selection/BCD patterns.

## Python terminal monitor

`dm441b_monitor.py` uses only the Python 3 standard library. It displays the
reading with the display's decimal position, range flags, every LED as ON/off,
and the raw unsigned 64-bit status. On a terminal it refreshes in place; when
redirected it emits timestamped snapshots. Ctrl+C exits. Connection/protocol
errors exit with a diagnostic; rerun to reconnect.

```
python3 dm441b_monitor.py 192.168.1.100
python3 dm441b_monitor.py 192.168.1.100 --interval 1 --once
```

Replace the example IP with your meter's address. The default port is 5025 and
socket timeout is 3 seconds; override with `--port` and `--timeout`. The monitor
shows the firmware's last saved samples, which may be stale or uninitialized.
DP_1 and no decimal point share encoding zero in the current firmware ABI.
