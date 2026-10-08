#!/usr/bin/env python3
"""Consolidated Host API system-service validation tool for SR100 over USB."""

from __future__ import annotations

import argparse
import sys

from host_api_common import (
    SYNC1,
    SYNC2,
    add_serial_args,
    build_request,
    exchange,
    expect_header,
    open_port,
    parse_u32,
    print_exchange,
)


SERVICE_SYSTEM = 0x01
ACTIVE_INTERFACE_USB = 0x01

OPCODE_TOGGLE_CRC_CHECK = 0x00
OPCODE_HOST_API_VERSION = 0x01
OPCODE_READ_REGISTER = 0x02
OPCODE_WRITE_REGISTER = 0x03
OPCODE_READ_PENDING_MESSAGE = 0x04
OPCODE_CONFIG_ACTIVE_INTERFACE = 0x05
OPCODE_GET_LOADED_APPS = 0x06
OPCODE_INITIATE_SW_RESET = 0x07
OPCODE_WRITE_I2C_REGISTER = 0x08
OPCODE_READ_I2C_REGISTER = 0x09
ERROR_READ_PENDING_MESSAGE = 0x04


def expect(label: str, actual: bytes, expected: bytes) -> None:
    if actual != expected:
        raise ValueError(f"{label} mismatch: expected {expected.hex(' ')}, got {actual.hex(' ')}")


def run_version(port, args: argparse.Namespace) -> None:
    request = build_request(SERVICE_SYSTEM, OPCODE_HOST_API_VERSION)
    header, payload = exchange(port, request)
    print_exchange("VERSION", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_HOST_API_VERSION, 4)
    if len(payload) != 4:
        raise ValueError(f"payload length mismatch: expected 4, got {len(payload)}")

    sdk_version = f"{payload[1]}.{payload[2]}.{payload[3]}"
    print(f"Version : {sdk_version}")
    print("PASS: version response received")


def run_loaded_apps(port, args: argparse.Namespace) -> None:
    request = build_request(SERVICE_SYSTEM, OPCODE_GET_LOADED_APPS)
    header, payload = exchange(port, request)
    print_exchange("GET_LOADED_APPS", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_GET_LOADED_APPS, 0)
    expect("payload", payload, b"")
    print("PASS: get_loaded_apps returned zero-length response")


def run_toggle_crc(port, args: argparse.Namespace) -> None:
    request = build_request(SERVICE_SYSTEM, OPCODE_TOGGLE_CRC_CHECK)
    header, payload = exchange(port, request)
    print_exchange("TOGGLE_CRC", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_TOGGLE_CRC_CHECK, 0)
    expect("payload", payload, b"")
    print("PASS: toggle_crc command path responded successfully")


def run_read_pending_empty(port, args: argparse.Namespace) -> None:
    request = build_request(SERVICE_SYSTEM, OPCODE_READ_PENDING_MESSAGE)
    header, payload = exchange(port, request)
    print_exchange("READ_PENDING", request, header, payload, verbose=args.verbose)
    expected_header = bytes([SYNC1, SYNC2, ERROR_READ_PENDING_MESSAGE, 0x00, 0x00, 0x00, 0x00, 0x00])
    expect("header", header, expected_header)
    expect("payload", payload, b"")
    print("PASS: read_pending_message reported no pending event as expected")


def run_config_active_interface_usb(port, args: argparse.Namespace) -> None:
    request = build_request(
        SERVICE_SYSTEM,
        OPCODE_CONFIG_ACTIVE_INTERFACE,
        bytes([ACTIVE_INTERFACE_USB]),
    )
    header, payload = exchange(port, request)
    print_exchange("CONFIG_USB", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_CONFIG_ACTIVE_INTERFACE, 0)
    expect("payload", payload, b"")
    print("PASS: config_active_interface(USB) returned success")


def run_read_register(port, args: argparse.Namespace, address: int, expected_value: int | None) -> int:
    request = build_request(SERVICE_SYSTEM, OPCODE_READ_REGISTER, address.to_bytes(4, "little"))
    header, payload = exchange(port, request)
    print_exchange("READ_REGISTER", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_READ_REGISTER, 4)
    if len(payload) != 4:
        raise ValueError(f"payload length mismatch: expected 4, got {len(payload)}")

    value = int.from_bytes(payload, "little")
    print(f"Value   : 0x{value:08x}")
    if expected_value is not None and value != expected_value:
        raise ValueError(
            f"register value mismatch: expected 0x{expected_value:08x}, got 0x{value:08x}"
        )
    print("PASS: read_register returned expected value")
    return value


def run_write_register(port, args: argparse.Namespace, address: int, value: int) -> None:
    payload = address.to_bytes(4, "little") + value.to_bytes(4, "little")
    request = build_request(SERVICE_SYSTEM, OPCODE_WRITE_REGISTER, payload)
    header, resp_payload = exchange(port, request)
    print_exchange("WRITE_REGISTER", request, header, resp_payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_WRITE_REGISTER, 0)
    expect("payload", resp_payload, b"")
    print("PASS: write_register returned success")


def run_reset(port, args: argparse.Namespace) -> None:
    request = build_request(SERVICE_SYSTEM, OPCODE_INITIATE_SW_RESET)
    header, payload = exchange(port, request)
    print_exchange("RESET", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_INITIATE_SW_RESET, 0)
    expect("payload", payload, b"")
    print("PASS: reset command acknowledged; device should reboot immediately")


def run_i2c_read(
    port,
    args: argparse.Namespace,
    slave: int,
    register: int,
    expected_rc: int | None,
    expected_value: int | None,
) -> tuple[int, int]:
    request = build_request(
        SERVICE_SYSTEM,
        OPCODE_READ_I2C_REGISTER,
        bytes([slave & 0xFF, register & 0xFF]),
    )
    header, payload = exchange(port, request)
    print_exchange("I2C_READ", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_READ_I2C_REGISTER, 5)
    if len(payload) != 5:
        raise ValueError(f"payload length mismatch: expected 5, got {len(payload)}")

    rc = int.from_bytes(payload[0:4], "little", signed=True)
    value = payload[4]
    print(f"RC      : {rc}")
    print(f"Value   : 0x{value:02x}")

    if expected_rc is not None and rc != expected_rc:
        raise ValueError(f"i2c rc mismatch: expected {expected_rc}, got {rc}")
    if expected_value is not None and value != expected_value:
        raise ValueError(f"i2c value mismatch: expected 0x{expected_value:02x}, got 0x{value:02x}")

    print("PASS: read_i2c_register returned expected response")
    return rc, value


def run_i2c_write(
    port,
    args: argparse.Namespace,
    slave: int,
    register: int,
    value: int,
    expected_rc: int | None,
) -> int:
    request = build_request(
        SERVICE_SYSTEM,
        OPCODE_WRITE_I2C_REGISTER,
        bytes([slave & 0xFF, register & 0xFF, value & 0xFF]),
    )
    header, payload = exchange(port, request)
    print_exchange("I2C_WRITE", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_SYSTEM, OPCODE_WRITE_I2C_REGISTER, 4)
    if len(payload) != 4:
        raise ValueError(f"payload length mismatch: expected 4, got {len(payload)}")

    rc = int.from_bytes(payload, "little", signed=True)
    print(f"RC      : {rc}")
    if expected_rc is not None and rc != expected_rc:
        raise ValueError(f"i2c rc mismatch: expected {expected_rc}, got {rc}")

    print("PASS: write_i2c_register returned expected response")
    return rc


def run_basic(port, args: argparse.Namespace) -> None:
    run_version(port, args)
    run_loaded_apps(port, args)
    run_toggle_crc(port, args)
    run_read_pending_empty(port, args)
    run_config_active_interface_usb(port, args)


def run_query(port, args: argparse.Namespace) -> None:
    run_version(port, args)
    run_loaded_apps(port, args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Consolidated Zephyr Host API system-service validation tool over USB"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    for name in (
        "query",
        "version",
        "loaded-apps",
        "toggle-crc",
        "read-pending-empty",
        "config-usb",
        "reset",
        "basic",
    ):
        sub = subparsers.add_parser(name)
        add_serial_args(sub, timeout_default=3.0)
        sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")

    sub = subparsers.add_parser("read-register")
    add_serial_args(sub, timeout_default=3.0)
    sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    sub.add_argument("--address", required=True, type=parse_u32, help="32-bit target address")
    sub.add_argument("--expected", type=parse_u32, help="Expected 32-bit value")

    sub = subparsers.add_parser("write-register")
    add_serial_args(sub, timeout_default=3.0)
    sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    sub.add_argument("--address", required=True, type=parse_u32, help="32-bit target address")
    sub.add_argument("--value", required=True, type=parse_u32, help="32-bit value to write")

    sub = subparsers.add_parser("register-rw")
    add_serial_args(sub, timeout_default=3.0)
    sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    sub.add_argument("--address", required=True, type=parse_u32, help="32-bit validation register address")
    sub.add_argument("--initial", required=True, type=parse_u32, help="Expected initial 32-bit value")
    sub.add_argument("--write-value", required=True, type=parse_u32, help="32-bit value to write")

    sub = subparsers.add_parser("i2c-read")
    add_serial_args(sub, timeout_default=3.0)
    sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    sub.add_argument("--slave", required=True, type=parse_u32, help="7-bit I2C slave address")
    sub.add_argument("--register", required=True, type=parse_u32, help="8-bit register address")
    sub.add_argument("--expected-rc", type=int, help="Expected signed I2C return code")
    sub.add_argument("--expected-value", type=parse_u32, help="Expected 8-bit register value")

    sub = subparsers.add_parser("i2c-write")
    add_serial_args(sub, timeout_default=3.0)
    sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    sub.add_argument("--slave", required=True, type=parse_u32, help="7-bit I2C slave address")
    sub.add_argument("--register", required=True, type=parse_u32, help="8-bit register address")
    sub.add_argument("--value", required=True, type=parse_u32, help="8-bit value to write")
    sub.add_argument("--expected-rc", type=int, help="Expected signed I2C return code")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        with open_port(args) as port:
            if args.command == "query":
                run_query(port, args)
            elif args.command == "version":
                run_version(port, args)
            elif args.command == "loaded-apps":
                run_loaded_apps(port, args)
            elif args.command == "toggle-crc":
                run_toggle_crc(port, args)
            elif args.command == "read-pending-empty":
                run_read_pending_empty(port, args)
            elif args.command == "config-usb":
                run_config_active_interface_usb(port, args)
            elif args.command == "read-register":
                run_read_register(port, args, args.address, args.expected)
            elif args.command == "write-register":
                run_write_register(port, args, args.address, args.value)
            elif args.command == "register-rw":
                run_read_register(port, args, args.address, args.initial)
                run_write_register(port, args, args.address, args.write_value)
                run_read_register(port, args, args.address, args.write_value)
            elif args.command == "reset":
                run_reset(port, args)
            elif args.command == "i2c-read":
                run_i2c_read(
                    port,
                    args,
                    args.slave,
                    args.register,
                    args.expected_rc,
                    None if args.expected_value is None else (args.expected_value & 0xFF),
                )
            elif args.command == "i2c-write":
                run_i2c_write(port, args, args.slave, args.register, args.value & 0xFF, args.expected_rc)
            elif args.command == "basic":
                run_basic(port, args)
            else:
                raise ValueError(f"unsupported command: {args.command}")

        return 0
    except Exception as exc:  # pylint: disable=broad-except
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
