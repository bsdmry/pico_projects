# wirectl

Pico W SCPI-over-TCP controller. The server listens on TCP port 5025 and
accepts newline-, carriage-return-, or semicolon-terminated commands.

Default GPIO assignment:

| Function | GPIO |
|---|---:|
| SPI0 SCK / TX / CS | 18 / 19 / 17 |
| SPI1 SCK / TX / CS | 10 / 11 / 13 |
| UART0 TX / RX | 0 / 1 |
| UART1 TX / RX | 4 / 5 |
| Encoder 1 A / B / button | 2 / 3 / 6 |
| Encoder 2 A / B / button | 7 / 8 / 9 |

Each pin can be overridden independently at compile time, for example
`-DWIRECTL_SPI0_CS=16`. Both UARTs use 115200 baud, 8 data bits, no parity,
and one stop bit.

Encoder 1 changes interface 1's voltage limit in steps of 100. Encoder 2
changes interface 1's current limit in steps of 100. Both limits are clamped
to their SCPI ranges. Encoder inputs and buttons use internal pull-ups; button
interrupt handlers are present but intentionally empty.

Supported commands:

```
*IDN?
:INFO:CTRL:NUMI[?]
:INFO:NET:MAC[?]
:INFO:NET:IP[?]
:CTL:INT1:VOLT <1..2000>
:CTL:INT2:VOLT <1..2000>
:CTL:INT1:VOLT?
:CTL:INT2:VOLT?
:CTL:INT1:CURR <5..3000>
:CTL:INT2:CURR <5..3000>
:CTL:INT1:CURR?
:CTL:INT2:CURR?
:CTL:INT1:OUTP <ON|OFF|1|0>
:CTL:INT2:OUTP <ON|OFF|1|0>
:CTL:INT1:OUTP?
:CTL:INT2:OUTP?
:CTL:INT1:LOCK <ON|OFF|1|0>
:CTL:INT2:LOCK <ON|OFF|1|0>
:CTL:INT1:LOCK?
:CTL:INT2:LOCK?
```

The voltage scale is displayed as hundredths of a volt, so `2000` appears as
`20.00 V`. A successful write has no TCP response, following normal SCPI
command behavior. Invalid or locked writes return a SCPI-style error line.

Core1 retains a segment buffer and decimal-point bitmap for each display. It
continuously feeds one digit from each buffer every 500 microseconds, giving
every multiplexed digit a 250 Hz refresh rate. SCPI writes only update these
buffers; display scanning continues independently of network activity.

Core1 owns the volatile SCPI register bank. Core0 validates incoming values
and sends `CORE1_SET_*` messages without retaining copies. Register queries
are asynchronous: Core0 sends a `CORE1_GET_*` message, Core1 replies with the
matching `CORE0_DAT_*` message, and Core0 then completes the pending TCP
query. Identification and network properties remain local to Core0.

## Verification

Run the dependency-free verifier from a computer on the same network:

```sh
python3 scpi_verify.py 192.168.1.50
```

Replace the address with the Pico W address. Use `--help` for custom port,
timeout, and test-voltage options. The script checks queries, both voltage
channels, locks, and range validation. Display and UART output still require
visual/electrical confirmation on the hardware.

## OpenDPS UART control

OpenDPS does not use line-oriented ASCII commands. It uses the binary
`uFrame` protocol on USART1. The firmware starts at **9600 baud, 8 data bits,
no parity, 1 stop bit, no flow control**. OpenDPS can persist a different baud
rate, so a unit which has already been configured may start at another rate.

The examples below assume that the active OpenDPS function is `cl` (constant
voltage with a current limit). In this function, voltage values are integer
millivolts and current values are integer milliamps.

### uFrame encoding

Each UART message has this wire format:

```text
7e  <payload and CRC, byte-stuffed>  <CRC16 high> <CRC16 low>  7f
```

- `0x7e` is SOF, `0x7f` is EOF, and `0x7d` is the escape byte.
- In the payload and CRC, bytes `0x7e`, `0x7d`, and `0x7f` are sent as
  `0x7d` followed by the byte XOR `0x20`.
- CRC is CRC-16/CCITT, polynomial `0x1021`, initial value `0x0000`, calculated
  over the unescaped payload only. CRC bytes are sent most-significant first.
- All binary 16-bit fields are big-endian. Parameter names and values are
  NUL-terminated ASCII strings.

The OpenDPS Python utility already implements this framing in
`dpsctl/uframe.py` and the commands in `dpsctl/protocol.py`.

### Read voltage, current, presets, and output state

Send `CMD_QUERY` (`0x04`). The complete example frame is:

```text
7e 04 40 84 7f
```

After removing framing, escapes, and CRC, the response payload is:

| Offset | Data | Meaning |
|---:|---|---|
| 0 | `0x84` | `CMD_RESPONSE | CMD_QUERY` |
| 1 | `uint8` | Status; currently always `1` |
| 2 | `uint16` | Input voltage, mV |
| 4 | `uint16` | **Measured output voltage, mV** |
| 6 | `uint16` | **Measured output current, mA** |
| 8 | `uint8` | Output enabled: `0` or `1` |
| 9 | `int16` | Temperature 1 in tenths of a degree, or `0xffff` |
| 11 | `int16` | Temperature 2 in tenths of a degree, or `0xffff` |
| 13 | `uint8` | Thermal-shutdown flag |
| 14 | `uint8` | Display brightness |
| 15... | C strings | Function name, then parameter-name/value pairs |

For the `cl` function, parse the trailing C strings as follows:

```text
"cl" NUL "voltage" NUL "<mV>" NUL "current" NUL "<mA>" NUL
```

Therefore one query provides all requested read operations:

- Current output voltage: response `v_out` at offsets 4..5.
- Preset output voltage: integer value of `params["voltage"]`.
- Current output current: response `i_out` at offsets 6..7.
- Preset maximum output current: integer value of `params["current"]`.
- Output enabled/disabled: response byte at offset 8.

Do not use the old fixed-field query layout still present in the legacy
`protocol.c` helper; the running firmware and current Python utility use the
dynamic layout above.

### Set output voltage or maximum current

Send `CMD_SET_PARAMETERS` (`0x0e`) followed by one or more NUL-terminated
name/value pairs:

```text
0e "voltage" 00 "12000" 00
0e "current" 00 "1500" 00
```

These examples set 12.000 V and a 1.500 A current limit. Their complete
uFrame messages are:

```text
# Set voltage to 12000 mV
7e 0e 76 6f 6c 74 61 67 65 00 31 32 30 30 30 00 15 2f 7f

# Set current limit to 1500 mA
7e 0e 63 75 72 72 65 6e 74 00 31 35 30 30 00 e7 ee 7f
```

The response payload is `8e 01 <parameter-status>...`. The overall status is
`1`; each requested parameter then has one status byte: `0` success, `1`
unknown name, `2` out of range, `3` unsupported, or `4` flash error. Values
outside the active DPS model/function limits are rejected.

Using the supplied Python module, the equivalent frames are made with:

```python
protocol.create_set_parameter(["voltage=12000"])
protocol.create_set_parameter(["current=1500"])
```

### Enable or disable the output

Send `CMD_ENABLE_OUTPUT` (`0x0c`) followed by a Boolean byte:

```text
# Enable
7e 0c 01 55 4c 7f

# Disable
7e 0c 00 45 6d 7f
```

The response payload is `8c 01` on success or `8c 00` on failure. The Python
helpers are `protocol.create_enable_output("on")` and
`protocol.create_enable_output("off")`.

### Detect current-limiting mode

There is no explicit CC/current-limit flag in the UART query response. Query
the device and, while `output_enabled == 1` and `cur_func == "cl"`, reproduce
the decision used by `func_cl.c`:

```python
voltage_error = abs(int(data["params"]["voltage"]) - data["v_out"])
current_error = abs(int(data["params"]["current"]) - data["i_out"])
current_limiting = current_error < voltage_error
```

This is exactly the OpenDPS UI heuristic for selecting its CC icon; it is not
a separately measured hardware status bit. For control logic that needs
noise immunity, additionally apply a suitable current tolerance and require
the condition for several consecutive samples.
