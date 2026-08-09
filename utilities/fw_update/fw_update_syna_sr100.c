/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fw_update.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include <logger.h>

#include "fw_update_cfg.h"
#include "fw_update_internal.h"
#include "nvm.h"

#ifndef LOG_MOD_FW_UPDATE
#define LOG_MOD_FW_UPDATE "FW_UPDATE"
#endif

#if !DT_HAS_CHOSEN(zephyr_flash_controller)
#error "zephyr,flash-controller chosen node is required for FW update support"
#endif

#define FW_UPDATE_FLASH_NODE DT_CHOSEN(zephyr_flash_controller)
#define FW_UPDATE_FLASH_DEV DEVICE_DT_GET(FW_UPDATE_FLASH_NODE)

#define VER_NUM_SIZE_IN_BYTES 4U
#define WRITE_MAX_NUM_BYTES FW_UPDATE_PAGE_SIZE_IN_BYTES
#define LOOKUP_TBL_MAX_CHARS 10U

static const uint8_t ver_num[VER_NUM_SIZE_IN_BYTES] = {
	0x00,
	CONFIG_HOST_API_SDK_VER_MAJOR,
	CONFIG_HOST_API_SDK_VER_MINOR,
	CONFIG_HOST_API_SDK_VER_REVISION,
};

static st_fw_update_info s_st_info;
static st_nvm_fw s_st_nvm;
static uint32_t s_written_image_size_in_bytes;
static uint32_t s_secondary_image_offset;
static bool s_is_secondary_offset_set;
#if defined(CONFIG_SYNA_LOG_DEBUG)
static const char state_lookup_tbl[FW_UPDATE_STATE_LAST][LOOKUP_TBL_MAX_CHARS] = {
	"READY", "WRITING", "CANDIDATE", "STAGED", "TRIAL", "REJECTED", "FAILED", "UPDATED",
};
static const char image_type_lookup_tbl[FW_UPDATE_IMG_TYPE_LAST][LOOKUP_TBL_MAX_CHARS] = {
	"SPK", "APBL", "SDK", "MODEL", "APP",
};
#endif

static int32_t fw_update_flash_get_device(const struct device **flash_dev);
static int32_t fw_update_flash_erase(uint32_t offset, uint32_t size);
static int32_t fw_update_flash_write(uint32_t offset, const uint8_t *data, uint32_t size);
static int32_t fw_update_flash_read(uint32_t offset, uint8_t *data, uint32_t size);
static uint32_t p_calc_crc(uint32_t crc, uint16_t size, uint8_t *p_buf);
static int32_t p_dependency_check(void);
static bool p_is_otf_managed_offset(uint32_t offset, uint32_t size);
static void p_dump_fw_update(void);
static void p_update_secondary_info(uint32_t image_id);
static void p_swap_slots(uint32_t image_id);
static void p_set_primary_slot_functional(void);
static void p_restore_primary_slot_metadata(st_nvm_fw_update_component *p_component);
static void p_clear_secondary_slot_metadata(st_nvm_fw_update_component *p_component);
static void p_cancel(uint32_t image_id, st_nvm_fw *p_st_nvm);
static int32_t p_fw_post(st_nvm_fw *p_st_nvm);
static int32_t p_fw_post_nvm(st_nvm_fw *p_st_nvm);
static int32_t p_handle_fw_post_failure(st_nvm_fw *p_st_nvm);
static st_nvm_fw_update_component *p_get_component(uint32_t image_id);

__attribute__((weak)) int32_t fw_update_platform_otf_disable(void)
{
	return 0;
}

__attribute__((weak)) int32_t fw_update_platform_otf_enable(void)
{
	return 0;
}

__attribute__((weak)) int32_t fw_update_platform_get_boot_partition(uint32_t *slot)
{
	if (slot != NULL) {
		*slot = 0U;
	}

	return 0;
}

__attribute__((weak)) int32_t fw_update_platform_update_boot_partition_indicator(void)
{
	return 0;
}

__attribute__((weak)) int32_t fw_update_platform_update_rollback_counter(uint32_t image_id)
{
	ARG_UNUSED(image_id);
	return 0;
}

static int32_t fw_update_flash_get_device(const struct device **flash_dev)
{
	if (flash_dev == NULL) {
		return -EINVAL;
	}

	*flash_dev = FW_UPDATE_FLASH_DEV;
	if (!device_is_ready(*flash_dev)) {
		LOG_ERROR(LOG_MOD_FW_UPDATE, "Flash controller is not ready\n");
		return -ENODEV;
	}

	return 0;
}

static int32_t fw_update_flash_erase(uint32_t offset, uint32_t size)
{
	const struct device *flash_dev;
	int ret;

	ret = fw_update_flash_get_device(&flash_dev);
	if (ret != 0) {
		return ret;
	}

	ret = flash_erase(flash_dev, (off_t)offset, size);
	if (ret == 0) {
		k_msleep(5);
	}

	return ret;
}

static int32_t fw_update_flash_write(uint32_t offset, const uint8_t *data, uint32_t size)
{
	const struct device *flash_dev;
	int ret;

	ret = fw_update_flash_get_device(&flash_dev);
	if (ret != 0) {
		return ret;
	}

	ret = flash_write(flash_dev, (off_t)offset, data, size);

	return ret;
}

static int32_t fw_update_flash_read(uint32_t offset, uint8_t *data, uint32_t size)
{
	const struct device *flash_dev;
	int ret;

	ret = fw_update_flash_get_device(&flash_dev);
	if (ret != 0) {
		return ret;
	}

	return flash_read(flash_dev, (off_t)offset, data, size);
}

static st_nvm_fw_update_component *p_get_component(uint32_t image_id)
{
	if (image_id >= FW_UPDATE_MAX_COMPONENTS) {
		return NULL;
	}

	return &s_st_nvm.data.sw_update.components[image_id];
}

int32_t fw_update_start(uint32_t image_id)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(image_id=%d) - Enter\n", __func__, image_id);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	if (p_component->state != FW_UPDATE_STATE_READY) {
		rc = FW_UPDATE_RC_START_OPCODE_BAD_STATE;
		goto exit_func;
	}

	s_written_image_size_in_bytes = 0U;
	s_secondary_image_offset = 0U;
	s_is_secondary_offset_set = false;
	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_WRITING;
	p_component->state = FW_UPDATE_STATE_WRITING;
	LOG_INFO(LOG_MOD_FW_UPDATE, "FW update starts (image_id=%u)\n", (unsigned int)image_id);

	(void)fw_update_platform_otf_disable();

exit_func:
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	p_component->failure_cause = (uint32_t)rc;
	if (rc != FW_UPDATE_RC_OK) {
		p_cancel(image_id, &s_st_nvm);
	}

	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_write(uint32_t image_id, uint32_t num_bytes, uint8_t *p_data)
{
	int32_t rc = FW_UPDATE_RC_OK;
	uint8_t rd_back_buff[FW_UPDATE_PAGE_SIZE_IN_BYTES];
	uint32_t input_crc;
	uint32_t rd_back_crc;
	st_nvm_fw_update_component *p_component;
	st_nvm_image_slot *p_slot;

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	p_slot = &p_component->slots[p_component->secondary_slot];
	if ((s_st_nvm.data.sw_update.state != FW_UPDATE_STATE_WRITING) ||
	    (p_component->state != FW_UPDATE_STATE_WRITING)) {
		rc = FW_UPDATE_RC_WRITE_OPCODE_BAD_STATE;
		goto exit_func;
	}

	if ((p_data == NULL) || (num_bytes == 0U) || (num_bytes > FW_UPDATE_PAGE_SIZE_IN_BYTES)) {
		rc = FW_UPDATE_RC_WRITE_ERR;
		goto exit_func;
	}

	if (!s_is_secondary_offset_set) {
		p_update_secondary_info(image_id);
		p_slot = &p_component->slots[p_component->secondary_slot];
		s_secondary_image_offset = p_slot->slot_address;
		s_is_secondary_offset_set = true;
	}

	if ((s_secondary_image_offset % FW_UPDATE_SECTOR_SIZE_IN_BYTES) == 0U) {
		rc = fw_update_flash_erase(s_secondary_image_offset, FW_UPDATE_SECTOR_SIZE_IN_BYTES);
		if (rc != 0) {
			LOG_ERROR(LOG_MOD_FW_UPDATE, "%s() - Erase sector failure\n", __func__);
			rc = FW_UPDATE_RC_WRITE_ERR;
			goto exit_func;
		}
	}

	memset(rd_back_buff, 0, sizeof(rd_back_buff));
	input_crc = p_calc_crc(FW_UPDATE_CRC32_START_VAL, (uint16_t)num_bytes, p_data);
	rc = fw_update_flash_write(s_secondary_image_offset, p_data, num_bytes);
	if (rc != 0) {
		LOG_ERROR(LOG_MOD_FW_UPDATE, "%s() - Page program failure\n", __func__);
		rc = FW_UPDATE_RC_WRITE_ERR;
		goto exit_func;
	}

	if (!p_is_otf_managed_offset(s_secondary_image_offset, num_bytes)) {
		rc = fw_update_flash_read(s_secondary_image_offset, rd_back_buff, num_bytes);
		if (rc != 0) {
			LOG_ERROR(LOG_MOD_FW_UPDATE, "%s() - Read-back failure\n", __func__);
			rc = FW_UPDATE_RC_WRITE_ERR;
			goto exit_func;
		}

		rd_back_crc = p_calc_crc(FW_UPDATE_CRC32_START_VAL, (uint16_t)num_bytes, rd_back_buff);
		if (input_crc != rd_back_crc) {
			LOG_ERROR(LOG_MOD_FW_UPDATE,
				  "%s() - CRC mismatch transport=0x%08X flash=0x%08X\n",
				  __func__, input_crc, rd_back_crc);
			rc = FW_UPDATE_RC_WRITE_ERR;
			goto exit_func;
		}
	} else {
		LOG_DEBUG(LOG_MOD_FW_UPDATE,
			  "%s() - Skip immediate CRC verify for OTF-managed range off=0x%08X size=%u\n",
			  __func__, s_secondary_image_offset, (unsigned int)num_bytes);
	}

	s_written_image_size_in_bytes += num_bytes;
	s_secondary_image_offset += num_bytes;

exit_func:
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	p_component->failure_cause = (uint32_t)rc;
	if (rc != FW_UPDATE_RC_OK) {
		p_cancel(image_id, &s_st_nvm);
		if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
			rc = FW_UPDATE_RC_SET_NVM_ERR;
		}
	}

	return rc;
}

int32_t fw_update_finish(uint32_t image_id)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(image_id=%d) - Enter\n", __func__, image_id);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		s_is_secondary_offset_set = false;
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		s_is_secondary_offset_set = false;
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	if ((s_st_nvm.data.sw_update.state != FW_UPDATE_STATE_WRITING) ||
	    (p_component->state != FW_UPDATE_STATE_WRITING)) {
		rc = FW_UPDATE_RC_FINISH_OPCODE_BAD_STATE;
		goto exit_func;
	}

	p_component->state = FW_UPDATE_STATE_CANDIDATE;
	s_is_secondary_offset_set = false;
	(void)fw_update_platform_otf_enable();

exit_func:
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	p_component->failure_cause = (uint32_t)rc;
	if (rc != FW_UPDATE_RC_OK) {
		p_cancel(image_id, &s_st_nvm);
		s_is_secondary_offset_set = false;
	}

	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(written image size=0x%08X bytes) - Exit rc=0x%08X\n",
		  __func__, s_written_image_size_in_bytes, (uint32_t)rc);
	return rc;
}

int32_t fw_update_install(uint32_t auto_reset)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;
	uint32_t otp_spk_slot = 0U;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(auto_reset=%d) - Enter\n", __func__, auto_reset);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (s_st_nvm.data.sw_update.state != FW_UPDATE_STATE_WRITING) {
		rc = FW_UPDATE_RC_INSTALL_OPCODE_BAD_STATE;
		s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_FAILED;
		goto exit_func;
	}

	rc = p_dependency_check();
	if (rc != FW_UPDATE_RC_OK) {
		LOG_ERROR(LOG_MOD_FW_UPDATE, "%s() - dependency check failure\n", __func__);
		s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_FAILED;
		goto exit_func;
	}

	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_CANDIDATE;
	if (s_st_nvm.data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		s_st_nvm.data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
		p_component = &s_st_nvm.data.sw_update.components[i];
		if (p_component->state == FW_UPDATE_STATE_CANDIDATE) {
			if (i == FW_UPDATE_IMG_TYPE_SPK) {
				if (fw_update_platform_get_boot_partition(&otp_spk_slot) != 0) {
					rc = FW_UPDATE_RC_SPK_UPDATE_INTENDED_SPK_ERR;
					s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_FAILED;
					goto exit_func;
				}

				if (otp_spk_slot != p_component->secondary_slot) {
					if (fw_update_platform_update_boot_partition_indicator() != 0) {
						rc = FW_UPDATE_RC_SPK_UPDATE_INTENDED_SPK_ERR;
						s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_FAILED;
						goto exit_func;
					}
				}
			} else if (i == FW_UPDATE_IMG_TYPE_APBL) {
				s_st_nvm.data.apbl_slot = p_component->secondary_slot;
			}

			p_component->state = FW_UPDATE_STATE_STAGED;
			p_swap_slots(i);
		}
	}

	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_STAGED;
	ARG_UNUSED(auto_reset);

exit_func:
	if (rc != FW_UPDATE_RC_OK) {
		for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
			p_component = &s_st_nvm.data.sw_update.components[i];
			if (p_component->state == FW_UPDATE_STATE_CANDIDATE) {
				p_cancel(i, &s_st_nvm);
				p_component->failure_cause = (uint32_t)rc;
			}
		}
	}

	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_cancel(uint32_t image_id)
{
	int32_t rc;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(image_id=%d) - Enter\n", __func__, image_id);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (p_get_component(image_id) == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	p_cancel(image_id, &s_st_nvm);
	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	} else {
		rc = FW_UPDATE_RC_OK;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_reboot(void)
{
	int32_t rc;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	s_st_nvm.data.sw_update.reset_cause = FW_UPDATE_M55_SW_RST_CAUSE;
	s_st_nvm.data.sw_update.failure_cause = FW_UPDATE_RC_OK;
	rc = nvm_set_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	} else {
		rc = FW_UPDATE_RC_OK;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_accept(void)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (s_st_nvm.data.sw_update.state != FW_UPDATE_STATE_TRIAL) {
		rc = FW_UPDATE_RC_ACCEPT_OPCODE_BAD_STATE;
		goto exit_func;
	}

	p_set_primary_slot_functional();
	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_UPDATED;

	if (s_st_nvm.data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		s_st_nvm.data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
		p_component = &s_st_nvm.data.sw_update.components[i];
		if (p_component->state == FW_UPDATE_STATE_UPDATED) {
#if !FW_UPDATE_POST_ACCEPT_CLEAN
			(void)fw_update_clean(i);
#endif
			if ((i == FW_UPDATE_IMG_TYPE_SPK) || (i == FW_UPDATE_IMG_TYPE_APBL) ||
			    (i == FW_UPDATE_IMG_TYPE_SDK)) {
				if (fw_update_platform_update_rollback_counter(i) != 0) {
					rc = FW_UPDATE_RC_SPK_UPDATE_ROLLBACK_COUNTER_ERR;
					goto exit_func;
				}
			}
		}
	}

exit_func:
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_reject(uint32_t auto_reset)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(auto_reset=%d) - Enter\n", __func__, auto_reset);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	if (s_st_nvm.data.sw_update.state != FW_UPDATE_STATE_TRIAL) {
		rc = FW_UPDATE_RC_REJECT_OPCODE_BAD_STATE;
		goto exit_func;
	}

	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_REJECTED;
	if (s_st_nvm.data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		s_st_nvm.data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
		p_component = &s_st_nvm.data.sw_update.components[i];
		if (p_component->state == FW_UPDATE_STATE_TRIAL) {
			p_component->state = FW_UPDATE_STATE_REJECTED;
			p_swap_slots(i);
			p_restore_primary_slot_metadata(p_component);
			p_clear_secondary_slot_metadata(p_component);
		}
	}

#if !FW_UPDATE_POST_REJECT_CLEAN
	for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
		p_component = &s_st_nvm.data.sw_update.components[i];
		if (p_component->state == FW_UPDATE_STATE_REJECTED) {
			(void)fw_update_clean(i);
		}
	}
#endif

exit_func:
	ARG_UNUSED(auto_reset);
	s_st_nvm.data.sw_update.reset_cause = 0U;
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_clean(uint32_t image_id)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(image_id=%d) - Enter\n", __func__, image_id);

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	if ((p_component->state != FW_UPDATE_STATE_FAILED) &&
	    (p_component->state != FW_UPDATE_STATE_UPDATED) &&
	    (p_component->state != FW_UPDATE_STATE_REJECTED)) {
		rc = FW_UPDATE_RC_CLEAN_OPCODE_BAD_STATE;
		goto exit_func;
	}

	p_component->state = FW_UPDATE_STATE_READY;
	p_component->failure_cause = FW_UPDATE_RC_OK;
	p_restore_primary_slot_metadata(p_component);
	p_clear_secondary_slot_metadata(p_component);
	s_st_nvm.data.sw_update.state = FW_UPDATE_STATE_READY;
	s_st_nvm.data.sw_update.reset_cause = 0U;
	s_written_image_size_in_bytes = 0U;
	s_secondary_image_offset = 0U;
	s_is_secondary_offset_set = false;

exit_func:
	s_st_nvm.data.sw_update.failure_cause = (uint32_t)rc;
	if (nvm_set_data(&s_st_nvm) != NVM_RC_OK) {
		rc = FW_UPDATE_RC_SET_NVM_ERR;
	}

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)rc);
	return rc;
}

int32_t fw_update_get_info(st_fw_update_info *p_st_info)
{
	int32_t rc;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	if (p_st_info == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	memcpy(&s_st_info.fw_version, ver_num, VER_NUM_SIZE_IN_BYTES);
	s_st_info.write_max_num_bytes = WRITE_MAX_NUM_BYTES;
	memcpy(p_st_info, &s_st_info, sizeof(*p_st_info));

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Exit rc=0x%08X\n", __func__, (uint32_t)FW_UPDATE_RC_OK);
	return FW_UPDATE_RC_OK;
}

int32_t fw_update_get_state(uint32_t *p_state)
{
	int32_t rc;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	if (p_state == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	*p_state = s_st_nvm.data.sw_update.state;
	p_dump_fw_update();

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(state=0x%08X) - Exit rc=0x%08X\n",
		  __func__, *p_state, (uint32_t)FW_UPDATE_RC_OK);
	return FW_UPDATE_RC_OK;
}

int32_t fw_update_get_component_state(uint32_t image_id, uint32_t *p_state)
{
	int32_t rc;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	if (p_state == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	*p_state = p_component->state;
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(state=0x%08X) - Exit rc=0x%08X\n",
		  __func__, *p_state, (uint32_t)FW_UPDATE_RC_OK);
	return FW_UPDATE_RC_OK;
}

int32_t fw_update_get_failure(uint32_t *p_failure)
{
	int32_t rc;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	if (p_failure == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	*p_failure = s_st_nvm.data.sw_update.failure_cause;
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(failure_cause=0x%08X) - Exit rc=0x%08X\n",
		  __func__, *p_failure, (uint32_t)FW_UPDATE_RC_OK);
	return FW_UPDATE_RC_OK;
}

int32_t fw_update_get_component_failure(uint32_t image_id, uint32_t *p_failure)
{
	int32_t rc;
	st_nvm_fw_update_component *p_component;

	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s() - Enter\n", __func__);

	if (p_failure == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = p_get_component(image_id);
	if (p_component == NULL) {
		return FW_UPDATE_RC_NOT_SUPPORTED;
	}

	*p_failure = p_component->failure_cause;
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "%s(failure_cause=0x%08X) - Exit rc=0x%08X\n",
		  __func__, *p_failure, (uint32_t)FW_UPDATE_RC_OK);
	return FW_UPDATE_RC_OK;
}

__attribute__((weak)) uint32_t fw_update_app_verify(void)
{
	return FW_UPDATE_RC_OK;
}

__attribute__((weak)) uint32_t fw_post_uart(void)
{
	return FW_UPDATE_RC_OK;
}

int32_t fw_update_post(void)
{
	int32_t rc = FW_UPDATE_RC_OK;
	st_nvm_fw_update_component *p_component;

	rc = nvm_get_data(&s_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	p_component = &s_st_nvm.data.sw_update.components[FW_UPDATE_IMG_TYPE_SDK];
	if (p_component->state == FW_UPDATE_STATE_TRIAL) {
		rc = p_fw_post(&s_st_nvm);
		if (rc != FW_UPDATE_RC_OK) {
			return rc;
		}

		rc = (int32_t)fw_update_app_verify();
	}

	return rc;
}

static uint32_t p_calc_crc(uint32_t crc, uint16_t size, uint8_t *p_buf)
{
	int i;

	crc ^= 0xFFFFFFFFU;
	for (i = 0; i < size; i++) {
		crc ^= p_buf[i];
		for (int j = 0; j < 8; ++j) {
			if ((crc & 1U) != 0U) {
				crc = (crc >> 1) ^ FW_UPDATE_CRC32_POLY;
			} else {
				crc >>= 1;
			}
		}
	}

	return crc ^ 0xFFFFFFFFU;
}

static int32_t p_dependency_check(void)
{
	return FW_UPDATE_RC_OK;
}

static bool p_is_otf_managed_offset(uint32_t offset, uint32_t size)
{
	const st_nvm_security *security = &s_st_nvm.data.security;
	uint32_t start = offset;
	uint32_t end = offset + size - 1U;
	const uint32_t *section_words;
	uint32_t sections;

	if (size == 0U) {
		return false;
	}

	sections = security->num_of_defined_sections;
	if (sections > 8U) {
		sections = 8U;
	}

	section_words = (const uint32_t *)security;
	section_words++;

	for (uint32_t i = 0U; i < sections; i++) {
		uint32_t ctl = section_words[0];
		uint32_t key = section_words[1];
		uint32_t section_start = section_words[2];
		uint32_t section_end = section_words[3];
		uint32_t crypto_offset = section_words[4];

		ARG_UNUSED(ctl);
		ARG_UNUSED(key);

		if ((crypto_offset != 0U) && (crypto_offset != UINT32_MAX) &&
		    (start <= section_end) && (end >= section_start)) {
			return true;
		}

		section_words += 5;
	}

	return false;
}

static void p_update_secondary_info(uint32_t image_id)
{
	st_nvm_fw_update_component *p_component = &s_st_nvm.data.sw_update.components[image_id];
	st_nvm_image_slot *p_secondary_slot;

	if (p_component->num_slots > FW_UPDATE_MAX_SLOTS) {
		p_component->num_slots = FW_UPDATE_MAX_SLOTS;
	}

	if (p_component->num_slots > 1U) {
		if (p_component->primary_slot == 0U) {
			p_component->secondary_slot = 1U;
		} else {
			p_component->secondary_slot = 0U;
		}
	} else {
		p_component->secondary_slot = 0U;
	}

	p_secondary_slot = &p_component->slots[p_component->secondary_slot];
	p_secondary_slot->image_is_bootable = FW_UPDATE_IMG_NOT_BOOTABLE;
	p_secondary_slot->image_is_functional = FW_UPDATE_IMG_NOT_FUNCTIONAL;
}

static void p_swap_slots(uint32_t image_id)
{
	st_nvm_fw_update_component *p_component = &s_st_nvm.data.sw_update.components[image_id];
	uint32_t tmp_slot = p_component->primary_slot;

	p_component->primary_slot = p_component->secondary_slot;
	p_component->secondary_slot = tmp_slot;
}

static void p_set_primary_slot_functional(void)
{
	st_nvm_fw_update_component *p_component;
	st_nvm_image_slot *p_slot;

	for (uint32_t i = 0U; i < FW_UPDATE_MAX_COMPONENTS; i++) {
		if ((i == FW_UPDATE_IMG_TYPE_SDK) || (i == FW_UPDATE_IMG_TYPE_MODEL) ||
		    (i == FW_UPDATE_IMG_TYPE_SPK) || (i == FW_UPDATE_IMG_TYPE_APBL)) {
			p_component = &s_st_nvm.data.sw_update.components[i];
			p_slot = &p_component->slots[p_component->primary_slot];
			if (p_component->state == FW_UPDATE_STATE_TRIAL) {
				p_component->state = FW_UPDATE_STATE_UPDATED;
				if ((p_slot->image_is_bootable == FW_UPDATE_IMG_BOOTABLE) &&
				    (p_slot->image_is_functional == FW_UPDATE_IMG_NOT_FUNCTIONAL)) {
					p_slot->image_is_functional = FW_UPDATE_IMG_FUNCTIONAL;
				}
			}
		}
	}
}

static void p_restore_primary_slot_metadata(st_nvm_fw_update_component *p_component)
{
	st_nvm_image_slot *p_slot;

	if ((p_component == NULL) || (p_component->num_slots == 0U) ||
	    (p_component->primary_slot >= p_component->num_slots)) {
		return;
	}

	p_slot = &p_component->slots[p_component->primary_slot];
	p_slot->image_is_bootable = FW_UPDATE_IMG_BOOTABLE;
	p_slot->image_is_functional = FW_UPDATE_IMG_FUNCTIONAL;
}

static void p_clear_secondary_slot_metadata(st_nvm_fw_update_component *p_component)
{
	st_nvm_image_slot *p_slot;

	if ((p_component == NULL) || (p_component->num_slots <= 1U) ||
	    (p_component->secondary_slot >= p_component->num_slots)) {
		return;
	}

	p_slot = &p_component->slots[p_component->secondary_slot];
	p_slot->image_is_bootable = FW_UPDATE_IMG_NOT_BOOTABLE;
	p_slot->image_is_functional = FW_UPDATE_IMG_NOT_FUNCTIONAL;
}

static void p_dump_fw_update(void)
{
#if defined(CONFIG_SYNA_LOG_DEBUG)
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "state=%s\n", state_lookup_tbl[s_st_nvm.data.sw_update.state]);
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "reset_cause=0x%08X\n", s_st_nvm.data.sw_update.reset_cause);
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "failure_cause=0x%08X\n", s_st_nvm.data.sw_update.failure_cause);
	LOG_DEBUG(LOG_MOD_FW_UPDATE, "num_components=0x%08X\n", s_st_nvm.data.sw_update.num_components);

	if (s_st_nvm.data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
		s_st_nvm.data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
	}

	for (uint32_t i = 0U; i < s_st_nvm.data.sw_update.num_components; i++) {
		k_msleep(10);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "component[%s]:\n", image_type_lookup_tbl[i]);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tstate=%s\n",
			  state_lookup_tbl[s_st_nvm.data.sw_update.components[i].state]);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tfailure_cause=0x%08X\n",
			  s_st_nvm.data.sw_update.components[i].failure_cause);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tmax_size=0x%08X\n",
			  s_st_nvm.data.sw_update.components[i].max_size);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tnum_slots=0x%08X\n",
			  s_st_nvm.data.sw_update.components[i].num_slots);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tprimary_slot=0x%08X\n",
			  s_st_nvm.data.sw_update.components[i].primary_slot);
		LOG_DEBUG(LOG_MOD_FW_UPDATE, "\tsecondary_slot=0x%08X\n",
			  s_st_nvm.data.sw_update.components[i].secondary_slot);
	}
#endif
}

static void p_cancel(uint32_t image_id, st_nvm_fw *p_st_nvm)
{
	st_nvm_fw_update_component *p_component = &p_st_nvm->data.sw_update.components[image_id];

	if (p_component->state == FW_UPDATE_STATE_WRITING) {
		p_component->state = FW_UPDATE_STATE_FAILED;
	}

	if (p_st_nvm->data.sw_update.state == FW_UPDATE_STATE_WRITING) {
		if (p_st_nvm->data.sw_update.num_components > FW_UPDATE_MAX_COMPONENTS) {
			p_st_nvm->data.sw_update.num_components = FW_UPDATE_MAX_COMPONENTS;
		}

		for (uint32_t i = 0U; i < p_st_nvm->data.sw_update.num_components; i++) {
			p_component = &p_st_nvm->data.sw_update.components[i];
			if (p_component->state == FW_UPDATE_STATE_CANDIDATE) {
				p_component->state = FW_UPDATE_STATE_FAILED;
			}
		}
	}

	p_st_nvm->data.sw_update.state = FW_UPDATE_STATE_FAILED;
}

static int32_t p_fw_post(st_nvm_fw *p_st_nvm)
{
	int32_t rc = p_fw_post_nvm(p_st_nvm);

	if (rc != FW_UPDATE_RC_OK) {
		goto exit_func;
	}

#if FW_UPDATE_SDK_POST_UART
	rc = (int32_t)fw_post_uart();
#endif

exit_func:
	if (rc != FW_UPDATE_RC_OK) {
		p_handle_fw_post_failure(p_st_nvm);
	}

	return rc;
}

static int32_t p_fw_post_nvm(st_nvm_fw *p_st_nvm)
{
	int32_t rc;
	st_nvm_fw_update_component *p_component;
	uint32_t crc32_val_1;
	uint32_t crc32_val_2;

	p_component = &p_st_nvm->data.sw_update.components[FW_UPDATE_IMG_TYPE_SDK];
	if (p_component->state != FW_UPDATE_STATE_TRIAL) {
		return FW_UPDATE_RC_OK;
	}

	p_st_nvm->crc32 = crc32_val_1 = p_calc_crc(FW_UPDATE_CRC32_START_VAL, sizeof(p_st_nvm->data),
						      (uint8_t *)&p_st_nvm->data);
	rc = nvm_set_data(p_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_SET_NVM_ERR;
	}

	rc = nvm_get_data(p_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_GET_NVM_ERR;
	}

	crc32_val_2 = p_calc_crc(FW_UPDATE_CRC32_START_VAL, sizeof(p_st_nvm->data),
				   (uint8_t *)&p_st_nvm->data);
	if (crc32_val_1 != crc32_val_2) {
		return FW_UPDATE_RC_FW_POST_ERR;
	}

	return FW_UPDATE_RC_OK;
}

static int32_t p_handle_fw_post_failure(st_nvm_fw *p_st_nvm)
{
	int32_t rc;
	st_nvm_fw_update_component *p_component;

	LOG_ERROR(LOG_MOD_FW_UPDATE, "%s() - SDK POST failure\n", __func__);

	p_st_nvm->data.sw_update.state = FW_UPDATE_STATE_FAILED;
	p_st_nvm->data.sw_update.failure_cause = FW_UPDATE_RC_FW_POST_ERR;

	for (uint32_t i = 0U; i < p_st_nvm->data.sw_update.num_components; i++) {
		p_component = &p_st_nvm->data.sw_update.components[i];
		if (p_component->state == FW_UPDATE_STATE_TRIAL) {
			p_component->state = FW_UPDATE_STATE_FAILED;
			p_component->failure_cause = FW_UPDATE_RC_FW_POST_ERR;
			if (p_component->num_slots > 1U) {
				p_swap_slots(i);
			}
		}
	}

	rc = nvm_set_data(p_st_nvm);
	if (rc != NVM_RC_OK) {
		return FW_UPDATE_RC_SET_NVM_ERR;
	}

	return FW_UPDATE_RC_OK;
}
