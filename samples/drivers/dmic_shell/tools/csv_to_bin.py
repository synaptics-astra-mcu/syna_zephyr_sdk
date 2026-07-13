#!/usr/bin/env python3

import argparse
import csv
import struct


def hex_to_signed_sample_from_saleae(value: str, bits: int) -> int:
    """
    Convert Saleae I2S/PCM exported data to signed PCM.

    16-bit mode:
        Assumes 16-bit audio data is left-aligned in bits [31:16].
        Example:
            0x000000001C200000 -> 0x1C20 -> +7200

    24-bit mode:
        Assumes 24-bit audio data is left-aligned in bits [31:8].
        Example:
            0x0000000030168001 -> 0x301680
            0x00000000F4768001 -> 0xF47680 -> negative signed 24-bit
    """
    raw = int(value, 16)

    if bits == 16:
        shift = 16
        mask = 0xFFFF
        sign_bit = 0x8000
        sign_extend = 0x10000
    elif bits == 24:
        shift = 8
        mask = 0xFFFFFF
        sign_bit = 0x800000
        sign_extend = 0x1000000
    else:
        raise ValueError("Only 16-bit and 24-bit PCM are supported")

    sample = (raw >> shift) & mask

    if sample & sign_bit:
        sample -= sign_extend

    return sample


def write_signed_24_le(binfile, sample: int):
    """
    Write signed 24-bit little-endian PCM.

    Python's struct module has no native 24-bit integer type,
    so write 3 bytes manually.
    """
    if sample < 0:
        sample += 0x1000000

    binfile.write(bytes([
        sample & 0xFF,
        (sample >> 8) & 0xFF,
        (sample >> 16) & 0xFF,
    ]))


def get_row_value(row, possible_names):
    """
    Return the first matching CSV column value from possible_names.

    This makes the script tolerate slightly different Saleae CSV headers,
    such as 'channel' vs 'Channel'.
    """
    for name in possible_names:
        if name in row and row[name] is not None:
            return row[name].strip()

    return ""


def main():
    parser = argparse.ArgumentParser(
        description="Convert Saleae I2S/PCM CSV export to raw signed PCM .bin"
    )

    parser.add_argument("input_csv", help="Input Saleae CSV file")
    parser.add_argument("output_bin", help="Output raw PCM .bin file")
    parser.add_argument(
        "--channel",
        choices=["0", "1", "both"],
        default="both",
        help="Select channel: 0, 1, or both. Default: both",
    )
    parser.add_argument(
        "--bits",
        type=int,
        choices=[16, 24],
        default=16,
        help="PCM sample width: 16 or 24 bits. Default: 16",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print skipped rows and CSV column names",
    )

    args = parser.parse_args()

    sample_count = 0
    skipped_count = 0

    with open(args.input_csv, "r", newline="") as csvfile, open(
        args.output_bin, "wb"
    ) as binfile:
        reader = csv.DictReader(csvfile)

        if args.verbose:
            print("CSV columns:", reader.fieldnames)

        for line_num, row in enumerate(reader, start=2):
            channel_raw = get_row_value(row, ["channel", "Channel", "CHANNEL"])
            data_raw = get_row_value(row, ["data", "Data", "DATA"])

            if not channel_raw or not data_raw:
                skipped_count += 1
                if args.verbose:
                    print(f"Skipping line {line_num}: missing channel or data")
                continue

            try:
                channel = str(int(channel_raw, 16))
                sample = hex_to_signed_sample_from_saleae(data_raw, args.bits)
            except ValueError:
                skipped_count += 1
                if args.verbose:
                    print(
                        f"Skipping line {line_num}: invalid hex "
                        f"channel={channel_raw!r}, data={data_raw!r}"
                    )
                continue

            if args.channel != "both" and channel != args.channel:
                continue

            if args.bits == 16:
                binfile.write(struct.pack("<h", sample))
            elif args.bits == 24:
                write_signed_24_le(binfile, sample)

            sample_count += 1

    print(f"Wrote {sample_count} {args.bits}-bit samples to {args.output_bin}")

    if skipped_count:
        print(f"Skipped {skipped_count} invalid/blank rows")


if __name__ == "__main__":
    main()
