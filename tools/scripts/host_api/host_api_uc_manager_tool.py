#!/usr/bin/env python3
"""Host API UC manager validation tool for SR100 over USB."""

from __future__ import annotations

import argparse
import sys

from host_api_common import (
    add_serial_args,
    build_request,
    exchange,
    expect_header,
    open_port,
    parse_u32,
    print_exchange,
)


SERVICE_UC_MANAGER = 0x06

OPCODE_CREATE_USECASE = 0x01
OPCODE_START_USECASE = 0x02
OPCODE_STOP_USECASE = 0x03
OPCODE_RESUME_USECASE = 0x04
OPCODE_KILL_USECASE = 0x05


def expect(label: str, actual: bytes, expected: bytes) -> None:
    if actual != expected:
        raise ValueError(f"{label} mismatch: expected {expected.hex(' ')}, got {actual.hex(' ')}")


def run_uc_command(
    port,
    args: argparse.Namespace,
    label: str,
    opcode: int,
    usecase_id: int,
) -> None:
    request = build_request(SERVICE_UC_MANAGER, opcode, bytes([usecase_id & 0xFF]))
    header, payload = exchange(port, request)
    print_exchange(label, request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_UC_MANAGER, opcode, 0)
    expect("payload", payload, b"")
    print(f"PASS: {label.lower()}({usecase_id}) returned success")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Zephyr Host API UC manager validation tool over USB"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    for name in ("create", "start", "stop", "resume", "kill"):
        sub = subparsers.add_parser(name)
        add_serial_args(sub, timeout_default=3.0)
        sub.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
        sub.add_argument("--usecase-id", required=True, type=parse_u32, help="8-bit usecase ID")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        with open_port(args) as port:
            if args.command == "create":
                run_uc_command(port, args, "UC_CREATE", OPCODE_CREATE_USECASE, args.usecase_id)
            elif args.command == "start":
                run_uc_command(port, args, "UC_START", OPCODE_START_USECASE, args.usecase_id)
            elif args.command == "stop":
                run_uc_command(port, args, "UC_STOP", OPCODE_STOP_USECASE, args.usecase_id)
            elif args.command == "resume":
                run_uc_command(port, args, "UC_RESUME", OPCODE_RESUME_USECASE, args.usecase_id)
            elif args.command == "kill":
                run_uc_command(port, args, "UC_KILL", OPCODE_KILL_USECASE, args.usecase_id)
            else:
                raise ValueError(f"unsupported command: {args.command}")

        return 0
    except Exception as exc:  # pylint: disable=broad-except
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
