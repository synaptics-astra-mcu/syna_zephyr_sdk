/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service_uc_manager.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <logger.h>

#ifndef LOG_MOD_GENERIC
#define LOG_MOD_GENERIC "GENERIC"
#endif

#define MAX_REGISTERED_USECASES 8U

enum uc_state {
	UC_STATE_REGISTERED = 0,
	UC_STATE_CREATED,
	UC_STATE_STARTED,
};

struct uc_registry_entry {
	uint8_t usecase_id;
	uc_operations_t ops;
	enum uc_state state;
	bool is_registered;
};

static struct uc_registry_entry uc_registry[MAX_REGISTERED_USECASES];
static struct k_mutex uc_registry_lock;
static bool uc_registry_initialized;

static void uc_manager_init_registry(void)
{
	if (uc_registry_initialized) {
		return;
	}

	k_mutex_init(&uc_registry_lock);
	uc_registry_initialized = true;
}

static struct uc_registry_entry *uc_manager_find_entry(uint8_t usecase_id)
{
	for (int i = 0; i < ARRAY_SIZE(uc_registry); i++) {
		if (uc_registry[i].is_registered && (uc_registry[i].usecase_id == usecase_id)) {
			return &uc_registry[i];
		}
	}

	return NULL;
}

static int uc_manager_get_usecase_id(uint8_t *input_data, uint8_t *usecase_id)
{
	uint32_t payload_len;

	if ((input_data == NULL) || (usecase_id == NULL)) {
		return UC_MANAGER_RC_ERROR;
	}

	payload_len = sys_get_le32(&input_data[4]);
	if (payload_len < sizeof(uint8_t)) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager payload too short: %u\n",
			  (unsigned int)payload_len);
		return UC_MANAGER_RC_ERROR;
	}

	*usecase_id = input_data[sizeof(h_api_header_t)];
	return UC_MANAGER_RC_OK;
}

int uc_manager_register_usecase(uint8_t usecase_id, const uc_operations_t *ops)
{
	struct uc_registry_entry *free_entry = NULL;
	int rc = UC_MANAGER_RC_ERROR;

	if (usecase_id == 0U) {
		return UC_MANAGER_RC_ERROR;
	}

	if (!uc_registry_initialized) {
		k_mutex_init(&uc_registry_lock);
		uc_registry_initialized = true;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	for (int i = 0; i < ARRAY_SIZE(uc_registry); i++) {
		if (uc_registry[i].is_registered) {
			if (uc_registry[i].usecase_id == usecase_id) {
				if (ops != NULL) {
					uc_registry[i].ops = *ops;
				} else {
					(void)memset(&uc_registry[i].ops, 0, sizeof(uc_registry[i].ops));
				}
				uc_registry[i].state = UC_STATE_REGISTERED;
				rc = UC_MANAGER_RC_OK;
				goto out;
			}
		} else if (free_entry == NULL) {
			free_entry = &uc_registry[i];
		}
	}

	if (free_entry != NULL) {
		free_entry->usecase_id = usecase_id;
		free_entry->is_registered = true;
		free_entry->state = UC_STATE_REGISTERED;
		if (ops != NULL) {
			free_entry->ops = *ops;
		} else {
			(void)memset(&free_entry->ops, 0, sizeof(free_entry->ops));
		}
		rc = UC_MANAGER_RC_OK;
	}

out:
	(void)k_mutex_unlock(&uc_registry_lock);

	if (rc == UC_MANAGER_RC_OK) {
		LOG_INFO(LOG_MOD_GENERIC, "UC manager registered usecase %u\n",
			 (unsigned int)usecase_id);
	} else {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager failed to register usecase %u\n",
			  (unsigned int)usecase_id);
	}

	return rc;
}

int uc_manager_unregister_usecase(uint8_t usecase_id)
{
	int rc = UC_MANAGER_RC_ERROR;

	uc_manager_init_registry();

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	for (int i = 0; i < ARRAY_SIZE(uc_registry); i++) {
		if (uc_registry[i].is_registered && (uc_registry[i].usecase_id == usecase_id)) {
			uc_registry[i].is_registered = false;
			(void)memset(&uc_registry[i].ops, 0, sizeof(uc_registry[i].ops));
			uc_registry[i].state = UC_STATE_REGISTERED;
			rc = UC_MANAGER_RC_OK;
			break;
		}
	}

	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}

int uc_manager_create_usecase(uint8_t id, uint8_t *input_data, uint8_t *output)
{
	struct uc_registry_entry *entry;
	uint8_t usecase_id;
	int rc = UC_MANAGER_RC_ERROR;

	ARG_UNUSED(id);

	uc_manager_init_registry();
	sys_put_le32(0U, &output[4]);

	if (uc_manager_get_usecase_id(input_data, &usecase_id) != UC_MANAGER_RC_OK) {
		return UC_MANAGER_RC_ERROR;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	entry = uc_manager_find_entry(usecase_id);
	if (entry == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager create rejected unknown usecase %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->ops.create != NULL) {
		rc = entry->ops.create();
		if (rc != UC_MANAGER_RC_OK) {
			LOG_ERROR(LOG_MOD_GENERIC, "UC manager create callback failed for %u\n",
				  (unsigned int)usecase_id);
			goto out;
		}
	}

	entry->state = UC_STATE_CREATED;
	rc = UC_MANAGER_RC_OK;
	LOG_INFO(LOG_MOD_GENERIC, "UC manager created usecase %u\n",
		 (unsigned int)usecase_id);

out:
	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}

int uc_manager_start_usecase(uint8_t id, uint8_t *input_data, uint8_t *output)
{
	struct uc_registry_entry *entry;
	uint8_t usecase_id;
	int rc = UC_MANAGER_RC_ERROR;

	ARG_UNUSED(id);

	uc_manager_init_registry();
	sys_put_le32(0U, &output[4]);

	if (uc_manager_get_usecase_id(input_data, &usecase_id) != UC_MANAGER_RC_OK) {
		return UC_MANAGER_RC_ERROR;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	entry = uc_manager_find_entry(usecase_id);
	if (entry == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager start rejected unknown usecase %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->state == UC_STATE_REGISTERED) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager start rejected usecase %u before create\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->ops.start != NULL) {
		rc = entry->ops.start();
		if (rc != UC_MANAGER_RC_OK) {
			LOG_ERROR(LOG_MOD_GENERIC, "UC manager start callback failed for %u\n",
				  (unsigned int)usecase_id);
			goto out;
		}
	}

	entry->state = UC_STATE_STARTED;
	rc = UC_MANAGER_RC_OK;
	LOG_INFO(LOG_MOD_GENERIC, "UC manager started usecase %u\n",
		 (unsigned int)usecase_id);

out:
	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}

int uc_manager_stop_usecase(uint8_t id, uint8_t *input_data, uint8_t *output)
{
	struct uc_registry_entry *entry;
	uint8_t usecase_id;
	int rc = UC_MANAGER_RC_ERROR;

	ARG_UNUSED(id);

	uc_manager_init_registry();
	sys_put_le32(0U, &output[4]);

	if (uc_manager_get_usecase_id(input_data, &usecase_id) != UC_MANAGER_RC_OK) {
		return UC_MANAGER_RC_ERROR;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	entry = uc_manager_find_entry(usecase_id);
	if (entry == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager stop rejected unknown usecase %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->state != UC_STATE_STARTED) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager stop rejected usecase %u before start\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->ops.stop != NULL) {
		rc = entry->ops.stop();
		if (rc != UC_MANAGER_RC_OK) {
			LOG_ERROR(LOG_MOD_GENERIC, "UC manager stop callback failed for %u\n",
				  (unsigned int)usecase_id);
			goto out;
		}
	}

	entry->state = UC_STATE_CREATED;
	rc = UC_MANAGER_RC_OK;
	LOG_INFO(LOG_MOD_GENERIC, "UC manager stopped usecase %u\n",
		 (unsigned int)usecase_id);

out:
	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}

int uc_manager_resume_usecase(uint8_t id, uint8_t *input_data, uint8_t *output)
{
	struct uc_registry_entry *entry;
	uint8_t usecase_id;
	int rc = UC_MANAGER_RC_ERROR;

	ARG_UNUSED(id);

	uc_manager_init_registry();
	sys_put_le32(0U, &output[4]);

	if (uc_manager_get_usecase_id(input_data, &usecase_id) != UC_MANAGER_RC_OK) {
		return UC_MANAGER_RC_ERROR;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	entry = uc_manager_find_entry(usecase_id);
	if (entry == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager resume rejected unknown usecase %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->state == UC_STATE_REGISTERED) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager resume rejected usecase %u before create\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->ops.resume == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager resume rejected usecase %u without resume callback\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	rc = entry->ops.resume();
	if (rc != UC_MANAGER_RC_OK) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager resume callback failed for %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	entry->state = UC_STATE_STARTED;
	rc = UC_MANAGER_RC_OK;
	LOG_INFO(LOG_MOD_GENERIC, "UC manager resumed usecase %u\n",
		 (unsigned int)usecase_id);

out:
	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}

int uc_manager_kill_usecase(uint8_t id, uint8_t *input_data, uint8_t *output)
{
	struct uc_registry_entry *entry;
	uint8_t usecase_id;
	int rc = UC_MANAGER_RC_ERROR;

	ARG_UNUSED(id);

	uc_manager_init_registry();
	sys_put_le32(0U, &output[4]);

	if (uc_manager_get_usecase_id(input_data, &usecase_id) != UC_MANAGER_RC_OK) {
		return UC_MANAGER_RC_ERROR;
	}

	(void)k_mutex_lock(&uc_registry_lock, K_FOREVER);

	entry = uc_manager_find_entry(usecase_id);
	if (entry == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager kill rejected unknown usecase %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	if (entry->ops.kill == NULL) {
		LOG_ERROR(LOG_MOD_GENERIC,
			  "UC manager kill rejected usecase %u without kill callback\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	rc = entry->ops.kill();
	if (rc != UC_MANAGER_RC_OK) {
		LOG_ERROR(LOG_MOD_GENERIC, "UC manager kill callback failed for %u\n",
			  (unsigned int)usecase_id);
		goto out;
	}

	entry->state = UC_STATE_REGISTERED;
	rc = UC_MANAGER_RC_OK;
	LOG_INFO(LOG_MOD_GENERIC, "UC manager killed usecase %u\n",
		 (unsigned int)usecase_id);

out:
	(void)k_mutex_unlock(&uc_registry_lock);
	return rc;
}
