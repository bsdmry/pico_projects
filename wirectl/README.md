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
