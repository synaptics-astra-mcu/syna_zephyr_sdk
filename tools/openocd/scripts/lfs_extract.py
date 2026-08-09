#!/usr/bin/env python3

from littlefs import LittleFS
from pathlib import Path
import argparse


def parse_args():
    parser = argparse.ArgumentParser(
        description="Extract all files from a dumped LittleFS partition image."
    )

    parser.add_argument(
        "-i", "--image",
        required=True,
        type=Path,
        help="Input LittleFS partition image, for example i2s_payload_lfs.bin",
    )

    parser.add_argument(
        "-o", "--output-dir",
        required=True,
        type=Path,
        help="Output directory for extracted files",
    )

    parser.add_argument(
        "-b", "--block-size",
        default=4096,
        type=lambda x: int(x, 0),
        help="LittleFS block size. Default: 4096. Use 0x1000 also allowed.",
    )

    return parser.parse_args()


def join_lfs_path(parent: str, name: str) -> str:
    if parent == "/":
        return "/" + name
    return parent.rstrip("/") + "/" + name


def is_directory(fs: LittleFS, path: str) -> bool:
    try:
        fs.listdir(path)
        return True
    except Exception:
        return False


def extract_tree(fs: LittleFS, lfs_dir: str, out_dir: Path) -> int:
    count = 0

    for name in fs.listdir(lfs_dir):
        lfs_path = join_lfs_path(lfs_dir, name)
        out_path = out_dir / lfs_path.lstrip("/")

        if is_directory(fs, lfs_path):
            out_path.mkdir(parents=True, exist_ok=True)
            count += extract_tree(fs, lfs_path, out_dir)
            continue

        out_path.parent.mkdir(parents=True, exist_ok=True)

        with fs.open(lfs_path, "rb") as f:
            data = f.read()

        out_path.write_bytes(data)
        print(f"Extracted {len(data):>10} bytes  {lfs_path} -> {out_path}")
        count += 1

    return count


def main():
    args = parse_args()

    image_data = args.image.read_bytes()

    if len(image_data) % args.block_size != 0:
        raise SystemExit(
            f"Image size {len(image_data)} is not divisible by "
            f"block size {args.block_size}"
        )

    block_count = len(image_data) // args.block_size

    fs = LittleFS(
        block_size=args.block_size,
        block_count=block_count,
        mount=False,
    )

    fs.context.buffer = bytearray(image_data)
    fs.mount()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Mounted LittleFS image: {args.image}")
    print(f"Block size : {args.block_size}")
    print(f"Block count: {block_count}")
    print(f"Output dir : {args.output_dir}")
    print()

    count = extract_tree(fs, "/", args.output_dir)

    print()
    print(f"Done. Extracted {count} file(s).")


if __name__ == "__main__":
    main()