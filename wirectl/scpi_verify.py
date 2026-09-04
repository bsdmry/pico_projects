#!/usr/bin/env python3
"""Basic functional verification for the wirectl SCPI-over-TCP server."""

import argparse
import socket
import sys


class ScpiClient:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.socket.settimeout(timeout)
        self.reader = self.socket.makefile("r", encoding="ascii", newline="\n")

    def close(self) -> None:
        self.reader.close()
        self.socket.close()

    def write(self, command: str) -> None:
        print(f"> {command}")
        self.socket.sendall((command + "\n").encode("ascii"))

    def query(self, command: str) -> str:
        self.write(command)
        response = self.reader.readline()
        if response == "":
            raise ConnectionError("device closed the TCP connection")
        response = response.rstrip("\r\n")
        print(f"< {response}")
        return response

    def read_response(self) -> str:
        response = self.reader.readline()
        if response == "":
            raise ConnectionError("device closed the TCP connection")
        response = response.rstrip("\r\n")
        print(f"< {response}")
        return response


def check(condition: bool, description: str) -> None:
    if not condition:
        raise AssertionError(description)
    print(f"  PASS: {description}")


def set_v1(client: ScpiClient, voltage: int) -> None:
    client.write(f":CTL:INT1:VOLT {voltage}")
def get_v1(client: ScpiClient) -> str:
    return str(client.query(f":CTL:INT1:VOLT?"))

def set_v2(client: ScpiClient, voltage: int) -> None:
    client.write(f":CTL:INT2:VOLT {voltage}")
def get_v2(client: ScpiClient) -> str:
    return str(client.query(f":CTL:INT2:VOLT?"))


def verify(client: ScpiClient, int1_voltage: int, int2_voltage: int) -> None:
    check(client.query("*IDN?") == "DemoDev", "device identification")
    check(client.query(":INFO:CTRL:NUMI?") == "2", "two control interfaces")

    mac = client.query(":INFO:NET:MAC?")
    check(len(mac) == 12 and all(c in "0123456789abcdefABCDEF" for c in mac),
          "MAC address is 12 hexadecimal digits")

    ip = client.query(":INFO:NET:IP?")
    octets = ip.split(".")
    check(len(octets) == 4 and all(part.isdigit() and 0 <= int(part) <= 255
                                    for part in octets),
          "IPv4 address is valid")

    for interface, voltage in ((1, int1_voltage), (2, int2_voltage)):
        client.write(f":CTL:INT{interface}:LOCK OFF")
        check(client.query(f":CTL:INT{interface}:LOCK?") == "OFF",
              f"interface {interface} unlock")

        client.write(f":CTL:INT{interface}:VOLT {voltage}")
        check(client.query(f":CTL:INT{interface}:VOLT?") == str(voltage),
              f"interface {interface} voltage write/readback")

        client.write(f":CTL:INT{interface}:LOCK ON")
        check(client.query(f":CTL:INT{interface}:LOCK?") == "ON",
              f"interface {interface} lock")

        blocked_voltage = voltage + 1 if voltage < 2000 else voltage - 1
        client.write(f":CTL:INT{interface}:VOLT {blocked_voltage}")
        error = client.read_response()
        check(error.startswith("-221,"),
              f"interface {interface} rejects voltage while locked")
        check(client.query(f":CTL:INT{interface}:VOLT?") == str(voltage),
              f"interface {interface} voltage unchanged while locked")

        client.write(f":CTL:INT{interface}:LOCK OFF")
        check(client.query(f":CTL:INT{interface}:LOCK?") == "OFF",
              f"interface {interface} final unlock")

    client.write(":CTL:INT1:VOLT 2001")
    check(client.read_response().startswith("-222,"), "out-of-range voltage rejected")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="Pico W IP address or hostname")
    parser.add_argument("--port", type=int, default=5025, help="SCPI TCP port (default: 5025)")
    parser.add_argument("--autotest", type=bool, default=False, help="Run autotest (default: False)")
    parser.add_argument("--setv1", type=bool, default=False, help="Set V1, using int1-volatage (default: False)")
    parser.add_argument("--getv1", type=bool, default=False, help="Get V1,  (default: False)")
    parser.add_argument("--setv2", type=bool, default=False, help="Set V2, using int2-volatage (default: False)")
    parser.add_argument("--getv2", type=bool, default=False, help="Get V2,  (default: False)")
    parser.add_argument("--timeout", type=float, default=3.0, help="socket timeout in seconds")
    parser.add_argument("--int1-voltage", type=int, default=1234,
                        choices=range(1, 2001), metavar="1..2000")
    parser.add_argument("--int2-voltage", type=int, default=567,
                        choices=range(1, 2001), metavar="1..2000")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    client = None
    try:
        print(f"Connecting to {args.host}:{args.port}")
        client = ScpiClient(args.host, args.port, args.timeout)
        if args.autotest:
            verify(client, args.int1_voltage, args.int2_voltage)
        if args.setv1:
            set_v1(client, args.int1_voltage)
        if args.setv2:
            set_v2(client, args.int2_voltage)
        if args.getv1:
            print(get_v1(client))
        if args.getv2:
            print(get_v2(client))
        print("\nAll SCPI checks passed.")
        print("Confirm the two displays and UART outputs on the hardware.")
        return 0
    except (AssertionError, ConnectionError, OSError) as error:
        print(f"\nFAIL: {error}", file=sys.stderr)
        return 1
    finally:
        if client is not None:
            # Best-effort cleanup in case a check failed after enabling a lock.
            try:
                client.write(":CTL:INT1:LOCK OFF")
                client.write(":CTL:INT2:LOCK OFF")
            except OSError:
                pass
            client.close()


if __name__ == "__main__":
    raise SystemExit(main())
