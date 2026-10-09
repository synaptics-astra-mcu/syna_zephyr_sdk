# con_boot

`con_boot` is the M52-side boot and download driver for the SRW1500 WLAN and
Bluetooth subsystems. Include `include/con_boot.h` and link the `con_boot`
library with the application.

## CMake integration

Add the driver directory to an existing CMake project and link the resulting
`con_boot` library:

```cmake
add_subdirectory(path/to/con_boot)
target_include_directories(app PRIVATE path/to/con_boot/include)
target_link_libraries(app PRIVATE con_boot)
```

Subsystem sources follow the platform's existing feature options. Zephyr uses
`CONFIG_WIFI_SYNA` and `CONFIG_BT_SYNA`; FreeRTOS uses
`CONFIG_WIFI_ENABLED` and `CONFIG_BLUETOOTH_ENABLED`. An unset option leaves
that subsystem's implementation out of the library.

Enable secure boot independently for each selected subsystem by defining its
`CONFIG_WLAN_SECURE_BOOT` or `CONFIG_BT_SECURE_BOOT` flag before adding the driver:

```cmake
add_compile_definitions(CONFIG_WLAN_SECURE_BOOT)  # or CONFIG_BT_SECURE_BOOT
```

For a FreeRTOS build, select the adapter and provide the platform SDK include
directories before adding the driver:

```cmake
set(CON_BOOT_PLATFORM freertos)
set(CON_BOOT_PLATFORM_INCLUDE_DIRS path/to/freertos-and-syn765x-sdk/includes)
add_subdirectory(path/to/con_boot)
```

## Thread safety

The initial WLAN and Bluetooth bring-up calls are expected to be made sequentially.
Short interrupt-disabled critical sections protect read-modify-write operations on
registers shared by the two subsystems. Payload downloads do not hold a driver lock.
These APIs must not be called from an ISR.

## API usage

```c
#include "con_boot.h"

con_boot_file_info file = {
    .source = image_buffer,
    .source_size = image_size,
    .source_type = CON_BOOT_SOURCE_BUFFER,
    .dest_addr = mapped_m52_destination,
};

int32_t ret = con_boot_wlan_init();
if (ret == 0)
    ret = con_boot_wlan_download(&file, 1U, timeout_ms);
```

`dest_addr` must be the M52-visible mapped address, not the native subsystem
address. For flash sources, `source` is the flash offset. For filesystem
sources, `source` is a NUL-terminated path. `con_boot_flash_read()` reads from
the platform flash device using a device-relative offset.

## Zephyr devicetree regions

```c
uint32_t addr, size;
int32_t ret = con_boot_get_dt_region("sram_heap0", &addr, &size);
```

This exported Zephyr-only function currently supports `"sram_heap0"` only.
It fills the outputs using `DT_REG_ADDR(DT_NODELABEL(sram_heap0))` and
`DT_REG_SIZE(DT_NODELABEL(sram_heap0))`. It returns `0`, `-EINVAL` for NULL
or identical output pointers, or `-ENOTSUP` for other node labels. Outputs
are unchanged on failure. No address translation or window programming is
performed.

## Return codes

All functions return 0 on success or a negative `errno` value on failure:

- `-EINVAL`: invalid arguments, empty payloads, invalid source types, address
  overflow, or a zero timeout.
- `-EACCES`: download requested before the corresponding subsystem granted
  access.
- `-EALREADY`: download already completed for the corresponding subsystem.
- `-ETIMEDOUT`: subsystem access was not granted or the transfer exceeded its
  timeout.
- `-EIO`: window configuration failed, a transfer failed, or completion
  readback was invalid.
- `-ENOMEM`: the transfer staging buffer could not be allocated.
- `-ENODEV`: the platform flash device is unavailable.

Source readers may return platform-specific negative errors such as `-ENOENT`,
`-ENODATA`, or `-EIO`.
