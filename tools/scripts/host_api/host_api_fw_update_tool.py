#!/usr/bin/env python3
"""Consolidated managed FW update tool for Host API over USB."""

from __future__ import annotations

import argparse
import math
import sys
import time
from pathlib import Path

from host_api_common import (
    add_serial_args,
    build_request,
    exchange,
    expect_header,
    open_port,
    parse_u32,
    print_exchange,
    reopen_after_reset,
)


SERVICE_FW_UPDATE = 0x0B

FW_UPDATE_OPCODE_START = 0x01
FW_UPDATE_OPCODE_WRITE = 0x02
FW_UPDATE_OPCODE_FINISH = 0x03
FW_UPDATE_OPCODE_INSTALL = 0x04
FW_UPDATE_OPCODE_REBOOT = 0x06
FW_UPDATE_OPCODE_ACCEPT = 0x07
FW_UPDATE_OPCODE_CLEAN = 0x09
FW_UPDATE_OPCODE_GET_INFO = 0x0A
FW_UPDATE_OPCODE_GET_STATE = 0x0B
FW_UPDATE_OPCODE_GET_COMPONENT_STATE = 0x0C
FW_UPDATE_OPCODE_GET_FAILURE = 0x0D
FW_UPDATE_OPCODE_GET_COMPONENT_FAILURE = 0x0E

FW_UPDATE_STATE_READY = 0
FW_UPDATE_STATE_WRITING = 1
FW_UPDATE_STATE_CANDIDATE = 2
FW_UPDATE_STATE_STAGED = 3
FW_UPDATE_STATE_TRIAL = 4
FW_UPDATE_STATE_REJECTED = 5
FW_UPDATE_STATE_FAILED = 6
FW_UPDATE_STATE_UPDATED = 7

FW_UPDATE_RC_OK = 0
SDK_IMAGE_ID_DEFAULT = 2
FW_UPDATE_SECTOR_SIZE = 4096

FLASH_ATTR_MAGIC_BYTES = bytes([0x5B, 0xB0, 0x00, 0x55])
NVM_MAGIC = 0x46574E56
NVM_A_OFFSET = 0x4A000
NVM_RECORD_SIZE = 508
NVM_IMAGE_OFFSET_BASE = 12
SDK_A_OFFSET_INDEX = 0
SDK_B_OFFSET_INDEX = 1


def read_u32_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little")


def expect_state(label: str, actual: int, expected: int) -> None:
    expect_state_with_report(label, actual, expected, report=True)


def expect_state_with_report(label: str, actual: int, expected: int, *, report: bool) -> None:
    if actual != expected:
        raise ValueError(f"{label}: expected state {expected}, got {actual}")
    if report:
        print(f"PASS: {label} == {expected}")


def load_image(path: Path) -> bytes:
    if not path.is_file():
        raise FileNotFoundError(f"image file not found: {path}")

    data = path.read_bytes()
    if not data:
        raise ValueError(f"image file is empty: {path}")

    return data


def try_extract_sdk_payload(image: bytes) -> tuple[bytes, str]:
    if len(image) < 4 or image[0:4] != FLASH_ATTR_MAGIC_BYTES:
        return image, "direct-image"

    if len(image) < (NVM_A_OFFSET + NVM_RECORD_SIZE):
        raise ValueError("full flash image detected, but the embedded NVM record is incomplete")

    if read_u32_le(image, NVM_A_OFFSET) != NVM_MAGIC:
        raise ValueError("full flash image detected, but the embedded NVM magic is invalid")

    image_offsets_base = NVM_A_OFFSET + NVM_IMAGE_OFFSET_BASE
    sdk_a_offset = read_u32_le(image, image_offsets_base + (SDK_A_OFFSET_INDEX * 4))
    sdk_b_offset = read_u32_le(image, image_offsets_base + (SDK_B_OFFSET_INDEX * 4))

    if sdk_a_offset >= len(image):
        raise ValueError(f"embedded SDK A offset 0x{sdk_a_offset:08x} is outside the file")

    if sdk_b_offset > sdk_a_offset and sdk_b_offset <= len(image):
        sdk_end_offset = sdk_b_offset
    else:
        sdk_end_offset = len(image)

    if sdk_end_offset <= sdk_a_offset:
        raise ValueError(f"invalid SDK payload range 0x{sdk_a_offset:08x}..0x{sdk_end_offset:08x}")

    return image[sdk_a_offset:sdk_end_offset], (
        f"full-flash-image: extracted SDK payload 0x{sdk_a_offset:08x}..0x{sdk_end_offset:08x}"
    )


def run_get_info(port, args: argparse.Namespace) -> tuple[int, int]:
    request = build_request(SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_INFO)
    header, payload = exchange(port, request)
    print_exchange("GET_INFO", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_INFO, 8)
    if len(payload) != 8:
        raise ValueError(f"GET_INFO: expected 8-byte payload, got {len(payload)}")

    fw_version = int.from_bytes(payload[0:4], "little")
    write_max = int.from_bytes(payload[4:8], "little")
    print(f"  FW Ver  : 0x{fw_version:08x}")
    print(f"  MaxWr   : {write_max}")
    return fw_version, write_max


def run_get_state(port, args: argparse.Namespace) -> int:
    request = build_request(SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_STATE)
    header, payload = exchange(port, request)
    print_exchange("GET_STATE", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_STATE, 4)
    return int.from_bytes(payload, "little")


def run_get_component_state(port, args: argparse.Namespace, image_id: int) -> int:
    request = build_request(
        SERVICE_FW_UPDATE,
        FW_UPDATE_OPCODE_GET_COMPONENT_STATE,
        image_id.to_bytes(4, "little"),
    )
    header, payload = exchange(port, request)
    print_exchange("GET_COMPONENT_STATE", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_COMPONENT_STATE, 4)
    return int.from_bytes(payload, "little")


def run_get_failure(port, args: argparse.Namespace) -> int:
    request = build_request(SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_FAILURE)
    header, payload = exchange(port, request)
    print_exchange("GET_FAILURE", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_FAILURE, 4)
    return int.from_bytes(payload, "little")


def run_get_component_failure(port, args: argparse.Namespace, image_id: int) -> int:
    request = build_request(
        SERVICE_FW_UPDATE,
        FW_UPDATE_OPCODE_GET_COMPONENT_FAILURE,
        image_id.to_bytes(4, "little"),
    )
    header, payload = exchange(port, request)
    print_exchange("GET_COMPONENT_FAILURE", request, header, payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, FW_UPDATE_OPCODE_GET_COMPONENT_FAILURE, 4)
    return int.from_bytes(payload, "little")


def run_rc_command(port, args: argparse.Namespace, label: str, opcode: int, payload: bytes = b"") -> int:
    request = build_request(SERVICE_FW_UPDATE, opcode, payload)
    header, resp_payload = exchange(port, request)
    print_exchange(label, request, header, resp_payload, verbose=args.verbose)
    expect_header(header, SERVICE_FW_UPDATE, opcode, 4)
    if len(resp_payload) != 4:
        raise ValueError(f"{label}: expected 4-byte RC payload, got {len(resp_payload)}")

    rc = int.from_bytes(resp_payload, "little")
    if args.verbose:
        print(f"  RC      : 0x{rc:08x}")
    if rc != FW_UPDATE_RC_OK:
        raise ValueError(f"{label}: FW update RC is 0x{rc:08x}")
    return rc


def recover_to_ready(port, args: argparse.Namespace):
    state = run_get_state(port, args)
    component_state = run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT)

    if state == FW_UPDATE_STATE_READY:
        print("PASS: FW update state is already READY")
        return port

    if state == FW_UPDATE_STATE_STAGED:
        expect_state("component state during STAGED recovery", component_state, FW_UPDATE_STATE_STAGED)
        run_rc_command(port, args, "REBOOT", FW_UPDATE_OPCODE_REBOOT)
        port.close()
        port = reopen_after_reset(args)
        state = run_get_state(port, args)
        component_state = run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT)

    if state == FW_UPDATE_STATE_TRIAL:
        expect_state("component state during TRIAL recovery", component_state, FW_UPDATE_STATE_TRIAL)
        run_rc_command(port, args, "ACCEPT", FW_UPDATE_OPCODE_ACCEPT)
        expect_state("global state after ACCEPT", run_get_state(port, args), FW_UPDATE_STATE_UPDATED)
        expect_state(
            "component state after ACCEPT",
            run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
            FW_UPDATE_STATE_UPDATED,
        )
        state = FW_UPDATE_STATE_UPDATED

    if state in (FW_UPDATE_STATE_UPDATED, FW_UPDATE_STATE_REJECTED, FW_UPDATE_STATE_FAILED):
        run_rc_command(
            port,
            args,
            "CLEAN",
            FW_UPDATE_OPCODE_CLEAN,
            SDK_IMAGE_ID_DEFAULT.to_bytes(4, "little"),
        )
        expect_state("global state after CLEAN", run_get_state(port, args), FW_UPDATE_STATE_READY)
        expect_state(
            "component state after CLEAN",
            run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
            FW_UPDATE_STATE_READY,
        )
        print("PASS: FW update state recovered to READY")
        return port

    raise ValueError(
        f"recover-ready does not support current global state {state}; "
        "expected READY/STAGED/TRIAL/UPDATED/REJECTED/FAILED"
    )


def print_sector_progress(offset: int, chunk_size: int, total_size: int) -> None:
    end_off = offset + chunk_size
    if ((offset % FW_UPDATE_SECTOR_SIZE) != 0) and (end_off != total_size):
        return

    sector_index = offset // FW_UPDATE_SECTOR_SIZE
    sector_offset = sector_index * FW_UPDATE_SECTOR_SIZE
    percent = (end_off * 100.0) / total_size
    print(f"[{percent:3.0f}%] @0x{sector_offset:08x} Flash Programming sector# {sector_index}")


def write_image(port, args: argparse.Namespace, image: bytes, chunk_size: int) -> None:
    total_chunks = math.ceil(len(image) / chunk_size)

    for chunk_idx in range(total_chunks):
        offset = chunk_idx * chunk_size
        chunk = image[offset:offset + chunk_size]
        print_sector_progress(offset, len(chunk), len(image))
        payload = SDK_IMAGE_ID_DEFAULT.to_bytes(4, "little") + chunk
        run_rc_command(port, args, f"WRITE[{chunk_idx + 1}]", FW_UPDATE_OPCODE_WRITE, payload)


def run_query(args: argparse.Namespace) -> None:
    with open_port(args) as port:
        run_get_info(port, args)
        print(f"State             : {run_get_state(port, args)}")
        print(f"Component State   : {run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT)}")
        print(f"Failure           : 0x{run_get_failure(port, args):08x}")
        print(f"Component Failure : 0x{run_get_component_failure(port, args, SDK_IMAGE_ID_DEFAULT):08x}")


def run_flash(args: argparse.Namespace) -> None:
    image_path = Path(args.bin_file)
    source_image = load_image(image_path)
    image, image_kind = try_extract_sdk_payload(source_image)

    port = open_port(args)
    try:
        _, max_write_size = run_get_info(port, args)
        chunk_size = max_write_size
        if args.chunk_size is not None:
            if args.chunk_size > max_write_size:
                raise ValueError(
                    f"requested chunk size {args.chunk_size} exceeds max write size {max_write_size}"
                )
            chunk_size = args.chunk_size

        print(f"Image file : {image_path}")
        print(f"Image kind : {image_kind}")
        print(f"Input size : {len(source_image)} bytes")
        print(f"Write size : {len(image)} bytes")
        print(f"Chunk size : {chunk_size} bytes")

        initial_state = run_get_state(port, args)
        if initial_state != FW_UPDATE_STATE_READY:
            print(f"Initial FW update state is {initial_state}; recovering to READY...")
            port = recover_to_ready(port, args)

        if not args.skip_ready_check:
            expect_state_with_report(
                "initial global state",
                run_get_state(port, args),
                FW_UPDATE_STATE_READY,
                report=False,
            )
            expect_state_with_report(
                "initial component state",
                run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
                FW_UPDATE_STATE_READY,
                report=False,
            )
            print("State check: READY")

        flash_start = time.time()
        print("Starting managed FW update...")
        run_rc_command(
            port,
            args,
            "START",
            FW_UPDATE_OPCODE_START,
            SDK_IMAGE_ID_DEFAULT.to_bytes(4, "little"),
        )
        expect_state_with_report(
            "global state after START",
            run_get_state(port, args),
            FW_UPDATE_STATE_WRITING,
            report=False,
        )
        expect_state_with_report(
            "component state after START",
            run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
            FW_UPDATE_STATE_WRITING,
            report=False,
        )

        write_image(port, args, image, chunk_size)

        run_rc_command(
            port,
            args,
            "FINISH",
            FW_UPDATE_OPCODE_FINISH,
            SDK_IMAGE_ID_DEFAULT.to_bytes(4, "little"),
        )
        expect_state_with_report(
            "global state after FINISH",
            run_get_state(port, args),
            FW_UPDATE_STATE_WRITING,
            report=False,
        )
        expect_state_with_report(
            "component state after FINISH",
            run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
            FW_UPDATE_STATE_CANDIDATE,
            report=False,
        )

        print("Installing staged image...")
        run_rc_command(
            port,
            args,
            "INSTALL",
            FW_UPDATE_OPCODE_INSTALL,
            int(args.install_auto_reset).to_bytes(4, "little"),
        )
        expect_state_with_report(
            "global state after INSTALL",
            run_get_state(port, args),
            FW_UPDATE_STATE_STAGED,
            report=False,
        )
        expect_state_with_report(
            "component state after INSTALL",
            run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
            FW_UPDATE_STATE_STAGED,
            report=False,
        )
        print(f"Flash burn completed successfully, Programming time: {time.time() - flash_start:.0f} sec")

        if not args.reboot:
            print("PASS: image staged successfully; reboot not requested")
            return

        if not args.install_auto_reset:
            print("Rebooting into trial image...")
            run_rc_command(port, args, "REBOOT", FW_UPDATE_OPCODE_REBOOT)
    finally:
        port.close()

    post_reset_validation = args.expect_trial or args.accept or args.clean
    if not post_reset_validation:
        print("PASS: reboot requested; no post-reset Host API check selected")
        print("      staged image should take over after reset")
        return

    with reopen_after_reset(args) as port:
        if args.expect_trial:
            expect_state_with_report(
                "global state after reboot",
                run_get_state(port, args),
                FW_UPDATE_STATE_TRIAL,
                report=False,
            )
            expect_state_with_report(
                "component state after reboot",
                run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
                FW_UPDATE_STATE_TRIAL,
                report=False,
            )
            print("Trial boot check: OK")

    print("PASS: managed FW update image flashing completed")


def run_post_reboot_recovery(args: argparse.Namespace) -> None:
    with open_port(args) as port:
        state = run_get_state(port, args)
        component_state = run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT)

        print(f"Current State     : {state}")
        print(f"Component State   : {component_state}")

        if args.accept:
            expect_state_with_report(
                "component state before ACCEPT",
                component_state,
                FW_UPDATE_STATE_TRIAL,
                report=False,
            )
            print("Accepting trial image...")
            run_rc_command(port, args, "ACCEPT", FW_UPDATE_OPCODE_ACCEPT)
            expect_state_with_report(
                "global state after ACCEPT",
                run_get_state(port, args),
                FW_UPDATE_STATE_UPDATED,
                report=False,
            )
            expect_state_with_report(
                "component state after ACCEPT",
                run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
                FW_UPDATE_STATE_UPDATED,
                report=False,
            )
            print("Accept complete: UPDATED")

        if args.clean:
            print("Cleaning FW update metadata...")
            run_rc_command(
                port,
                args,
                "CLEAN",
                FW_UPDATE_OPCODE_CLEAN,
                SDK_IMAGE_ID_DEFAULT.to_bytes(4, "little"),
            )
            expect_state_with_report(
                "global state after CLEAN",
                run_get_state(port, args),
                FW_UPDATE_STATE_READY,
                report=False,
            )
            expect_state_with_report(
                "component state after CLEAN",
                run_get_component_state(port, args, SDK_IMAGE_ID_DEFAULT),
                FW_UPDATE_STATE_READY,
                report=False,
            )
            print("Clean complete: READY")

    print("PASS: post-reboot recovery completed")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Consolidated Zephyr Host API managed FW update tool over USB"
    )
    add_serial_args(parser, timeout_default=30.0, include_reconnect_timeout=True)
    parser.add_argument("--query", action="store_true", help="Query the current managed FW update state")
    parser.add_argument("--bin-file", help="Path to the SDK binary image file to flash")
    parser.add_argument(
        "--chunk-size",
        type=parse_u32,
        help="Optional write chunk size; must not exceed GET_INFO max write size",
    )
    parser.add_argument("--skip-ready-check", action="store_true", help="Skip initial READY-state checks")
    parser.add_argument(
        "--recover-ready",
        action="store_true",
        help="Retained for compatibility; flashing auto-recovers recoverable non-READY states",
    )
    parser.add_argument(
        "--install-auto-reset",
        action="store_true",
        help="Request auto-reset inside INSTALL instead of sending explicit REBOOT",
    )
    parser.add_argument(
        "--no-reboot",
        dest="reboot",
        action="store_false",
        help="Stop after INSTALL/STAGED and do not reboot",
    )
    parser.add_argument(
        "--no-trial-check",
        dest="expect_trial",
        action="store_false",
        help="Do not require TRIAL state after reboot",
    )
    parser.add_argument(
        "--accept",
        action="store_true",
        help="Send ACCEPT for the current TRIAL image and validate UPDATED",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Send CLEAN for the current FW update metadata and validate READY",
    )
    parser.add_argument("--verbose", action="store_true", help="Show packet TX/header/payload dumps")
    parser.set_defaults(reboot=True, expect_trial=True)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        if args.bin_file:
            if args.accept or args.clean:
                raise ValueError("use --accept and --clean as a separate post-reboot command")
            run_flash(args)
        elif args.query:
            run_query(args)
        elif args.accept or args.clean:
            run_post_reboot_recovery(args)
        else:
            raise ValueError("choose one action: --query, --bin-file <image>, or --accept/--clean")
        return 0
    except Exception as exc:  # pylint: disable=broad-except
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
