/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmsis_compiler.h>

#include "con_boot_srw1500_regs.h"
#include "con_boot.h"
#include "con_boot_platform.h"
#include "con_boot_time.h"
#include "con_boot_trace.h"
#include "con_boot_windows.h"
#include "con_boot_transfer.h"

#define ARR_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BT_ACCESS_STATUS_ADDR CON_BOOT_BT_ACCESS_STATUS_ADDR
#define BT_DOWNLOAD_STATUS_ADDR (BT_ACCESS_STATUS_ADDR + 0x4U)
#define BT_ENABLE_BIT (1U << 1)
#define BT_RETENTION_BIT (1U << 11)
#define BT_BOOT_VALID (1U << 0)
#define BT_RESET_BIT (1U << 5)
#define BT_WAKE_REQ_BIT (1U << 7)
#define BT_RESET_DELAY 20000U
#define ACCESS_GRANTED 0xAAAAAAAAU
#define DOWNLOAD_COMPLETE 0xFFFFFFFFU
#define ACCESS_TIMEOUT_MS 20000U
#define ACCESS_POLL_INTERVAL_MS 60U

#if defined(CONFIG_BT_SECURE_BOOT)
	#define BT_SECURE_OTP_DEST_ADDR              CON_BOOT_BT_SECURE_OTP_DEST_ADDR
	#define MCU_OTP_AUTOLOAD_REGION_ADDR         CON_BOOT_MCU_OTP_AUTOLOAD_ADDR
	#define MCU_OTP_AUTOLOAD_REGION_SIZE         CON_BOOT_MCU_OTP_AUTOLOAD_SIZE
	#define MCU_OTP_BTSW_NONSECURE_REGION_ADDR   CON_BOOT_MCU_OTP_BTSW_NS_ADDR
	#define MCU_OTP_BTSW_NONSECURE_REGION_SIZE   CON_BOOT_MCU_OTP_BTSW_NS_SIZE
#endif

static const con_boot_window mcu2css[] = {
	{ 4U, CON_BOOT_BT_MCU2CSS_WINDOW4_ADDR, CON_BOOT_BT_MCU2CSS_WINDOW4_SIZE, CON_BOOT_BT_MCU_RAM_BASE },
};

static const con_boot_window css2mcu[] = {
	{ 4U, CON_BOOT_BT_CSS2MCU_WINDOW4_ADDR, CON_BOOT_BT_CSS2MCU_WINDOW_SIZE, CON_BOOT_MCU_SYSCTRL_ADDR },
	{ 5U, CON_BOOT_BT_CSS2MCU_WINDOW5_ADDR, CON_BOOT_BT_CSS2MCU_WINDOW_SIZE, CON_BOOT_MCU_SPROT_MBOX_ADDR },
};

static const con_boot_window_config windows = {
	mcu2css, ARR_SIZE(mcu2css),
	css2mcu, ARR_SIZE(css2mcu)
};

static bool bt_initialized;
static bool bt_cold_boot = true;
static bool bt_download_complete;

static void power_off(void)
{
	uint32_t css_ctl;

	CON_BOOT_ATOMIC_BLOCK({
		con_boot_bus_read32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, &css_ctl);
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl & ~BT_ENABLE_BIT);
	});
}

static void assert_reset(void)
{
	uint32_t pwr_ctrl;
	con_boot_bus_read32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, &pwr_ctrl);
	con_boot_bus_write32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, pwr_ctrl | BT_RESET_BIT);

	for (uint32_t i = 0U; i < BT_RESET_DELAY; ++i)
		__NOP();

	con_boot_bus_read32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, &pwr_ctrl);
	con_boot_bus_write32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, pwr_ctrl & ~BT_RESET_BIT);
}

static void clear_boot_status(void)
{
	con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_BLE2MCU_INT_STATUS_OFFSET, BT_BOOT_VALID);
	__DSB();
}

static void set_wake_request(void)
{
	uint32_t pwr_ctrl;

	CON_BOOT_ATOMIC_BLOCK({
		con_boot_bus_read32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, &pwr_ctrl);

		pwr_ctrl |= BT_WAKE_REQ_BIT;
		con_boot_bus_write32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, pwr_ctrl);
	});
}

static void power_on(void)
{
	uint32_t css_ctl;

	CON_BOOT_ATOMIC_BLOCK({
		con_boot_bus_read32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, &css_ctl);
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl | BT_RETENTION_BIT);

		con_boot_bus_read32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, &css_ctl);
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl | BT_ENABLE_BIT);
	});
}

#if defined(CONFIG_BT_SECURE_BOOT)
static int32_t prepare_secure_boot(void)
{
	uint8_t otp_data[MCU_OTP_AUTOLOAD_REGION_SIZE + MCU_OTP_BTSW_NONSECURE_REGION_SIZE];
	int32_t ret;

	ret = con_boot_bus_read(MCU_OTP_AUTOLOAD_REGION_ADDR, otp_data, MCU_OTP_AUTOLOAD_REGION_SIZE);

	if (ret == 0) {
		ret = con_boot_bus_read(MCU_OTP_BTSW_NONSECURE_REGION_ADDR,
														otp_data + MCU_OTP_AUTOLOAD_REGION_SIZE,
														MCU_OTP_BTSW_NONSECURE_REGION_SIZE);
	}
	if (ret == 0) {
		ret = con_boot_bus_write(BT_SECURE_OTP_DEST_ADDR, otp_data, sizeof(otp_data));
	}

	memset(otp_data, 0, sizeof(otp_data));

	return ret;
}
#endif

/*
 * Determine whether WLAN is still held in reset during cold boot.
 *
 * The PMS architecture specifies that secondary subsystems are held in
 * reset after cold boot until the initial M52/PIE initialization completes.
 * A reset-active state is represented by one or more bits set in
 * GLOBAL_SEC.PWR_CTRL.
 *
 * WLAN reset_core() clears WLAN_RESET_BITS as part of WLAN initialization.
 * Therefore, during the sequential initial boot flow:
 *
 *   - Any WLAN reset bit set means WLAN has not been initialized yet.
 *   - All WLAN reset bits clear means WLAN initialization has already
 *     progressed past the reset-release stage.
 *
 * BT uses this hardware state only during its first initialization to decide
 * whether BT must perform assert_reset(). On later BT reinitializations,
 * bt_cold_boot is false and this check is not used.
 *
 * This avoids maintaining separate software state for WLAN initialization.
 * The initial WLAN/BT boot calls are required to be sequential.
 */
static bool wlan_is_in_reset(void)
{
	uint32_t pwr_ctrl;

	con_boot_bus_read32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, &pwr_ctrl);

	return (pwr_ctrl & WLAN_RESET_BITS) != 0U;
}

int32_t con_boot_bt_init(void)
{
	const uint32_t init_start_ms = con_boot_time_ms();

	bt_initialized = false;
	bt_download_complete = false;

	CON_BOOT_TRACE("BT init start");

	power_off();

	if (bt_cold_boot && wlan_is_in_reset()) {
		assert_reset();

	} else {
		CON_BOOT_TRACE("BT reset skipped because WLAN is active or BT is already initialized");
	}

	clear_boot_status();

	if (con_boot_configure_windows(&windows) != 0) {
		CON_BOOT_TRACE("BT window configuration failed");
		return -EIO;
	}

	CON_BOOT_TRACE("BT window configuration complete");

	set_wake_request();

	power_on();

	const uint32_t start_ms = con_boot_time_ms();

	do {
		// Delay to avoid TZC access before its configuration completes
		con_boot_delay_ms(ACCESS_POLL_INTERVAL_MS);

		uint32_t grant = 0U;
		int32_t ret = con_boot_bus_read32(BT_ACCESS_STATUS_ADDR, &grant);

		CON_BOOT_TRACE("BT ACCESS_GRANTED poll addr=0xC002cc00 ret=%d value=0x%08x", ret, grant);

		if ((ret == 0) && (grant == ACCESS_GRANTED)) {

#if defined(CONFIG_BT_SECURE_BOOT)

			ret = prepare_secure_boot();
			if (ret != 0) {
				CON_BOOT_TRACE("BT secure OTP handoff failed ret=%d", ret);
				return ret;
			}
#endif
			bt_initialized = true;
			bt_cold_boot = false;

			CON_BOOT_TRACE("\nBT init complete total time=%u ms\n", (unsigned)(con_boot_time_ms() - init_start_ms));
			return 0;
		}
	} while ((uint32_t)(con_boot_time_ms() - start_ms) < ACCESS_TIMEOUT_MS);

	return -ETIMEDOUT;
}

int32_t con_boot_bt_download(const con_boot_file_info *files, size_t file_count, uint32_t timeout_ms)
{
	const uint32_t start_ms = con_boot_time_ms();

	if (timeout_ms == 0U)
		return -EINVAL;

	if (!bt_initialized)
		return -EACCES;

	if (bt_download_complete)
		return -EALREADY;

	CON_BOOT_TRACE("BT payload transfer start files=%u timeout_ms=%u", (unsigned)file_count, timeout_ms);
	int32_t ret = con_boot_transfer_files(files, file_count, timeout_ms);
	if (ret != 0)
		return ret;

	CON_BOOT_TRACE("BT payload transfer complete; writing HOST_DOWNLOAD_COMPLETE addr=0xC002cc04");

	ret = con_boot_bus_write32(BT_DOWNLOAD_STATUS_ADDR, DOWNLOAD_COMPLETE);

	if (ret == 0) {
		uint32_t status = 0U;
		ret = con_boot_bus_read32(BT_DOWNLOAD_STATUS_ADDR, &status);

		CON_BOOT_TRACE("BT HOST_DOWNLOAD_COMPLETE readback addr=0xC002cc04 ret=%d value=0x%08x", ret, status);

		if (ret == 0) {
			if (status == DOWNLOAD_COMPLETE) {
				bt_download_complete = true;

				CON_BOOT_TRACE("\nBT payload transfer complete total time=%u ms\n", (unsigned)(con_boot_time_ms() - start_ms));

			} else
				ret = -EIO;
		}
	}
	return ret;
}
