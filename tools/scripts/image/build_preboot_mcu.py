#!/usr/bin/env python3
import os
import sys
import shutil
import struct
import subprocess
from pathlib import Path
import gzip
import argparse

def die(msg: str) -> None:
    print(f"Error: {msg}", file=sys.stderr)
    sys.exit(1)


def require_file(path: Path) -> None:
    if not path.is_file():
        die(f"Missing required file: {path}")


def run_image_generator(mcuboot_topdir: Path) -> None:
    """Run the image generator script before building preboot MCU."""
    image_gen_script = mcuboot_topdir / "tools/image_gen/image_generator.py"

    if not image_gen_script.is_file():
        die(f"Image generator script not found: {image_gen_script}")
    try:
        subprocess.run([sys.executable, str(image_gen_script)], check=True)
    except subprocess.CalledProcessError as e:
        die(f"Image generator failed with exit code {e.returncode}")


def genx_secure_image( mcuboot_topdir: Path, v_image_type: str, in_key_type: str, in_extras: Path, in_length: str, f_input: Path, f_output: Path, mcu_soc: str) -> None:
    require_file(f_input)
    require_file(in_extras)

    exec_cmd = "gen_x_secure_image"
    security_tools_path = mcuboot_topdir / "tools/scripts/image/bin"
    security_keys_path = mcuboot_topdir / f"tools/signingKeys/{mcu_soc}/security"

    syna_chip_name = os.environ.get("syna_chip_name") or ""
    syna_chip_rev = os.environ.get("syna_chip_rev") or ""

    # Always include chip args to satisfy tool assertions (even if empty)
    args = [
        str(security_tools_path / exec_cmd),
        f"--chip-name={syna_chip_name}",
        f"--chip-rev={syna_chip_rev}",
        f"--img_type={v_image_type}",
        f"--key_type={in_key_type}",
        f"--length={in_length}",
        f"--extras={in_extras}",
        f"--workdir-security-tools={security_tools_path}",
        f"--workdir-security-keys={security_keys_path}",
        "--tool-version=genx_v3",
        f"--in_payload={f_input}",
        f"--out_store={f_output}",
    ]

    subprocess.run(args, check=True)


def concat_bin(out_path: Path, inputs: list[Path]) -> None:
    for p in inputs:
        require_file(p)
    with out_path.open("wb") as w:
        for p in inputs:
            with p.open("rb") as r:
                shutil.copyfileobj(r, w)


def compress_gzip(src: Path, dst: Path, compresslevel: int = 1) -> None:
    require_file(src)
    # Use system gzip for maximum compatibility (mirrors bash `gzip -1`)
    with dst.open("wb") as out_f:
        subprocess.run(["gzip", f"-{compresslevel}", "-c", str(src)], check=True, stdout=out_f)


# ========== XSPI BOOTIMG GENERATION FUNCTIONS ==========

def gen_4le_bytes(hex_value: int) -> bytes:
    """Convert integer to 4-byte little-endian format."""
    return struct.pack('<I', hex_value)


def write_4le_bytes(hex_value: int, output_path: Path) -> None:
    """Write 4-byte little-endian integer to file."""
    with output_path.open('wb') as f:
        f.write(gen_4le_bytes(hex_value))


def get_pt_name(line_num: int, ptfile: Path) -> str:
    """Get partition name from line number in partition table file."""
    with ptfile.open('r') as f:
        lines = f.readlines()
    if line_num <= 0 or line_num > len(lines):
        return ""
    # Skip comment lines
    line = lines[line_num - 1].strip()
    if line.startswith('#'):
        return ""
    parts = line.split()
    if len(parts) >= 2:
        return parts[1].strip()
    return ""


def to_bytes(size_str: str) -> int:
    """Convert size string (with K/M/G suffix) to bytes."""
    size_str = size_str.strip()
    unit = size_str[-1]

    if unit in 'KMG':
        num = int(size_str[:-1])
    else:
        return int(size_str)

    multipliers = {'K': 1024, 'M': 1024 * 1024, 'G': 1024 * 1024 * 1024}
    return num * multipliers.get(unit, 1)


def get_pt_size(line_num: int, ptfile: Path) -> int:
    """Get partition size from line number in partition table file."""
    with ptfile.open('r') as f:
        lines = f.readlines()
    if line_num <= 0 or line_num > len(lines):
        return 0
    line = lines[line_num - 1].strip()
    if line.startswith('#'):
        return 0
    parts = line.split()
    if len(parts) >= 1:
        return to_bytes(parts[0].strip())
    return 0


def get_pt_id(name: str) -> int:
    """Get partition ID based on partition name."""
    pt_id_map = {
        "sysmgr": 0x00000012, "sysmgr_a": 0x00000012, "sysmgr_b": 0x00000012,
        "tzk": 0x00020014, "tzk_a": 0x00020014, "tzk_b": 0x00020014,
        "bl": 0x00020017, "bl_a": 0x00020017, "bl_b": 0x00020017, "uboot": 0x00020017,
        "boot": 0x00020018, "boot_a": 0x00020018, "boot_b": 0x00020018,
        "key": 0x00020050, "key_a": 0x00020050, "key_b": 0x00020050,
        "firmware": 0x00020051, "firmware_a": 0x00020051, "firmware_b": 0x00020051,
        "rootfs": 0x00020052, "rootfs_a": 0x00020052, "rootfs_b": 0x00020052,
        "fastlogo": 0x00020053, "fastlogo_a": 0x00020053, "fastlogo_b": 0x00020053,
        "factory_setting": 0x00020070,
        "misc": 0x00020071,
        "userdata": 0x00020072,
    }
    return pt_id_map.get(name, 0xFFFFFFFF)


def gen_boot_entry(pt_offset: int, pt_id: int, pt_size: int, flash_offset: int, output_path: Path) -> None:
    """Generate a boot entry (16 bytes) with partition info."""
    pt_size_kb = pt_size // 1024

    entry = struct.pack('<IIII',
                        pt_offset & 0xFFFFFFFF,
                        pt_id & 0xFFFFFFFF,
                        pt_size_kb & 0xFFFFFFFF,
                        flash_offset & 0xFFFFFFFF)

    with output_path.open('wb') as f:
        f.write(entry)


def gen_boot_pt(pt_offset: int, flash_offset: int, output_path: Path) -> None:
    """Generate boot partition table with multiple entries."""
    tmp_entry = Path("/tmp/entry")

    # K0_SYNA
    gen_boot_entry(pt_offset, 0x00000001, 4096, flash_offset, output_path)

    # K0_OEM through SPK
    entries = [
        (pt_offset + 16, 0x00000002, 4096, flash_offset + 4096),      # K0_OEM
        (pt_offset + 32, 0x00000003, 4096, flash_offset + 8192),      # K0_3rd
        (pt_offset + 48, 0x00000004, 4096, flash_offset + 12288),     # K1_A
        (pt_offset + 64, 0x00000005, 4096, flash_offset + 16384),     # K1_B
        (pt_offset + 80, 0x00000006, 4096, flash_offset + 20480),     # K1_C
        (pt_offset + 96, 0x00000007, 4096, flash_offset + 24576),     # K1_D
        (pt_offset + 112, 0x00000010, 0x20000, flash_offset + 28672), # SPK
    ]

    for offset, id_val, size, flash_off in entries:
        gen_boot_entry(offset, id_val, size, flash_off, tmp_entry)
        with tmp_entry.open('rb') as f:
            with output_path.open('ab') as out:
                out.write(f.read())


def truncate_file(path: Path, size: int) -> None:
    """Truncate file to specified size."""
    with path.open('r+b') as f:
        f.truncate(size)


def concat_files(output_path: Path, input_paths: list[Path]) -> None:
    """Concatenate multiple files into one."""
    for p in input_paths:
        require_file(p)

    with output_path.open('wb') as out:
        for p in input_paths:
            with p.open('rb') as inp:
                shutil.copyfileobj(inp, out)

def get_partition_offsets(ptfile: Path) -> dict:
    """Read partition table and return partition names with their offsets."""
    partitions = {}
    offset = 0

    with ptfile.open('r') as f:
        lines = f.readlines()

    for i, line in enumerate(lines, start=1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        parts = line.split()
        if len(parts) >= 2:
            size_str = parts[0]
            name = parts[1]
            try:
                size = to_bytes(size_str)
                partitions[name] = {'offset': offset, 'size': size}
                offset += size
            except (ValueError, IndexError) as e:
                print(f"Warning: Failed to parse partition line {i}: {line}")
                continue

    return partitions


def build_spi_boot_image(ptfile: Path, outdir_subimg_intermediate: Path, output_path: Path, a55_image: Path | None = None) -> None:
    """
    Build SPI boot image by placing bootinfo, preboot, sysmgr, and optional a55_zephyr
    at proper offsets according to the partition table (xspi.pt).
    """

    # Get partition offsets from xspi.pt
    partitions = get_partition_offsets(ptfile)

    # Optional external image for SPI-only packing
    env_a55 = os.environ.get("A55_ZEPHYR_BIN", "").strip()
    a55_zephyr_bin = a55_image if a55_image else (Path(env_a55) if env_a55 else None)

    # Define the images to include
    images = {
        'bootinfo': outdir_subimg_intermediate / 'bootinfo.subimg',
        'preboot': outdir_subimg_intermediate / 'preboot.subimg',
        'sysmgr': outdir_subimg_intermediate / 'sysmgr.subimg',
    }

    # Add optional a55_zephyr only for SPI boot image preparation
    if a55_zephyr_bin:
        images['a55_zephyr'] = a55_zephyr_bin

    # Calculate total size needed
    total_size = 0
    image_offsets = {}

    for img_name, img_file in images.items():
        if img_name in partitions:
            offset = partitions[img_name]['offset']
            size = partitions[img_name]['size']
            image_offsets[img_name] = (offset, size, img_file)
            total_size = max(total_size, offset + size)
        elif img_name == 'a55_zephyr' and a55_zephyr_bin:
            print("  Warning: a55_zephyr image provided but no 'a55_zephyr' partition in xspi.pt; skipping")

    if total_size == 0:
        die("No valid partition offsets found in partition table")

    # Create output image with padding
    with output_path.open('wb') as out:
        out.write(b'\x00' * total_size)

    # Write each image at its proper offset
    with output_path.open('r+b') as out:
        for img_name, (offset, max_size, img_file) in image_offsets.items():
            if not img_file.exists():
                print(f"  Warning: {img_name} image not found at {img_file}, skipping")
                continue

            with img_file.open('rb') as inp:
                img_data = inp.read()

            # Keep partition aligned packing: image must fit partition
            if len(img_data) > max_size:
                die(f"{img_name} image size ({len(img_data)}) exceeds partition size ({max_size}) in xspi.pt")

            out.seek(offset)
            out.write(img_data)
            print(f"  {img_name}: offset=0x{offset:x}, size={len(img_data)} bytes, part_size={max_size} bytes")

    print(f"SPI boot image {output_path} file {total_size} bytes")


def generate_bootinfo(outdir_subimg_intermediate: Path, product_dir: Path) -> None:
    """Generate bootinfo.subimg from partition table and configuration."""

    # Get configuration from environment
    config_flash_attr = os.environ.get('CONFIG_FLASH_ATTR', 'W25Q128JW-1BIT')
    config_preboot_bootflow_ab = os.environ.get('CONFIG_PREBOOT_BOOTFLOW_AB', 'n')
    config_xspi_total_size = int(os.environ.get('CONFIG_XSPI_TOTAL_SIZE', '0x4000000'), 0)
    config_uboot_spiuboot = os.environ.get('CONFIG_UBOOT_SPIUBOOT', 'y')

    intermediate_dir = outdir_subimg_intermediate / 'bootinfo'

    # Input files
    flash_attr_file = product_dir / 'flash_attr' / config_flash_attr
    xspi_pt_file = product_dir / 'xspi.pt'

    require_file(flash_attr_file)
    require_file(xspi_pt_file)

    # Create intermediate directory
    intermediate_dir.mkdir(parents=True, exist_ok=True)

    # Copy flash attributes
    shutil.copy2(flash_attr_file, intermediate_dir / 'flash_attr')

    # Append MCU part B offset
    if config_preboot_bootflow_ab == 'y':
        write_4le_bytes(0x000040A0, intermediate_dir / 'mcupartBoffset')
    else:
        write_4le_bytes(0xFFFFFFFF, intermediate_dir / 'mcupartBoffset')

    with (intermediate_dir / 'flash_attr').open('ab') as out:
        with (intermediate_dir / 'mcupartBoffset').open('rb') as inp:
            shutil.copyfileobj(inp, out)

    # Generate boot partition indicators
    write_4le_bytes(0xFFFF0000, intermediate_dir / 'bp_indicate_spk_vidx0')
    write_4le_bytes(0xFFFFFFFF, intermediate_dir / 'bp_indicate_spk_vidx1')
    concat_files(intermediate_dir / 'bp_indicate_spk',
                [intermediate_dir / 'bp_indicate_spk_vidx0',
                 intermediate_dir / 'bp_indicate_spk_vidx1'])
    truncate_file(intermediate_dir / 'bp_indicate_spk', 4096)

    write_4le_bytes(0xFFFF0000, intermediate_dir / 'bp_indicate_m52_vidx0')
    write_4le_bytes(0xFFFFFFFF, intermediate_dir / 'bp_indicate_m52_vidx1')
    concat_files(intermediate_dir / 'bp_indicate_m52',
                [intermediate_dir / 'bp_indicate_m52_vidx0',
                 intermediate_dir / 'bp_indicate_m52_vidx1'])
    truncate_file(intermediate_dir / 'bp_indicate_m52', 4096)

    # Verify bootinfo is the mandatory first partition
    pt_name = get_pt_name(2, xspi_pt_file)
    if pt_name != 'bootinfo':
        die('bootinfo is a must for xSPI image')

    # Count lines in partition table
    with xspi_pt_file.open('r') as f:
        lines = len(f.readlines())

    # Initialize variables
    pt_offset = 0x00000140 if config_preboot_bootflow_ab == 'y' else 0x000000A0
    entry_num = 20 if config_preboot_bootflow_ab == 'y' else 10
    flash_offset = 0

    # Append AP part A offset to flash_attr
    ap_partA_offset = pt_offset + 0x4000
    write_4le_bytes(ap_partA_offset, intermediate_dir / 'appartAoffset')
    with (intermediate_dir / 'flash_attr').open('ab') as out:
        with (intermediate_dir / 'appartAoffset').open('rb') as inp:
            shutil.copyfileobj(inp, out)

    # Process A slot partitions
    pt_a_file = intermediate_dir / 'pt_a_normal'
    if pt_a_file.exists():
        pt_a_file.unlink()

    for i in range(2, lines + 1):
        pt_name = get_pt_name(i, xspi_pt_file)
        pt_size = get_pt_size(i, xspi_pt_file)

        if pt_size == 0:
            pt_size = config_xspi_total_size - flash_offset

        if pt_name in ['preboot', 'preboot_a']:
            gen_boot_pt(0x0, flash_offset, intermediate_dir / 'pt_a')
            apbl_size = pt_size - 0x27000
            apbl_offset = flash_offset + 0x27000
            gen_boot_entry(0x80, 0x00000011, apbl_size, apbl_offset,
                          intermediate_dir / 'pt_a_apbl_entry')
        elif pt_name in ['sysmgr', 'sysmgr_a']:
            gen_boot_entry(0x90, 0x00000012, pt_size, flash_offset,
                          intermediate_dir / 'pt_a_sysmgr_entry')
        elif pt_name in ['tzk', 'tzk_a', 'bl', 'bl_a', 'uboot']:
            pt_id = get_pt_id(pt_name)
            gen_boot_entry(pt_offset, pt_id, pt_size, flash_offset,
                          intermediate_dir / pt_name)
            with (intermediate_dir / 'pt_a_normal').open('ab') as out:
                with (intermediate_dir / pt_name).open('rb') as inp:
                    shutil.copyfileobj(inp, out)
            pt_offset += 16
            entry_num += 1

        flash_offset += pt_size

    # Process B slot if enabled
    if config_preboot_bootflow_ab == 'y':
        flash_offset = 0

        ap_partB_offset = pt_offset + 0x4000
        write_4le_bytes(ap_partB_offset, intermediate_dir / 'appartBoffset')
        with (intermediate_dir / 'flash_attr').open('ab') as out:
            with (intermediate_dir / 'appartBoffset').open('rb') as inp:
                shutil.copyfileobj(inp, out)

        pt_b_file = intermediate_dir / 'pt_b_normal'
        if pt_b_file.exists():
            pt_b_file.unlink()

        for i in range(2, lines + 1):
            pt_name = get_pt_name(i, xspi_pt_file)
            pt_size = get_pt_size(i, xspi_pt_file)

            if pt_size == 0:
                pt_size = config_xspi_total_size - flash_offset

            if pt_name == 'preboot_b':
                gen_boot_pt(0xA0, flash_offset, intermediate_dir / 'pt_b')
                apbl_size = pt_size - 0x27000
                apbl_offset = flash_offset + 0x27000
                gen_boot_entry(0x120, 0x00000011, apbl_size, apbl_offset,
                              intermediate_dir / 'pt_b_apbl_entry')
            elif pt_name == 'sysmgr_b':
                gen_boot_entry(0x130, 0x00000012, pt_size, flash_offset,
                              intermediate_dir / 'pt_b_sysmgr_entry')
            elif pt_name in ['tzk_b', 'bl_b']:
                pt_id = get_pt_id(pt_name)
                gen_boot_entry(pt_offset, pt_id, pt_size, flash_offset,
                              intermediate_dir / pt_name)
                with (intermediate_dir / 'pt_b_normal').open('ab') as out:
                    with (intermediate_dir / pt_name).open('rb') as inp:
                        shutil.copyfileobj(inp, out)
                pt_offset += 16
                entry_num += 1

            flash_offset += pt_size
    else:
        write_4le_bytes(0xFFFFFFFF, intermediate_dir / 'appartBoffset')
        with (intermediate_dir / 'flash_attr').open('ab') as out:
            with (intermediate_dir / 'appartBoffset').open('rb') as inp:
                shutil.copyfileobj(inp, out)

    # Process common partitions
    flash_offset = 0
    pt_common_file = intermediate_dir / 'pt_common'
    if pt_common_file.exists():
        pt_common_file.unlink()

    for i in range(2, lines + 1):
        pt_name = get_pt_name(i, xspi_pt_file)
        pt_size = get_pt_size(i, xspi_pt_file)

        if pt_size == 0:
            pt_size = config_xspi_total_size - flash_offset

        if pt_name in ['factory_setting', 'misc', 'userdata']:
            pt_id = get_pt_id(pt_name)
            gen_boot_entry(pt_offset, pt_id, pt_size, flash_offset,
                          intermediate_dir / pt_name)
            with (intermediate_dir / 'pt_common').open('ab') as out:
                with (intermediate_dir / pt_name).open('rb') as inp:
                    shutil.copyfileobj(inp, out)
            pt_offset += 16
            entry_num += 1

        flash_offset += pt_size

    # Append number of entries to flash_attr
    write_4le_bytes(entry_num, intermediate_dir / 'entry_num')
    with (intermediate_dir / 'flash_attr').open('ab') as out:
        with (intermediate_dir / 'entry_num').open('rb') as inp:
            shutil.copyfileobj(inp, out)

    # Calculate and append CRC
    # run_crc_tool(intermediate_dir / 'flash_attr')

    # Truncate flash_attr to 4096 bytes
    truncate_file(intermediate_dir / 'flash_attr', 4096)

    # Build bootinfo.bin
    bootinfo_parts = [
        intermediate_dir / 'flash_attr',
        intermediate_dir / 'flash_attr',
        intermediate_dir / 'bp_indicate_spk',
        intermediate_dir / 'bp_indicate_m52',
    ]
    concat_files(intermediate_dir / 'bootinfo.bin', bootinfo_parts)

    # Append pt_a entries
    with (intermediate_dir / 'bootinfo.bin').open('ab') as out:
        with (intermediate_dir / 'pt_a').open('rb') as inp:
            shutil.copyfileobj(inp, out)
        with (intermediate_dir / 'pt_a_apbl_entry').open('rb') as inp:
            shutil.copyfileobj(inp, out)
        # sysmgr is optional
        if (intermediate_dir / 'pt_a_sysmgr_entry').exists():
            with (intermediate_dir / 'pt_a_sysmgr_entry').open('rb') as inp:
                shutil.copyfileobj(inp, out)

    # Append pt_b entries if enabled
    if config_preboot_bootflow_ab == 'y':
        with (intermediate_dir / 'bootinfo.bin').open('ab') as out:
            with (intermediate_dir / 'pt_b').open('rb') as inp:
                shutil.copyfileobj(inp, out)
            with (intermediate_dir / 'pt_b_apbl_entry').open('rb') as inp:
                shutil.copyfileobj(inp, out)
            # sysmgr_b is optional
            if (intermediate_dir / 'pt_b_sysmgr_entry').exists():
                with (intermediate_dir / 'pt_b_sysmgr_entry').open('rb') as inp:
                    shutil.copyfileobj(inp, out)

    # Append pt_a_normal
    with (intermediate_dir / 'bootinfo.bin').open('ab') as out:
        if (intermediate_dir / 'pt_a_normal').exists():
            with (intermediate_dir / 'pt_a_normal').open('rb') as inp:
                shutil.copyfileobj(inp, out)

    # Append pt_b_normal if enabled
    if config_preboot_bootflow_ab == 'y':
        with (intermediate_dir / 'bootinfo.bin').open('ab') as out:
            if (intermediate_dir / 'pt_b_normal').exists():
                with (intermediate_dir / 'pt_b_normal').open('rb') as inp:
                    shutil.copyfileobj(inp, out)

    # Append pt_common if exists
    with (intermediate_dir / 'bootinfo.bin').open('ab') as out:
        if (intermediate_dir / 'pt_common').exists():
            with (intermediate_dir / 'pt_common').open('rb') as inp:
                shutil.copyfileobj(inp, out)

    # Truncate for UBOOT_SPIUBOOT config
    if config_uboot_spiuboot == 'y':
        truncate_file(intermediate_dir / 'bootinfo.bin', 20480)

    # Copy to final output location
    output_subimg = outdir_subimg_intermediate / 'bootinfo.subimg'
    shutil.copy2(intermediate_dir / 'bootinfo.bin', output_subimg)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build MCU artifacts")
    parser.add_argument(
        "--outdir",
        dest="outdir",
        type=Path,
        default=None,
        help="Output directory for generated images (default: None)",
    )
    parser.add_argument(
        "--soc",
        dest="soc",
        type=str,
        default="sl2610",
        help="Target MCU SoC (default: sl2610)",
    )
    parser.add_argument(
        "--m52_image",
        dest="m52_image",
        type=Path,
        default=None,
        help="M52 zephyr image file to pack into boot image (m52_zephyr partition)",
    )
    parser.add_argument(
        "--a55_image",
        dest="a55_image",
        type=Path,
        default=None,
        help="Optional a55 zephyr image file to pack into SPI boot image (a55_zephyr partition)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    mcu_soc = args.soc.strip().lower()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    mcuboot_topdir = Path(os.path.join(script_dir, "../../.."))

    # Run image generator first
    run_image_generator(mcuboot_topdir)

    boot_security_prebuilts_dir = mcuboot_topdir / f"tools/signingKeys/{mcu_soc}/SPK"
    boot_security_keys_dir = boot_security_prebuilts_dir / "key_stores"

    preboot_outdir_build_release = args.outdir / "image"
    outdir_subimg_intermediate = preboot_outdir_build_release / "intermediate"
    outdir_product_release_emmc = preboot_outdir_build_release / "eMMCimg"
    outdir_product_release_usb_boot = preboot_outdir_build_release / "usb_boot"
    outdir_product_release_spi_boot = preboot_outdir_build_release / "spi"

    f_BL =  Path(os.path.join(os.getcwd(), "out", "nexus_bin", "zephyr_output.bin"))
    f_BL_EXTRA = Path(os.path.join(os.getcwd(), "out", "nexus_bin", "zephyr_extras.bin"))
    f_K0_SYNA_store = boot_security_keys_dir / "K0_SYNA_store_4k.bin"
    f_K0_OEM_store = boot_security_keys_dir / "K0_OEM_store_4k.bin"
    f_K0_3rd_store = boot_security_keys_dir / "K0_3RD_store_4k.bin"
    f_K1_A_store = boot_security_keys_dir / "K1_A_store_4k.bin"
    f_K1_B_store = boot_security_keys_dir / "K1_B_store_4k.bin"
    f_K1_C_store = boot_security_keys_dir / "K1_C_store_4k.bin"
    f_K1_D_store = boot_security_keys_dir / "K1_D_store_4k.bin"
    f_SPK = boot_security_prebuilts_dir / "spk.bin"

    for p in [
        f_K0_SYNA_store, f_K0_OEM_store, f_K0_3rd_store,
        f_K1_A_store, f_K1_B_store, f_K1_C_store, f_K1_D_store,
        f_SPK, f_BL, f_BL_EXTRA
    ]:
        require_file(p)

    for d in [
        preboot_outdir_build_release,
        outdir_subimg_intermediate,
        outdir_product_release_emmc,
        outdir_product_release_usb_boot,
        outdir_product_release_spi_boot,
    ]:
        if d.exists():
            shutil.rmtree(d)
    for d in [
        preboot_outdir_build_release,
        outdir_subimg_intermediate,
        outdir_product_release_emmc,
        outdir_product_release_usb_boot,
        outdir_product_release_spi_boot,
    ]:
        d.mkdir(parents=True, exist_ok=True)

    shutil.copy2(f_K0_SYNA_store, outdir_subimg_intermediate / "K0_SYNA_store_4k.bin")
    shutil.copy2(f_K0_OEM_store, outdir_subimg_intermediate / "K0_OEM_store_4k.bin")
    shutil.copy2(f_K0_3rd_store, outdir_subimg_intermediate / "K0_3RD_store_4k.bin")
    shutil.copy2(f_K1_A_store, outdir_subimg_intermediate / "K1_A_store_4k.bin")
    shutil.copy2(f_K1_B_store, outdir_subimg_intermediate / "K1_B_store_4k.bin")
    shutil.copy2(f_K1_C_store, outdir_subimg_intermediate / "K1_C_store_4k.bin")
    shutil.copy2(f_K1_D_store, outdir_subimg_intermediate / "K1_D_store_4k.bin")

    spk_out = outdir_subimg_intermediate / "spk.bin"
    shutil.copy2(f_SPK, spk_out)
    with spk_out.open("r+b") as f:
        f.truncate(131072)

    genx_secure_image(
        mcuboot_topdir,
        "BL",
        "ree",
        f_BL_EXTRA,
        "0x0",
        f_BL,
        outdir_subimg_intermediate / "bl_en.bin",
        mcu_soc,
    )

    concat_bin(
        outdir_subimg_intermediate / "preboot.subimg",
        [
            outdir_subimg_intermediate / "K0_SYNA_store_4k.bin",
            outdir_subimg_intermediate / "K0_OEM_store_4k.bin",
            outdir_subimg_intermediate / "K0_3RD_store_4k.bin",
            outdir_subimg_intermediate / "K1_A_store_4k.bin",
            outdir_subimg_intermediate / "K1_B_store_4k.bin",
            outdir_subimg_intermediate / "K1_C_store_4k.bin",
            outdir_subimg_intermediate / "K1_D_store_4k.bin",
            outdir_subimg_intermediate / "spk.bin",
            outdir_subimg_intermediate / "bl_en.bin",
        ],
    )

    # Generate bootinfo.subimg for xSPI
    product_dir = mcuboot_topdir / 'tools/scripts/image/xspi'
    if product_dir.exists():
        try:
            generate_bootinfo(outdir_subimg_intermediate, product_dir)
        except Exception as e:
            print(f"Warning: Could not generate bootinfo: {e}")

    # Generate SPI boot image (bootinfo + preboot + sysmgr + optional a55_zephyr)
    xspi_pt_file = product_dir / 'xspi.pt'
    spi_boot_output = outdir_product_release_spi_boot / 'spi_boot.bin'
    if xspi_pt_file.exists():
        try:
            build_spi_boot_image(
                xspi_pt_file,
                outdir_subimg_intermediate,
                spi_boot_output,
                a55_image=args.a55_image,
            )
        except Exception as e:
            print(f"Warning: Could not generate SPI boot image: {e}")

    concat_bin(
        out_path=outdir_product_release_usb_boot / "key.bin",
        inputs=[
            outdir_subimg_intermediate / "K0_SYNA_store_4k.bin",
            outdir_subimg_intermediate / "K0_OEM_store_4k.bin",
            outdir_subimg_intermediate / "K0_3RD_store_4k.bin",
            outdir_subimg_intermediate / "K1_A_store_4k.bin",
            outdir_subimg_intermediate / "K1_B_store_4k.bin",
            outdir_subimg_intermediate / "K1_C_store_4k.bin",
            outdir_subimg_intermediate / "K1_D_store_4k.bin",
        ],
    )

    shutil.copy2(outdir_subimg_intermediate / "spk.bin", outdir_product_release_usb_boot / "spk.bin")
    shutil.copy2(outdir_subimg_intermediate / "bl_en.bin", outdir_product_release_usb_boot / "m52bl.bin")
    print(f"USB boot files {os.path.realpath(outdir_product_release_usb_boot)}")

    compress_gzip(outdir_subimg_intermediate / "preboot.subimg", outdir_product_release_emmc / "preboot.subimg.gz", compresslevel=1)
    print(f"eMMC boot image {os.path.realpath(outdir_product_release_emmc / 'preboot.subimg.gz')}")

    print(f"{mcu_soc.upper()} image generation done...")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
            die(f"Command failed with exit code {e.returncode}: {e}")
    except Exception as e:
        die(str(e))
