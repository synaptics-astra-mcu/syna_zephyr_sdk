# Image Generator Tool

A simplified tool for generating binary images from ELF files for the SL2610 platform.

## Quick Start

### Using with Make (Recommended)

```bash
# Build project and generate images in one command
make imagegen
```

### Manual Usage

```bash
# Navigate to project root and run
python tools/image_gen/image_generator.py
```

## Configuration

### JSON Configuration File

The tool uses `inp.json` located in the `tools/image_gen/` directory to specify which files to process:

```json
{
    "files": [
        {
            "filepath": "sl2610_cm52_fw/debug/sl2610_fw.elf",
            "load_address": "0xB8000000",
            "flash_offset": "0x3000000",
            "xspi_offset": "0x3C000",
            "Image_id": "0x00000012"
        },
        {
            "filepath": "sl2610_bootloader/release/sl2610_bootloader.elf",
            "load_address": "0xB8000000",
            "flash_offset": "0x3000000",
            "xspi_offset": "0x3C000",
            "Image_id": "0x00000012"
        }
    ]
}
```

### Configuration Parameters

- **`filepath`**: Relative path from `../../out/` to the ELF file
- **`load_address`**: Memory address where the image will be loaded
- **`flash_offset`**: Flash memory offset for storage
- **`xspi_offset`**: XSPI flash offset
- **`Image_id`**: Unique identifier for the image header

### File Path Resolution

The tool automatically constructs full file paths by combining:
- **Base path**: `../../out/` (relative to script location)
- **JSON filepath**: Path specified in the JSON configuration

**Example:**
- Base: `../../out/`
- JSON: `sl2610_cm52_fw/debug/sl2610_fw.elf`
- Final: `../../out/sl2610_cm52_fw/debug/sl2610_fw.elf`

## Adding New Files

To process additional ELF files, simply add new entries to the `files` array in `inp.json`:

```json
{
    "files": [
        {
            "filepath": "your_project/debug/your_file.elf",
            "load_address": "0xYourAddress",
            "flash_offset": "0xYourOffset",
            "xspi_offset": "0xYourXSPIOffset",
            "Image_id": "0xYourImageId"
        }
    ]
}
```

## Output Files

The tool generates two sets of output files in the `out/` directory:

### nexus_bin/
- Binary files without headers
- Used for direct flash programming

### nexus_loadable/
- Binary files with headers and metadata
- Used for bootloader and system loading
- Includes `output_flash_map.txt` with file mapping information

## Command Line Options

```bash
python image_generator.py [OPTIONS]

Options:
  --parse, -p              Parse existing binary files instead of generating new ones
  --config_file FILE       Use alternative JSON config file (default: inp.json)
  --include_sections LIST  Specify ELF sections to include
  -h, --help              Show help message
```

## Examples

```bash
# Generate images using default inp.json
python tools/image_gen/image_generator.py

# Use custom configuration file
python tools/image_gen/image_generator.py --config_file my_config.json

# Parse existing binary files
python tools/image_gen/image_generator.py --parse
```
