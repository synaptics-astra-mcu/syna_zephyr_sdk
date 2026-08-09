#!/usr/bin/env python3
"""Common Host API serial transport helpers for USB tools."""

from __future__ import annotations

import argparse
import time

import serial
from serial.tools import list_ports


SYNC1 = 0x5B
SYNC2 = 0x5A

DEFAULT_USB_VID = 0xCAFE
DEFAULT_USB_PID = 0x4002

def format_bytes(data: bytes) -> str:
    return " ".join(f"{byte:02x}" for byte in data)


def parse_u32(value: str) -> int:
    return int(value, 0) & 0xFFFFFFFF


def build_request(
    service_id: int,
    opcode: int,
    payload: bytes = b"",
    *,
    ack: int = 0,
    no_response: int = 0,
) -> bytes:
    flags = (service_id & 0x3F) | ((no_response & 0x1) << 6) | ((ack & 0x1) << 7)
    return bytes([SYNC1, SYNC2, flags, opcode]) + len(payload).to_bytes(4, "little") + payload


def read_exact(port: serial.Serial, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk:
            raise TimeoutError(f"timed out while reading {size} bytes, got {len(data)}")
        data.extend(chunk)
    return bytes(data)


def exchange(port: serial.Serial, request: bytes) -> tuple[bytes, bytes]:
    port.reset_input_buffer()
    port.write(request)
    port.flush()

    header = read_exact(port, 8)
    payload_len = int.from_bytes(header[4:8], "little")
    payload = read_exact(port, payload_len)
    return header, payload


def expect_header(header: bytes, service_id: int, opcode: int, payload_len: int) -> None:
    expected = bytes([SYNC1, SYNC2, service_id, opcode]) + payload_len.to_bytes(4, "little")
    if header != expected:
        raise ValueError(
            f"header mismatch: expected {format_bytes(expected)}, got {format_bytes(header)}"
        )


def print_exchange(
    label: str,
    request: bytes,
    header: bytes,
    payload: bytes,
    *,
    verbose: bool,
    max_tx_bytes: int = 64,
) -> None:
    if not verbose:
        return

    print(f"{label}:")

    if len(request) <= max_tx_bytes:
        print(f"  TX      : {format_bytes(request)}")
    else:
        preview = format_bytes(request[:max_tx_bytes])
        print(f"  TX      : {preview} ... ({len(request)} bytes total)")

    print(f"  Header  : {format_bytes(header)}")
    print(f"  Payload : {format_bytes(payload)}")


def describe_port(port_info: list_ports.ListPortInfo) -> str:
    parts = [port_info.device]

    if port_info.vid is not None and port_info.pid is not None:
        parts.append(f"VID:PID={port_info.vid:04x}:{port_info.pid:04x}")

    if port_info.product:
        parts.append(f"product={port_info.product}")
    elif port_info.description:
        parts.append(f"description={port_info.description}")

    if getattr(port_info, "interface", None):
        parts.append(f"interface={port_info.interface}")

    return ", ".join(parts)


def find_matching_ports(vid: int, pid: int) -> list[list_ports.ListPortInfo]:
    matches: list[list_ports.ListPortInfo] = []

    for port_info in list_ports.comports():
        if port_info.vid == vid and port_info.pid == pid:
            matches.append(port_info)

    return matches


def resolve_port_name(args: argparse.Namespace) -> str:
    manual_port = getattr(args, "port", None)
    if manual_port:
        return manual_port

    vid = getattr(args, "vid", DEFAULT_USB_VID)
    pid = getattr(args, "pid", DEFAULT_USB_PID)

    matches = find_matching_ports(vid, pid)
    if not matches:
        raise FileNotFoundError(
            "could not auto-detect Host API port for "
            f"VID:PID {vid:04x}:{pid:04x}"
        )

    if len(matches) > 1:
        candidates = "\n".join(f"  - {describe_port(port_info)}" for port_info in matches)
        raise RuntimeError(
            "multiple Host API ports matched the requested VID/PID; use --port to choose one:\n"
            f"{candidates}"
        )

    selected = matches[0]
    if not getattr(args, "_auto_port_reported", False):
        print(f"Auto-detected Host API port by VID/PID: {describe_port(selected)}")
        args._auto_port_reported = True

    return selected.device


def add_serial_args(
    parser: argparse.ArgumentParser,
    *,
    timeout_default: float,
    include_reconnect_timeout: bool = False,
) -> None:
    parser.add_argument("--port", help="Serial port name, for example COM7 or /dev/ttyACM0")
    parser.add_argument(
        "--vid",
        type=parse_u32,
        default=DEFAULT_USB_VID,
        help=f"USB vendor ID used for auto-detect, default 0x{DEFAULT_USB_VID:04x}",
    )
    parser.add_argument(
        "--pid",
        type=parse_u32,
        default=DEFAULT_USB_PID,
        help=f"USB product ID used for auto-detect, default 0x{DEFAULT_USB_PID:04x}",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="Serial baud rate for the CDC ACM port",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=timeout_default,
        help="Read timeout in seconds",
    )
    parser.add_argument(
        "--settle-ms",
        type=int,
        default=200,
        help="Delay after opening the port before sending commands",
    )

    if include_reconnect_timeout:
        parser.add_argument(
            "--reconnect-timeout",
            type=float,
            default=20.0,
            help="Seconds to wait for the CDC port to return after reboot",
        )


def open_port(args: argparse.Namespace) -> serial.Serial:
    port_name = resolve_port_name(args)
    port = serial.Serial(port_name, baudrate=args.baudrate, timeout=args.timeout)
    port.dtr = True
    port.rts = True
    time.sleep(args.settle_ms / 1000.0)
    return port


def reopen_after_reset(args: argparse.Namespace, delay_s: float = 1.0) -> serial.Serial:
    deadline = time.time() + getattr(args, "reconnect_timeout", 20.0)
    last_error: Exception | None = None

    time.sleep(delay_s)
    while time.time() < deadline:
        try:
            return open_port(args)
        except Exception as exc:  # pylint: disable=broad-except
            last_error = exc
            time.sleep(0.5)

    raise TimeoutError(f"Host API port did not return after reset: {last_error}")
