/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fw_update.h"
#include "fw_update_boot.h"

#include <stddef.h>
#include <stdint.h>

#include <zephyr/arch/common/sys_io.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <logger.h>

#include "nvm.h"

#ifndef LOG_MOD_FW_UPDATE
#define LOG_MOD_FW_UPDATE "FW_UPDATE"
#endif

#if !DT_HAS_CHOSEN(zephyr_flash_controller)
#error "zephyr,flash-controller chosen node is required for FW update boot handoff"
#endif

#define FW_UPDATE_BOOT_FLASH_NODE DT_CHOSEN(zephyr_flash_controller)
#define FW_UPDATE_BOOT_XIP_BASE DT_REG_ADDR_BY_NAME(FW_UPDATE_BOOT_FLASH_NODE, xspi_xip)
#define FW_UPDATE_BOOT_GCR_BASE DT_REG_ADDR(DT_NODELABEL(gcr))

#define FW_UPDATE_BOOT_BCM_BASE 0x50112000U
#define FW_UPDATE_BOOT_BCM_CMD_PARAM0_OFFSET 0x0U
#define FW_UPDATE_BOOT_BCM_CMD_PARAM1_OFFSET 0x4U
#define FW_UPDATE_BOOT_BCM_CMD_OFFSET 0x40U
#define FW_UPDATE_BOOT_BCM_CMD_RET_STATUS_OFFSET 0x80U
#define FW_UPDATE_BOOT_BCM_HST_INTERRUPT_RST_OFFSET 0xC8U
#define FW_UPDATE_BOOT_BCM_SP_CMD_CMPLT BIT(0)
#define FW_UPDATE_BOOT_BCM_CMD_CONFIG_AXI_CRYPTO 0x0103U
#define FW_UPDATE_BOOT_BCM_CLK_CTRL_OFFSET 0x24U
#define FW_UPDATE_BOOT_SYNC_WORD 0x5A5BU
#define FW_UPDATE_BOOT_SERVICE_ID 0x33U
#define FW_UPDATE_BOOT_OP_RUN_APBL_IMG 0x0CU
#define FW_UPDATE_BOOT_OP_RUN_FW_IMG 0x0DU
#define FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_IP 0x12U
#define FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_NOT_IP 0x13U
#define FW_UPDATE_BOOT_MAX_AXI_SECTIONS 8U
#define FW_UPDATE_BOOT_BCM_WAIT_NOP 20U
#define FW_UPDATE_BOOT_BCM_WAIT_ITERATIONS 500000U

typedef struct boot_api_cmd_hdr {
	uint16_t sync_word;
	uint8_t service_id;
	uint8_t op_code;
} st_boot_api_cmd_hdr;

typedef struct boot_api_gen_cmd {
	st_boot_api_cmd_hdr hdr;
	uint32_t reserved_1;
	uint32_t reserved_2;
	uint32_t reserved_3;
	uint32_t reserved_4;
	uint32_t reserved_5;
	uint32_t reserved_6;
	uint32_t reserved_7;
} st_boot_api_gen_cmd;

typedef struct boot_api_download_cmd {
	st_boot_api_cmd_hdr hdr;
	uint32_t reserved_1;
	uint32_t num_words_1;
	uint32_t num_words_2;
	uint32_t exe_addr;
	uint32_t im_addr;
	uint32_t img_type;
	uint32_t flags;
} st_boot_api_download_cmd;

typedef struct boot_api_run_cmd {
	st_boot_api_cmd_hdr hdr;
	uint32_t reserved_1;
	uint32_t address;
	uint32_t reserved_3;
	uint32_t reserved_4;
	uint32_t reserved_5;
	uint32_t reserved_6;
	uint32_t reserved_7;
} st_boot_api_run_cmd;

typedef union boot_api_flags {
	struct {
		uint32_t warm_boot : 1;
		uint32_t jump_addr : 1;
		uint32_t last_img : 1;
		uint32_t reserved_1 : 29;
	} bits;
	uint32_t word;
} t_boot_api_flags;

typedef struct fw_update_boot_axi_crypto_cfg {
	uint32_t ctl;
	uint32_t start_addr;
	uint32_t end_addr;
	uint32_t addr_offset;
} st_fw_update_boot_axi_crypto_cfg;

static st_fw_update_boot_axi_crypto_cfg s_axi_crypto_cfg[FW_UPDATE_BOOT_MAX_AXI_SECTIONS];

static ALWAYS_INLINE void p_wait_num_nop(uint32_t num_nop)
{
	volatile uint32_t i;

	for (i = 0U; i < num_nop; i++) {
		__asm__ volatile("nop");
	}
}

static ALWAYS_INLINE void p_copy_bytes(void *dst, const void *src, size_t size)
{
	uint8_t *dst_u8 = (uint8_t *)dst;
	const uint8_t *src_u8 = (const uint8_t *)src;

	for (size_t i = 0U; i < size; i++) {
		dst_u8[i] = src_u8[i];
	}
}

static ALWAYS_INLINE void p_sync_caches_before_jump(void)
{
	int icache_rc;
	int dcache_rc;

	dcache_rc = sys_cache_data_flush_and_invd_all();
	icache_rc = sys_cache_instr_flush_and_invd_all();

	ARG_UNUSED(dcache_rc);
	ARG_UNUSED(icache_rc);

	__DSB();
	__ISB();
}

static ALWAYS_INLINE void p_bcm_clock_enable(void)
{
	uint32_t val = sys_read32(FW_UPDATE_BOOT_GCR_BASE + FW_UPDATE_BOOT_BCM_CLK_CTRL_OFFSET);

	sys_write32(val | BIT(0), FW_UPDATE_BOOT_GCR_BASE + FW_UPDATE_BOOT_BCM_CLK_CTRL_OFFSET);
}

static int32_t p_bcm_wait_for_complete(void)
{
	for (uint32_t i = 0U; i < FW_UPDATE_BOOT_BCM_WAIT_ITERATIONS; i++) {
		uint32_t val = sys_read32(FW_UPDATE_BOOT_BCM_BASE +
					    FW_UPDATE_BOOT_BCM_HST_INTERRUPT_RST_OFFSET);

		if ((val & FW_UPDATE_BOOT_BCM_SP_CMD_CMPLT) != 0U) {
			return FW_UPDATE_RC_OK;
		}

		p_wait_num_nop(FW_UPDATE_BOOT_BCM_WAIT_NOP);
	}

	return FW_UPDATE_RC_DECRYPTION_ERR;
}

static ALWAYS_INLINE void p_bcm_clear_complete(void)
{
	sys_write32(FW_UPDATE_BOOT_BCM_SP_CMD_CMPLT,
		   FW_UPDATE_BOOT_BCM_BASE + FW_UPDATE_BOOT_BCM_HST_INTERRUPT_RST_OFFSET);
}

static int32_t p_otf_init(const st_nvm_security *security)
{
	uint32_t *section_words;
	uint32_t sections;

	if (security == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	sections = security->num_of_defined_sections;
	if (sections == 0U) {
		return FW_UPDATE_RC_OK;
	}

	if (sections > FW_UPDATE_BOOT_MAX_AXI_SECTIONS) {
		sections = FW_UPDATE_BOOT_MAX_AXI_SECTIONS;
	}

	section_words = (uint32_t *)security;
	section_words++;

	for (uint32_t i = 0U; i < sections; i++) {
		s_axi_crypto_cfg[i].ctl = section_words[0] | (section_words[1] << 1);
		s_axi_crypto_cfg[i].start_addr = FW_UPDATE_BOOT_XIP_BASE + section_words[2];
		s_axi_crypto_cfg[i].end_addr = FW_UPDATE_BOOT_XIP_BASE + section_words[3];
		s_axi_crypto_cfg[i].addr_offset = section_words[4];
		section_words += 5;
	}

	(void)sys_cache_data_flush_range(s_axi_crypto_cfg, sizeof(s_axi_crypto_cfg));
	p_bcm_clock_enable();

	sys_write32(sections, FW_UPDATE_BOOT_BCM_BASE + FW_UPDATE_BOOT_BCM_CMD_PARAM0_OFFSET);
	sys_write32((uint32_t)(uintptr_t)s_axi_crypto_cfg,
		   FW_UPDATE_BOOT_BCM_BASE + FW_UPDATE_BOOT_BCM_CMD_PARAM1_OFFSET);

	sys_write32(FW_UPDATE_BOOT_BCM_CMD_CONFIG_AXI_CRYPTO,
		   FW_UPDATE_BOOT_BCM_BASE + FW_UPDATE_BOOT_BCM_CMD_OFFSET);

	if (p_bcm_wait_for_complete() != FW_UPDATE_RC_OK) {
		return FW_UPDATE_RC_DECRYPTION_ERR;
	}

	if (sys_read32(FW_UPDATE_BOOT_BCM_BASE + FW_UPDATE_BOOT_BCM_CMD_RET_STATUS_OFFSET) != 0U) {
		p_bcm_clear_complete();
		return FW_UPDATE_RC_DECRYPTION_ERR;
	}

	p_bcm_clear_complete();
	return FW_UPDATE_RC_OK;
}

static ALWAYS_INLINE void p_read_boot_cmd(uint32_t image_offset, st_boot_api_gen_cmd *cmd)
{
	p_copy_bytes(cmd, (const void *)(uintptr_t)(FW_UPDATE_BOOT_XIP_BASE + image_offset), sizeof(*cmd));
}

static int32_t p_mark_fw_update_failed(st_nvm_fw *nvm, uint32_t rc)
{
	st_nvm_fw_update_component *component;

	if (nvm == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	if (nvm->data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		nvm->data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < nvm->data.sw_update.num_components; i++) {
		component = &nvm->data.sw_update.components[i];
		if ((component->state == FW_UPDATE_STATE_STAGED) ||
		    (component->state == FW_UPDATE_STATE_TRIAL)) {
			component->state = FW_UPDATE_STATE_FAILED;
			component->failure_cause = rc;
		}
	}

	nvm->data.sw_update.state = FW_UPDATE_STATE_FAILED;
	nvm->data.sw_update.reset_cause = 0U;
	nvm->data.sw_update.failure_cause = rc;

	return (nvm_set_data(nvm) == NVM_RC_OK) ? (int32_t)rc : FW_UPDATE_RC_SET_NVM_ERR;
}

int32_t fw_update_boot_mark_trial(void)
{
	st_nvm_fw nvm = { 0 };
	st_nvm_fw_update_component *component;
	st_nvm_image_slot *primary_slot;
	st_nvm_image_slot *rollback_slot;
	int32_t rc;

	rc = nvm_get_data(&nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (nvm.data.sw_update.state != FW_UPDATE_STATE_STAGED) {
		return FW_UPDATE_RC_OK;
	}

	if (nvm.data.sw_update.reset_cause != FW_UPDATE_M55_SW_RST_CAUSE) {
		return FW_UPDATE_RC_OK;
	}

	if (nvm.data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		nvm.data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < nvm.data.sw_update.num_components; i++) {
		component = &nvm.data.sw_update.components[i];

		if (component->state == FW_UPDATE_STATE_STAGED) {
			primary_slot = &component->slots[component->primary_slot];
			rollback_slot = &component->slots[component->secondary_slot];
			primary_slot->image_is_bootable = FW_UPDATE_IMG_BOOTABLE;
			rollback_slot->image_is_bootable = FW_UPDATE_IMG_BOOTABLE;
			component->state = FW_UPDATE_STATE_TRIAL;
			component->failure_cause = FW_UPDATE_RC_OK;
		}
	}

	nvm.data.sw_update.state = FW_UPDATE_STATE_TRIAL;
	nvm.data.sw_update.reset_cause = 0U;
	nvm.data.sw_update.failure_cause = FW_UPDATE_RC_OK;
	rc = nvm_set_data(&nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_SET_NVM_ERR;
	}

	return FW_UPDATE_RC_OK;
}

static int32_t p_scan_staged_image(uint32_t image_offset, uint32_t *jump_addr)
{
	st_boot_api_gen_cmd cmd = { 0 };
	t_boot_api_flags flags;

	if (jump_addr == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	while (true) {
		p_read_boot_cmd(image_offset, &cmd);

		if ((cmd.hdr.sync_word != FW_UPDATE_BOOT_SYNC_WORD) ||
		    (cmd.hdr.service_id != FW_UPDATE_BOOT_SERVICE_ID)) {
			return FW_UPDATE_RC_DECRYPTION_ERR;
		}

		switch (cmd.hdr.op_code) {
		case FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_IP:
		case FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_NOT_IP: {
			st_boot_api_download_cmd *download_cmd = (st_boot_api_download_cmd *)&cmd;

			flags.word = download_cmd->flags;
			if (flags.bits.jump_addr != 0U) {
				*jump_addr = download_cmd->reserved_1;
			}

			image_offset += sizeof(*download_cmd);
			image_offset += (download_cmd->num_words_1 << 2);
			image_offset += (download_cmd->num_words_2 << 2);

			if (flags.bits.last_img != 0U) {
				return (*jump_addr != 0U) ? FW_UPDATE_RC_OK : FW_UPDATE_RC_DECRYPTION_ERR;
			}
			break;
		}
		case FW_UPDATE_BOOT_OP_RUN_APBL_IMG:
		case FW_UPDATE_BOOT_OP_RUN_FW_IMG: {
			st_boot_api_run_cmd *run_cmd = (st_boot_api_run_cmd *)&cmd;

			*jump_addr = run_cmd->address;
			return (*jump_addr != 0U) ? FW_UPDATE_RC_OK : FW_UPDATE_RC_DECRYPTION_ERR;
		}
		default:
			return FW_UPDATE_RC_NOT_SUPPORTED;
		}
	}
}

static void p_jump_to_address(uint32_t jump_addr)
{
	void (*entry)(void) = (void (*)(void))(uintptr_t)jump_addr;

	p_sync_caches_before_jump();
	(void)irq_lock();
	SysTick->CTRL = 0U;
	__DSB();
	__ISB();
	entry();
}

static int32_t p_load_staged_image(uint32_t image_offset)
{
	st_boot_api_gen_cmd cmd = { 0 };
	t_boot_api_flags flags;
	uint32_t jump_addr = 0U;

	while (true) {
		p_read_boot_cmd(image_offset, &cmd);

		if ((cmd.hdr.sync_word != FW_UPDATE_BOOT_SYNC_WORD) ||
		    (cmd.hdr.service_id != FW_UPDATE_BOOT_SERVICE_ID)) {
			return FW_UPDATE_RC_DECRYPTION_ERR;
		}

		switch (cmd.hdr.op_code) {
		case FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_IP:
		case FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_NOT_IP: {
			st_boot_api_download_cmd *download_cmd = (st_boot_api_download_cmd *)&cmd;
			size_t body_size = (size_t)download_cmd->num_words_2 << 2;

			flags.word = download_cmd->flags;
			if (flags.bits.jump_addr != 0U) {
				jump_addr = download_cmd->reserved_1;
			}

			image_offset += sizeof(*download_cmd);
			image_offset += (download_cmd->num_words_1 << 2);

			if (cmd.hdr.op_code == FW_UPDATE_BOOT_OP_DWNLD_SCR_IMG_DEC_NOT_IP) {
				p_copy_bytes((void *)(uintptr_t)download_cmd->exe_addr,
					     (const void *)(uintptr_t)(FW_UPDATE_BOOT_XIP_BASE + image_offset),
					     body_size);
			}

			image_offset += (download_cmd->num_words_2 << 2);

			if (flags.bits.last_img != 0U) {
				if (jump_addr == 0U) {
					return FW_UPDATE_RC_DECRYPTION_ERR;
				}

				p_jump_to_address(jump_addr);
				return FW_UPDATE_RC_DECRYPTION_ERR;
			}
			break;
		}
		case FW_UPDATE_BOOT_OP_RUN_APBL_IMG:
		case FW_UPDATE_BOOT_OP_RUN_FW_IMG: {
			st_boot_api_run_cmd *run_cmd = (st_boot_api_run_cmd *)&cmd;

			if (run_cmd->address == 0U) {
				return FW_UPDATE_RC_DECRYPTION_ERR;
			}

			p_jump_to_address(run_cmd->address);
			return FW_UPDATE_RC_DECRYPTION_ERR;
		}
		default:
			return FW_UPDATE_RC_NOT_SUPPORTED;
		}
	}
}

int32_t fw_update_boot_try_handoff(void)
{
	st_nvm_fw nvm = { 0 };
	st_nvm_fw_update_component *sdk_component;
	st_nvm_image_slot *primary_slot;
	uint32_t staged_offset;
	uint32_t jump_addr = 0U;
	int32_t rc;

	rc = nvm_get_data(&nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (nvm.data.sw_update.state != FW_UPDATE_STATE_STAGED) {
		return FW_UPDATE_RC_OK;
	}

	if (nvm.data.sw_update.reset_cause != FW_UPDATE_M55_SW_RST_CAUSE) {
		return FW_UPDATE_RC_OK;
	}

	sdk_component = &nvm.data.sw_update.components[FW_UPDATE_IMG_TYPE_SDK];
	if ((sdk_component->num_slots == 0U) || (sdk_component->primary_slot >= sdk_component->num_slots)) {
		return p_mark_fw_update_failed(&nvm, FW_UPDATE_RC_NOT_SUPPORTED);
	}

	primary_slot = &sdk_component->slots[sdk_component->primary_slot];
	staged_offset = primary_slot->slot_address;

	rc = p_otf_init(&nvm.data.security);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_FW_UPDATE, "Failed to configure FW update OTF view rc=0x%08x\n",
			  (uint32_t)rc);
		return p_mark_fw_update_failed(&nvm, (uint32_t)rc);
	}

	rc = p_scan_staged_image(staged_offset, &jump_addr);
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_FW_UPDATE,
			  "Failed to scan staged SDK image rc=0x%08x jump=0x%08x\n",
			  (uint32_t)rc, (uint32_t)jump_addr);
		return p_mark_fw_update_failed(&nvm, (uint32_t)rc);
	}

	rc = fw_update_boot_mark_trial();
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_FW_UPDATE, "Failed to advance staged update into TRIAL rc=0x%08x\n",
			  (uint32_t)rc);
		return rc;
	}

	return p_load_staged_image(staged_offset);
}

#if defined(CONFIG_FW_UPDATE_BOOT_HOOK_ENABLED)
static int fw_update_boot_init(void)
{
	int32_t rc = fw_update_boot_try_handoff();

	if ((rc != FW_UPDATE_RC_OK) && (rc != FW_UPDATE_RC_DECRYPTION_ERR)) {
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "FW update boot hook rc=0x%08x\n", (uint32_t)rc);
	}

	return 0;
}

SYS_INIT(fw_update_boot_init, APPLICATION, 0);
#endif
