import os
import struct
import subprocess
import sys
import json
import argparse
import hashlib
import re
import binascii
from dataclasses import dataclass

# Struct formats
segment_info_format = 'HHIII'  # type, needs_copy, offset, length, load_address
extras_format = "IIII"
header_format = 'I I I 16s 64s I I I I I I 512s'  # Custom header format
image_trailer_format = 'I I'  # Magic number and number of sections
#use_flash_start_offset = 0x400000     # Use 4MB Flash offset to read other images
use_flash_start_offset = 0x0     # Use 4MB Flash offset to read other images
gbl_offset = 0x0

def classify_memory_region(address):
    memory_map = [
        (0x10000000, 0x1003FFFF, "ITCM", 1),
        (0x30000000, 0x3003FFFF, "DTCM", 2),
        (0x18000000, 0x1FFFFFFF, "XIP", 3),
        (0xB0000000, 0xCFFFFFFF, "DDR", 4),
        (0x20000000, 0x2003FFFF, "DTCM_NS", 1),
        (0x00000000, 0x0003FFFF, "ITCM_NSC", 2),
        (0x08000000, 0x0FFFFFFF, "XIP_NS", 3),
        (0xA0000000, 0xAFFFFFFF, "DDR_NS", 4),
    ]
    for start, end, region, type in memory_map:
        if start <= address <= end:
            return type
    return 0  # Default if no match

def get_flash_offset(filename, flash_offset):
    global gbl_offset

    new_offset = int(flash_offset, 16)
    # Determine the file size
    file_size = os.path.getsize(filename)
    pt_adj = 0x100000 - (file_size % 0x100000)
    #print("[INFO] File {0} size: {1} bytes, padding adjustment: {2} bytes, new global offset: {3:#x}".format(os.path.basename(filename), file_size, pt_adj, gbl_offset))
    if new_offset != 0:
        gbl_offset = new_offset
    else:
        new_offset = gbl_offset
    gbl_offset += file_size+pt_adj
    return str(hex(new_offset))


code_data_flags = {'CODE', 'DATA', 'LOAD'}

def _parse_objdump_align(tok):
    # objdump prints alignment like: 2**3
    try:
        if tok.startswith("2**"):
            return 1 << int(tok.split("**", 1)[1], 10)
        return int(tok, 0)
    except Exception:
        return 1


def generate_binary_files(input_json_file, output_folder, trailer_flag, no_header):

    output_basename = os.path.basename(output_folder).strip()
    output_path = os.path.join("./out/", output_basename)
    if os.path.exists(output_path):
        #print("[WARN] Output folder already exists: {0}".format(output_path))
        os.system("rm -rf {0}/*".format(output_path))

    if not os.path.exists(output_path):
        os.makedirs(output_path)

    # Load JSON configuration
    with open(input_json_file, 'r') as f:
        config = json.load(f)

    # Create or overwrite the output_flash_map.txt file
    for file_config in config['files']:
        if file_config.get('filepath'):
            # Combine fixed base path ../../out with the filepath from JSON
            full_filepath = os.path.join(os.getcwd(), "", file_config.get('filepath'))
            filename = full_filepath
            in_filename = os.path.basename(full_filepath).strip()
        else:
            print("[ERROR] filepath not provided in JSON, skipping...")
            continue

        if not os.path.isfile(filename):
            print("[ERROR] File not found: {0}".format(in_filename))
            continue

        #print("[INFO] Processing file:: {0}".format(filename))
        load_address = file_config['load_address']
        _offset = file_config['flash_offset'] if 'flash_offset' in file_config else None
        xspi_offset = file_config['xspi_offset'] if 'xspi_offset' in file_config else None

        output_filename = "{0}_output.bin".format(os.path.splitext(in_filename)[0])
        output_filename = os.path.join(output_path, output_filename)
        segment_infos = []

        with open(output_filename, 'wb+') as output_file:
            # Header fields
            image_id = int(file_config["Image_id"], 16)
            image_format_version = 0
            length = 628
            iv = b'\x00' * 16
            checksum = b'\x00' * 64
            seg_id = 0x53454749
            version = 0
            production_image_flag = 0
            rfu = 0
            try:
                string_types = (basestring,)
            except NameError:
                string_types = (str,)
            destination_address = int(load_address, 16) if isinstance(load_address, string_types) else load_address
            sm_entry_point = 0
            signature = b'\x00' * 512

            if filename.endswith('.elf'):
                try:
                    readelf_out = subprocess.check_output(["arm-none-eabi-readelf", "-h", filename])
                    try:
                        readelf_out = readelf_out.decode('utf-8')
                    except Exception:
                        pass
                    for line in readelf_out.splitlines():
                        if "Entry point address:" in line:
                            sm_entry_point = int(line.split()[-1], 16)
                            break
                except subprocess.CalledProcessError as e:
                    print("[ERROR] readelf failed: {0}".format(e))
                    continue

            # Define the pattern to match any of the keywords
            pattern = r"(apbl|bootloader|bl|zephyr)"
            sm_entry_point = destination_address if re.search(pattern, filename) else sm_entry_point

            header = struct.pack(
                header_format,
                int(image_id) & 0xFFFFFFFF,
                int(image_format_version) & 0xFFFFFFFF,
                int(length) & 0xFFFFFFFF,
                iv,
                checksum,
                int(seg_id) & 0xFFFFFFFF,
                int(version) & 0xFFFFFFFF,
                int(production_image_flag) & 0xFFFFFFFF,
                int(rfu) & 0xFFFFFFFF,
                int(destination_address) & 0xFFFFFFFF,
                int(sm_entry_point) & 0xFFFFFFFF,
                signature
            )

            if no_header == False:
                output_file.write(header)

            current_offset = 0
            if filename.endswith('.elf') and trailer_flag == True:
                try:
                    objdump_out = subprocess.check_output(["arm-none-eabi-objdump", "-h", filename])
                    try:
                        objdump_out = objdump_out.decode('utf-8')
                    except Exception:
                        pass
                except subprocess.CalledProcessError as e:
                    print("[ERROR] objdump failed: {0}".format(e))
                    continue

                section_lines = objdump_out.splitlines()
                selected_sections = []
                i = 1
                while i < len(section_lines) - 1:
                    line = section_lines[i]
                    next_line = section_lines[i + 1]
                    parts = line.split()
                    i += 2
                    if len(parts) > 1 and parts[0].isdigit():
                        section_name = parts[1]
                        flags = next_line.split()
                        # Include all code and data sections
                        if not code_data_flags.intersection(flags):
                            continue

                        # objdump -h columns: Idx Name Size VMA LMA FileOff Algn
                        if len(parts) < 7:
                            continue

                        try:
                            vma = int(parts[3], 16)
                            lma = int(parts[4], 16)
                            align = _parse_objdump_align(parts[6])
                        except Exception:
                            continue

                        # print("[INFO] Processing section: {0}".format(section_name))
                        temp_bin = "temp_{0}.bin".format(section_name.replace('.', ''))
                        try:
                            subprocess.check_call([
                                "arm-none-eabi-objcopy",
                                "-O", "binary",
                                "--only-section={0}".format(section_name),
                                filename,
                                temp_bin
                            ])
                            with open(temp_bin, 'rb') as temp_file:
                                section_data = temp_file.read()
                            selected_sections.append({
                                "name": section_name,
                                "data": section_data,
                                "vma": vma,
                                "lma": lma,
                                "align": max(1, align),
                            })
                        except subprocess.CalledProcessError as e:
                            print("[ERROR] objcopy failed for section {0}: {1}".format(section_name, e))
                            continue
                        finally:
                            if os.path.exists(temp_bin):
                                os.remove(temp_bin)

                # Build body respecting true LMA gaps/alignment
                if selected_sections:
                    selected_sections.sort(key=lambda s: (s["lma"], s["name"]))
                    base_lma = selected_sections[0]["lma"]
                    body = bytearray()

                    for s in selected_sections:
                        target_off = s["lma"] - base_lma
                        if target_off < 0:
                            print("[ERROR] Invalid negative offset for section {0}".format(s["name"]))
                            continue

                        # Pad gap between sections
                        if len(body) < target_off:
                            gap_size = target_off - len(body)
                            #print(f"[INFO] Padding gap of 0x{gap_size:x} bytes before section {s['name']}")
                            body.extend(b"\x00" * gap_size)
                        elif len(body) > target_off:
                            print("[ERROR] Section overlap: {0} (cur=0x{1:x}, tgt=0x{2:x})".format(
                                s["name"], len(body), target_off
                            ))
                            continue

                        # Record offset BEFORE writing section data
                        sec_off = len(body)
                        body.extend(s["data"])
                        section_length = len(s["data"])

                        #print(f"[INFO] Section {s['name']}: length 0x{section_length:x}, body offset 0x{sec_off:x}, LMA 0x{s['lma']:x}, VMA 0x{s['vma']:x}")

                        need_copy = 1 #if classify_memory_region(vma) != 1 else 0
                        segment_infos.append((classify_memory_region(s["vma"]), need_copy,
                            sec_off,           # offset within body (after gaps)
                            section_length,
                            s["lma"]           # original LMA
                        ))

                    output_file.write(body)
                    current_offset = len(body)

            elif filename.endswith('.elf') and trailer_flag == False:
                print("[INFO] Skipping Trailer info, trailer_flag is False")
                try:
                    subprocess.check_call(["arm-none-eabi-objcopy", "-O", "binary", filename, filename.replace('.elf', '.bin')])
                    with open(filename.replace('.elf', '.bin'), 'rb') as temp_file:
                        file_data = temp_file.read()
                        output_file.write(file_data)
                        current_offset += len(file_data)
                except subprocess.CalledProcessError as e:
                    print("[ERROR] objcopy failed: {0}".format(e))
                    continue
            else:
                print("[INFO] Processing binary file: {0}".format(filename))
                with open(filename, 'rb') as file:
                    file_data = file.read()
                    output_file.write(file_data)
                    section_length = len(file_data)
                    sm_entry_point = destination_address
                    segment_infos.append((0, 1, 0, section_length, destination_address))
                    current_offset += section_length

            # Update and rewrite header with correct length
            length += current_offset
            # Calculate checksum for section

            # Write trailer
            magic = 0xDEADC0DE
            num_sections = len(segment_infos)
            if not no_header:
                output_file.seek(length)

            #if length % 4 != 0:
            #    print("[INFO] Padding output file to 4-byte alignment, current length: {0}".format(length))
            # Calculate trailer size
            trailer_size = struct.calcsize(image_trailer_format) + (len(segment_infos) * struct.calcsize(segment_info_format))

            # Calculate padding needed for 32-byte alignment of (body + trailer)
            body_trailer_size = current_offset + trailer_size
            padding_needed = (32 - (body_trailer_size % 32)) % 32

            if padding_needed > 0:
                #print("[INFO] Total size {0} Adding {1} bytes of padding for 32-byte alignment".format(body_trailer_size, padding_needed))
                output_file.write(b"\x00" * padding_needed)
                length += padding_needed

            if trailer_flag == True:
                # Write magic number first
                trailer_magic = struct.pack('<I', magic)
                output_file.write(trailer_magic)

                # Write all section info structures
                for seg in segment_infos:
                    output_file.write(struct.pack(segment_info_format, *seg))

                # Write number of sections AFTER all section infos
                num_sections_packed = struct.pack('<I', num_sections)
                output_file.write(num_sections_packed)

                # Calculate total bytes for selected code
                length += struct.calcsize(image_trailer_format) + (len(segment_infos)*struct.calcsize(segment_info_format))
            #print("[INFO] Total length of binary file: {0:#x} {0} bytes".format(length))
            if no_header == True:
                output_extras = "{0}_extras.bin".format(os.path.splitext(in_filename)[0])
                output_extras = os.path.join(output_path, output_extras)
                with open(output_extras, 'wb') as out_extras_file:
                    extr_pif = 0
                    extr_rfu = 0x12345678
                    extras = struct.pack( extras_format, extr_pif, extr_rfu,destination_address, sm_entry_point)
                    #Writing to extras_bin
                    out_extras_file.write(extras)
            else:
                #seek to header_end and apply sha-512
                output_file.seek(struct.calcsize(header_format))
                checksum = binascii.unhexlify(hashlib.sha512(output_file.read()).hexdigest())
                #update the header by moving to start
                header = struct.pack(
                    header_format,
                    int(image_id) & 0xFFFFFFFF,
                    int(image_format_version) & 0xFFFFFFFF,
                    int(length) & 0xFFFFFFFF,
                    iv,
                    checksum,
                    int(seg_id) & 0xFFFFFFFF,
                    int(version) & 0xFFFFFFFF,
                    int(production_image_flag) & 0xFFFFFFFF,
                    int(rfu) & 0xFFFFFFFF,
                    int(destination_address) & 0xFFFFFFFF,
                    int(sm_entry_point) & 0xFFFFFFFF,
                    signature
                )
                output_file.seek(0)
                output_file.write(header)


            #print(" image body size 0x{0:08X} bytes".format(length))
            #print("[SUCCESS] Generated binary file: {0}".format((output_filename)))
            if _offset is None:
                flash_offset_calculated = get_flash_offset(output_filename, "0x0")
            else :
                flash_offset_calculated = get_flash_offset(output_filename, _offset)

            file_size = hex(os.path.getsize(output_filename))

            out_info_file = os.path.join(output_path, 'output_flash_map.txt')
            with open(out_info_file, 'a') as flash_map_file:
                # Write to the flash map file
                flash_map_file.write("{0},{1},{2},{3}\n".format(os.path.basename(output_filename), flash_offset_calculated, xspi_offset, file_size))


def parse_binary_file(binary_file, flash_offset):
    #print("[INFO] File name {0} size: {1:#x} Flash offset: {2:#x}".format(binary_file, os.path.getsize(binary_file), flash_offset))
    with open(binary_file, 'rb') as f:
        # Read and unpack the header
        header_data = f.read(628)
        header = struct.unpack(header_format, header_data)
        print("[INFO] Header details: ")
        print("  image_id: {0:08X}".format(header[0]))
        print("  image_format_version: {0:08X}".format(header[1]))
        print("  length: {0:08X}".format(header[2]))
        print("  iv: {0}".format(header[3].encode('hex')))
        #print("  checksum: {0}".format(header[4].encode('hex')))
        print("  seg_id: {0:08X}".format(header[5]))
        print("  version: {0:08X}".format(header[6]))
        print("  production_image_flag: {0:08X}".format(header[7]))
        print("  rfu: {0:08X}".format(header[8]))
        print("  destination_address: {0:08X}".format(header[9]))
        print("  sm_entry_point: {0:08X}".format(header[10]))
        #print("  signature: {0}".format(header[11][:16].encode('hex')))

        # Read and unpack the trailer
        f.seek(-struct.calcsize('<I'), os.SEEK_END)
        trailer_data = f.read(struct.calcsize('<I'))
        trailer = struct.unpack('<I', trailer_data)
        # get total number of segments in the trailer
        num_segments = trailer[0]

        move_offset= (struct.calcsize(image_trailer_format)+(num_segments*struct.calcsize(segment_info_format)))
        print("[INFO] Trailer size: {0}".format(move_offset))
        f.seek(-move_offset, os.SEEK_CUR)
        trailer_magic_data = f.read(struct.calcsize('<I'))
        trailer_magic = struct.unpack('<I', trailer_magic_data)

        print("[INFO] Trailer details:")
        print("  num_sections: {0:08X}".format(trailer[0]))
        print("  magic: {0:08X}".format(trailer_magic[0]))

        if( trailer_magic[0] != 0xDEADC0DE):
            print("[ERROR] Invalid magic number in trailer: {0:08X}".format(trailer_magic[0]))
            sys.exit(1)

        # Read and unpack segment_info_t structures
        print("[INFO] Segment details:")
        for _ in range(num_segments):
            segment_data = f.read(struct.calcsize(segment_info_format))
            seg = struct.unpack(segment_info_format, segment_data)
            print("  segment_info_t(type={0:04X}, needs_copy={1:04X}, offset={2:08X}, length={3:08X}, load_address=0x{4:08X})".format(seg[0], seg[1], seg[2], seg[3], seg[4]))



if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='Generate binary files from ELF/binary inputs')

    # Get script directory
    script_dir = os.path.dirname(os.path.abspath(__file__))

    parser.add_argument('--parse', '-p', action='store_true',
                       help='Add on parse bin section')
    parser.add_argument('--config_file', default='inp.json',
                       help='Configuration file name (default: inp.json)')
    parser.add_argument('--include_sections', nargs='*', default=['ER_S_ITCM_START', '.ARM.exidx', 'XIP_START', '.gnu.sgstubs', 'ER_S_SRAM_START',
                          '.rom_section',  '.copy.table', '.zero.table', '.data'],
                       help='Sections to include')
    args = parser.parse_args()

    # Look for inp.json in the script directory
    input_json_file = os.path.join(script_dir, args.config_file)

    # Check if inp.json exists
    if not os.path.isfile(input_json_file):
        print("[ERROR] Configuration file not found: {0}".format(input_json_file))
        print("Please create an 'inp.json' file in the same directory as this script.")
        sys.exit(1)

    #print("[INFO] Using configuration file: {0}".format(input_json_file))

    if args.parse == False:
        generate_binary_files(input_json_file, "nexus_bin", trailer_flag=True, no_header=True)

        use_flash_start_offset = 0x0
        gbl_offset = 0x0
        generate_binary_files(input_json_file, "nexus_loadable", trailer_flag=True, no_header=False)
    else:
        output_path = "out/nexus_loadable"
        info_file = os.path.join(output_path, 'output_flash_map.txt')
        with open(info_file, 'r') as f:
            lines = f.readlines()
        for line in lines:
            parts = line.strip().split(',')
            if len(parts) < 2:
                print("[WARN] Skipping invalid line: {0}".format(line.strip()))
                continue
            in_filename, flash_offset, xspi_offset, file_size = parts
            binary_file = os.path.join(output_path, in_filename)
            flash_offset = int(flash_offset, 16)
            parse_binary_file(binary_file, flash_offset)
