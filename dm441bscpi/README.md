# EZ DM-441B wireless display interface

Pico W firmware. Core0 runs Wi-Fi and a small SCPI command server on TCP 5025.
Core1 owns both SPI peripherals. DIGIT_5 is connected to GPIO12. Its rising
edge synchronizes a Core1 hardware timer that samples SPI0 at 1, 3, 5, 7 and
9 ms after the edge, at the middle of each 2 ms digit interval. Each SPI word
identifies its active digit through the captured DIGIT bits. A separate 10 ms
LED sampling deadline reads SPI1. GPIO11 is configured as an unused input.

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
- `RAW?`: unsigned decimal 32-bit raw snapshot, SPI0 in bits 31–16 and SPI1 in bits 15–0
- `*OPC?`: `1` (commands are synchronous; this does not trigger a fresh scan)

Reads return the last complete display scan and latest independent LED sample.
The initial display is zero until a complete valid scan arrives. If DIGIT_5 synchronization edges stop,
the last valid display remains available. The protocol does not currently report
sample age or validity. Invalid BCD patterns and contradictory multi-digit or
multi-point selections do not replace the saved display.

## 64-bit status ABI

| Bits | Meaning |
| --- | --- |
| 0–15 | LED states in the exact SPI1 positions from connctions.txt; normalized so 1 means lit; bit 7 is zero |
| 16–58 | Unsigned decimal magnitude (DIGIT_5 is most significant, DIGIT_1 least significant) |
| 59 | Overrange |
| 60 | Underrange |
| 61–62 | Decimal-point position from the left: 0 = four fractional digits, 1 = three, 2 = two, 3 = one |
| 63 | Negative sign, copied from normalized SPI1 POL bit 6 |

The two-bit decimal field cannot distinguish all decimal points off from
DP_4 on; both encode zero. Multiple simultaneous decimal points invalidate a
scan. DIGIT_5..1 are assembled from most to least significant, regardless of arrival order;
a repeated position or gap over 100 ms restarts assembly. A complete scan means
one occurrence of each position, not a hardware-synchronized ADC conversion.
UNDER/OVER and decimal signals are accumulated across that scan. Non-BCD
blank/error glyphs retain the previous valid reading; verify actual meter
behavior during range transitions and overload before relying on those states.

SPI1 LED bit positions in the corrected physical MSB-first layout:
15 Ohm, 14 MOhm, 13 V, 12 A, 11 kOhm, 10 mV, 9 mA, 8 R,
7 unused, 6 negative, 5 HOLD, 4 diode, 3 DC, 2 AC, 1 Freq/kHz, 0 hFE.
Bits 15–9 and 6 are inverted from the physical inputs. Bit 7 is masked out.

IC1 QH connects to SPI0 MISO and IC3 QH connects to SPI1 MISO.
Each chain sends the nearest IC's H through A first, then the upstream IC's
H through A. With MSB-first SPI, those samples occupy bits 15 through 0.
The corrected SPI0 layout is DIGIT_1 at bit 0, DIGIT_2 at bit 15, DIGIT_3 at
bit 14, DIGIT_4 at bit 13, DIGIT_5 at bit 8, BCD_1 through BCD_4 at bits
9 through 12, UNDER at bit 6, OVER at bit 5, and DP_1 through DP_4 at bits
4 through 1. Decimal-point inputs are active low, as confirmed by live SPI0
captures. UNDER and OVER remain active high. See connctions.txt for the full
physical pin map.

Update the firmware and monitor together. The corrected low 16 status bits
use the physical positions above. The monitor's binary line shows normalized
LED states (1 = on) from bit 15 on the left through bit 0 on the right.

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

SPI clock is 1 MHz. GPIO12 must receive active-high DIGIT_5 with a 2 ms
high interval once per 10 ms scan. The rising-edge handler clears incomplete
assembly and schedules the first SPI0 load for 1 ms later. Four more reads
follow at absolute 2 ms intervals, keeping transaction duration out of the
sampling period. The timer then stops until the next DIGIT_5 edge. Every edge
reanchors sampling to the display, preventing cumulative phase drift.

The timer IRQ runs on Core1. It loads the parallel inputs near the midpoint
and shifts the captured word. If a deadline cannot be armed or the callback
arrives at least 1 ms late, the scan is discarded until the next sync edge.
Confirm midpoint timing and the 2 ms/10 ms periods with a logic analyzer.
The SPI1 read can be interrupted by the display timer. A mutex protects the
64-bit published snapshot, and LED publication is serialized with timer
publication on Core1.

No hardware or live Wi-Fi/TCP validation has been performed. Verify DIGIT_1
least-significant position, BCD_1 least-significant weighting, polarity, overload glyphs and
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
DP_4 and no decimal point share encoding zero in the current firmware ABI.

## Wi-Fi connection watchdog

Core0 starts an asynchronous join at boot and polls the IP link status in its
main loop. Any status other than `CYW43_LINK_UP` (including no IP address or
join/authentication failure) counts as disconnected. After losing a working
connection it waits 30 seconds, then resets station mode and starts another
join. If still disconnected, it repeats every 30 seconds indefinitely. Boot
connection failures use the same retry interval. Regaining connectivity cancels
the pending retry; a later disconnect starts a fresh 30-second countdown.

The SCPI listener remains bound across reconnects. On disconnect the previous
client is aborted so it cannot prevent a new connection; restart the Python
monitor after connectivity returns. Core1 acquisition continues independently.
Wi-Fi events and join errors are logged to USB stdio. This watches the local
Wi-Fi/IP link, not Internet reachability.

The watchdog was tested with simulated time/link states (initial failure,
repeated retries, connection loss/recovery, pending DHCP, and immediate join
errors); actual access-point outage/recovery testing still requires hardware.

```
cc -std=c11 -Wall -Wextra -Werror -DWIFI_SSID='"test"' -DWIFI_PASSWORD='"test"' -I tests/wifi_watchdog tests/wifi_watchdog/test.c -o /tmp/dm441b-wifi-test
/tmp/dm441b-wifi-test
```

## Timer acquisition regression test

The mocked acquisition test checks rising-edge synchronization on GPIO12,
five reads at 1/3/5/7/9 ms, digit identification from captured bits, phase
resynchronization, incomplete-frame handling, and missed-deadline rejection.

```
cc -std=c11 -Wall -Wextra -Werror -I tests/acquisition_timer tests/acquisition_timer/test.c display_decoder.c -o /tmp/dm441b-timer-test
/tmp/dm441b-timer-test
```

## Raw SPI diagnostics and sampling LED output

`RAW?` returns `(spi0_word << 16) | spi1_word` as unsigned decimal text.
Both words contain physical input levels in MSB-first order, including unused
inputs. They are saved immediately after each read, before LED polarity
normalization or display decoding. SPI0 is the most recent individual digit
sample, including invalid/undecodable samples. SPI1 is the most recent LED
sample. The two halves are sampled independently and copied atomically for
the query. Before the first sample, the corresponding half is zero.

The updated Python monitor queries both `STAT?` and `RAW?` on each refresh.
It displays separate SPI0 and SPI1 raw binary lines, bit 15 on the left,
plus the combined value in decimal and hexadecimal. The normalized LED
binary line continues to use 1 for LED on. The status and raw queries may
observe different sampling instants. Install the updated firmware to use
the monitor's new raw query.

GPIO13 is the sampling LED/logic-analyzer output. It starts low and toggles
at the start of every actual SPI0 load/read, immediately before asserting the
74HC166 load control. Skipped samples and SPI1 reads do not toggle it. Connect
an external LED with a series resistor or a logic-analyzer probe to GPIO13.
The pin is defined as `DM_SAMPLE_LED_GPIO` in dm441bscpi.h. The Pico W onboard
LED is connected to the wireless chip, so this timing output uses GPIO13.

Firmware compilation and mocked acquisition/monitor tests pass, including
raw packing, preservation of undecodable samples, GPIO13 toggles, binary
formatting and fragmented raw TCP replies. Hardware timing remains to be
verified on the meter.

Live diagnosis at 192.168.0.190 found that valid scans were rejected because
DP_1..DP_4 had physical levels 1101, which the old decoder interpreted as
three active decimal points. The decimal pins are now inverted before scan
assembly. DIGIT_1 is treated as the rightmost digit, and DP_n indicates n
fractional digits. Captured words 019A, 0A9B, 829A, 409A, 209A therefore decode
to 0.015. A regression test uses those exact raw words.
