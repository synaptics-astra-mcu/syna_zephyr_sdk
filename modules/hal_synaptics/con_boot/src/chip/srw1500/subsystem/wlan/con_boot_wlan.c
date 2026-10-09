/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <cmsis_compiler.h>

#include "con_boot.h"
#include "con_boot_platform.h"
#include "con_boot_time.h"
#include "con_boot_trace.h"
#include "con_boot_windows.h"
#include "con_boot_transfer.h"
#include "con_boot_srw1500_regs.h"

#define ARR_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#if defined(CONFIG_WLAN_SECURE_BOOT)
	#define WLAN_SECURITY_MODE "secure"
	#define WLAN_MCU2CSS_WINDOW0_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW0_ADDR
	#define WLAN_MCU2CSS_WINDOW1_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW1_ADDR
	#define WLAN_MCU2CSS_WINDOW2_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW2_ADDR
	#define WLAN_MCU2CSS_WINDOW3_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW3_ADDR
#else
	#define WLAN_SECURITY_MODE "non-secure"
	#define WLAN_MCU2CSS_WINDOW0_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW0_NS_ADDR
	#define WLAN_MCU2CSS_WINDOW1_ADDR CON_BOOT_WLAN_MCU2CSS_WINDOW1_NS_ADDR
#endif

#define WLAN_ACCESS_STATUS_ADDR (WLAN_MCU2CSS_WINDOW0_ADDR + CON_BOOT_WLAN_SHARED_MEM_BASE)
#define WLAN_DOWNLOAD_STATUS_ADDR (WLAN_ACCESS_STATUS_ADDR + 0x4U)
#define WLAN_HT_ACCESS_BIT (1U << 4)
#define WLAN_HT_CLOCK_REQUEST_BIT (1U << 1)
#define WLAN_CR4_CONTROL_MASK (0x7U << 5)
#define WLAN_CR4_CLOCK_BIT (1U << 5)
#define WLAN_CR4_RUN_BIT (1U << 6)
#define WLAN_CR4_RESET_BIT (1U << 8)
#define WLAN_RETENTION_BIT (1U << 11)
#define ACCESS_GRANTED 0xAAAAAAAAU
#define DOWNLOAD_COMPLETE 0xFFFFFFFFU
#define ACCESS_TIMEOUT_MS 20000U
#define ACCESS_POLL_INTERVAL_MS 60U


static const con_boot_window mcu2css[] = {
	{ 0U, WLAN_MCU2CSS_WINDOW0_ADDR, CON_BOOT_WLAN_RAM_SIZE, CON_BOOT_WLAN_RAM_BASE },
	{ 1U, WLAN_MCU2CSS_WINDOW1_ADDR, 0x00001000U, CON_BOOT_WLAN_CHIPCOMMON_BASE },
#if defined(CONFIG_WLAN_SECURE_BOOT)
	/* Secure WLAN requires the complete static config of CR4 and GCI windows. */
	{ 2U, WLAN_MCU2CSS_WINDOW2_ADDR, 0x00001000U, CON_BOOT_WLAN_ARMCR4_BASE },
	{ 3U, WLAN_MCU2CSS_WINDOW3_ADDR, 0x00000C00U, CON_BOOT_WLAN_GCI_BASE },
#endif
};

static const con_boot_window css2mcu[] = {
	{ 0U, CON_BOOT_CSS2MCU_WINDOW0_ADDR, 0x00001000U, CON_BOOT_MCU_SPROT_MBOX_ADDR },
	{ 1U, CON_BOOT_CSS2MCU_WINDOW1_ADDR, 0x00001000U, CON_BOOT_MCU_SYSCTRL_ADDR },
	/* MHD allocates its SCI buffers in secure MCU SRAM. */
	{ 2U, CON_BOOT_CSS2MCU_WINDOW2_ADDR, CON_BOOT_WLAN_MCU_SRAM_SIZE, CON_BOOT_WLAN_MCU_SRAM_BASE },
	{ 3U, CON_BOOT_CSS2MCU_WINDOW3_ADDR, CON_BOOT_MCU_OTP_SIZE, CON_BOOT_MCU_OTP_ADDR },
};

static const con_boot_window_config windows = {
	mcu2css, ARR_SIZE(mcu2css),
	css2mcu, ARR_SIZE(css2mcu)
};

static bool wlan_initialized;
static bool wlan_download_complete;

static void prepare_core(void)
{
	uint32_t reset_cfg, ht_clock;
	con_boot_bus_read32(CON_BOOT_GGC_BASE + GGC_RESET_CFG_OFFSET, &reset_cfg);
	con_boot_bus_read32(CON_BOOT_GCI_BASE + GCI_HT_CLOCK_OFFSET, &ht_clock);

	CON_BOOT_TRACE("WLAN prepare before RESET_CFG=0x%08x HT_CLOCK=0x%08x", reset_cfg, ht_clock);

	con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_RESET_CFG_OFFSET, reset_cfg | WLAN_HT_ACCESS_BIT);
	con_boot_bus_write32(CON_BOOT_GCI_BASE + GCI_HT_CLOCK_OFFSET, ht_clock | WLAN_HT_CLOCK_REQUEST_BIT);

	CON_BOOT_TRACE("WLAN prepare after RESET_CFG=0x%08x HT_CLOCK=0x%08x", reset_cfg | WLAN_HT_ACCESS_BIT, ht_clock | WLAN_HT_CLOCK_REQUEST_BIT);
}

static void reset_core(void)
{
	uint32_t css_ctl, power_ctrl;

	CON_BOOT_ATOMIC_BLOCK({
		(void)con_boot_bus_read32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, &css_ctl);
		(void)con_boot_bus_read32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, &power_ctrl);

		power_ctrl &= ~WLAN_RESET_BITS;
		con_boot_bus_write32(CON_BOOT_GBL_SEC_BASE + GBL_SEC_PWR_CTRL_OFFSET, power_ctrl);

		css_ctl |= WLAN_RETENTION_BIT | WLAN_CR4_RESET_BIT;
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl);

		css_ctl = (css_ctl & ~WLAN_CR4_CONTROL_MASK) | WLAN_CR4_RUN_BIT | WLAN_CR4_CLOCK_BIT;
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl);

		css_ctl &= ~WLAN_CR4_RESET_BIT;
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl);

		css_ctl = (css_ctl & ~WLAN_CR4_CONTROL_MASK) | WLAN_CR4_CLOCK_BIT;
		con_boot_bus_write32(CON_BOOT_GGC_BASE + GGC_CSS_CTL_OFFSET, css_ctl);
	});

	CON_BOOT_TRACE("WLAN CR4 reset complete CSS_CTL=0x%08x POWER_CTRL=0x%08x", css_ctl, power_ctrl);
}

int32_t con_boot_wlan_init(void)
{
	const uint32_t init_start_ms = con_boot_time_ms();

	wlan_initialized = false;
	wlan_download_complete = false;

	CON_BOOT_TRACE("WLAN init start mode=%s window0=0x%08x access=0x%08x", WLAN_SECURITY_MODE, WLAN_MCU2CSS_WINDOW0_ADDR, WLAN_ACCESS_STATUS_ADDR);

	prepare_core();

	if (con_boot_configure_windows(&windows) != 0) {
		CON_BOOT_TRACE("WLAN window configuration failed");
		return -EIO;
	}

	CON_BOOT_TRACE("WLAN window configuration complete");

	reset_core();

	const uint32_t start_ms = con_boot_time_ms();

	do {
		// Delay to avoid TZC access before its configuration completes
		con_boot_delay_ms(ACCESS_POLL_INTERVAL_MS);

		uint32_t grant = 0U;
		int32_t ret = con_boot_bus_read32(WLAN_ACCESS_STATUS_ADDR, &grant);

		CON_BOOT_TRACE("WLAN ACCESS_GRANTED poll addr=0x%08x ret=%d value=0x%08x", WLAN_ACCESS_STATUS_ADDR, ret, grant);

		if (ret == 0 && grant == ACCESS_GRANTED) {
			wlan_initialized = true;

			CON_BOOT_TRACE("\nWLAN init complete total time=%u ms\n", (unsigned)(con_boot_time_ms() - init_start_ms));

			return 0;
		}
	} while ((uint32_t)(con_boot_time_ms() - start_ms) < ACCESS_TIMEOUT_MS);

	return -ETIMEDOUT;
}

int32_t con_boot_wlan_download(const con_boot_file_info *files, size_t file_count, uint32_t timeout_ms)
{
	const uint32_t start_ms = con_boot_time_ms();

	if (timeout_ms == 0U)
		return -EINVAL;

	if (!wlan_initialized)
		return -EACCES;

	if (wlan_download_complete)
		return -EALREADY;

	CON_BOOT_TRACE("WLAN payload transfer start files=%u timeout_ms=%u", (unsigned)file_count, timeout_ms);

	int32_t ret = con_boot_transfer_files(files, file_count, timeout_ms);
	if (ret != 0) {
		CON_BOOT_TRACE("WLAN payload transfer failed ret=%d", (int)ret);
		return ret;
	}

	CON_BOOT_TRACE("WLAN payload transfer complete; writing HOST_DOWNLOAD_COMPLETE addr=0x%08x", WLAN_DOWNLOAD_STATUS_ADDR);

	ret = con_boot_bus_write32(WLAN_DOWNLOAD_STATUS_ADDR, DOWNLOAD_COMPLETE);

	if (ret == 0) {
		uint32_t status = 0U;
		ret = con_boot_bus_read32(WLAN_DOWNLOAD_STATUS_ADDR, &status);

		CON_BOOT_TRACE("WLAN HOST_DOWNLOAD_COMPLETE readback addr=0x%08x ret=%d value=0x%08x", WLAN_DOWNLOAD_STATUS_ADDR, ret, status);

		if (ret == 0 && status == DOWNLOAD_COMPLETE) {
			wlan_download_complete = true;

			CON_BOOT_TRACE("\nWLAN payload transfer complete total time=%u ms\n", (unsigned)(con_boot_time_ms() - start_ms));

		} else if (ret == 0) {
			ret = -EIO;
		}
	}
	return ret;
}
