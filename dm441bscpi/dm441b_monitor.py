#!/usr/bin/env python3
"""Display an EZ DM-441B reading and LED states over SCPI TCP (standard library only)."""
import argparse
from dataclasses import dataclass
import math
import socket
import sys
import time

LED_NAMES = (
    (0, 'Ohm'), (1, 'MOhm'), (2, 'V'), (3, 'A'), (4, 'kOhm'),
    (5, 'mV'), (6, 'mA'), (7, 'R'), (9, 'NEG'), (10, 'HOLD'),
    (11, 'DIODE'), (12, 'DC'), (13, 'AC'), (14, 'Freq/kHz'), (15, 'hFE'),
)


@dataclass(frozen=True)
class Reading:
    status: int
    magnitude: int
    decimal_position: int
    negative: bool
    underrange: bool
    overrange: bool
    leds: int

    @property
    def text(self):
        # Integer formatting preserves trailing display zeroes without float rounding.
        digits = str(self.magnitude).zfill(5)
        fractional = 4 - self.decimal_position
        value = digits[:-fractional] + '.' + digits[-fractional:]
        return ('-' if self.negative else '') + value


def decode_status(status):
    if not 0 <= status < (1 << 64):
        raise ValueError('status is outside the unsigned 64-bit range')
    magnitude = (status >> 16) & ((1 << 43) - 1)
    if magnitude > 99999:
        raise ValueError('status magnitude exceeds the five-digit display')
    return Reading(status, magnitude, (status >> 61) & 3, bool(status >> 63),
                   bool(status & (1 << 60)), bool(status & (1 << 59)), status & 0xffff)


class Meter:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buffer = bytearray()

    def close(self):
        self.sock.close()

    def query(self, command):
        self.sock.sendall(command.encode('ascii') + b'\n')
        while b'\n' not in self.buffer:
            chunk = self.sock.recv(256)
            if not chunk:
                raise ConnectionError('meter closed the connection')
            self.buffer.extend(chunk)
            if len(self.buffer) > 1024:
                raise ValueError('meter response is too long')
        line, _, rest = self.buffer.partition(b'\n')
        self.buffer = bytearray(rest)
        return line.decode('ascii').strip()

    def read(self):
        response = self.query('STAT?')
        if not response or any(ch not in '0123456789' for ch in response):
            raise ValueError('invalid status response: ' + repr(response))
        return decode_status(int(response))


def render(name, reading):
    flags = []
    if reading.underrange:
        flags.append('UNDERRANGE')
    if reading.overrange:
        flags.append('OVERRANGE')
    lines = [name, '', 'Reading: ' + reading.text,
             'Range:   ' + (', '.join(flags) or 'normal'), '', 'LEDs (ON = lit):']
    for start in range(0, len(LED_NAMES), 5):
        lines.append('  '.join(f'{label:8} {"ON " if reading.leds & (1 << bit) else "off"}'
                               for bit, label in LED_NAMES[start:start + 5]))
    lines += ['', f'Status: {reading.status}  (0x{reading.status:016X})',
              'Saved samples; firmware does not report age or validity.']
    return '\n'.join(lines)


def positive_float(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError('must be a finite number greater than zero')
    return number


def port_number(value):
    port = int(value)
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError('must be between 1 and 65535')
    return port


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='meter IP address or hostname')
    parser.add_argument('--port', type=port_number, default=5025)
    parser.add_argument('--interval', type=positive_float, default=0.25,
                        help='seconds between queries (default: 0.25)')
    parser.add_argument('--timeout', type=positive_float, default=3.0,
                        help='socket timeout in seconds (default: 3)')
    parser.add_argument('--once', action='store_true', help='print one reading and exit')
    args = parser.parse_args(argv)
    interactive = sys.stdout.isatty() and not args.once
    meter = None
    try:
        meter = Meter(args.host, args.port, args.timeout)
        name = meter.query('*IDN?')
        if name != 'EZ DM-441B':
            raise ValueError('unexpected device identity: ' + repr(name))
        while True:
            reading = meter.read()
            if interactive:
                print('\033[2J\033[H', end='')
            else:
                print(time.strftime('%Y-%m-%d %H:%M:%S'))
            print(render(name, reading), flush=True)
            if args.once:
                return 0
            if interactive:
                print('\nCtrl+C to exit.', flush=True)
            else:
                print()
            time.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, UnicodeError) as error:
        print(f'Meter error: {error}', file=sys.stderr)
        return 1
    finally:
        if meter is not None:
            meter.close()


if __name__ == '__main__':
    sys.exit(main())
