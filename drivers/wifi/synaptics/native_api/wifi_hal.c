/*
 * Copyright (c) 2025 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_hal.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(syna_wifi, CONFIG_WIFI_LOG_LEVEL);
static const struct device *const sdhc_dev = DEVICE_DT_GET(DT_ALIAS(sdhc1));
static uint8_t syna_wifi_buffer[8192 + 1024] __aligned(256);

K_THREAD_STACK_DEFINE(syna_wifi_thread_stack, CONFIG_WIFI_SYNA_TASK_STACK_SIZE);
/*
 * Verify that SD stack can initialize an SD card
 * This test must run first, to ensure the card is initialized.
 */
int sdio_init(void)
{
	syna_wifi_data_t *data = &syna_wifi_data;
	int ret;

	ret = sd_is_card_present(sdhc_dev);

	ret = sd_init(sdhc_dev, &data->card);
	if (ret) {
		rtos_printf("Card initialization failed\n");
		return -1;
	}

	/* Init SDIO functions */
	ret = sdio_init_func(&data->card, &data->sdio_func1, 1);
	if (ret) {
		rtos_printf("sdio_enable_func BACKPLANE_FUNCTION, error: %d", ret);
		return -2;
	}

	ret = sdio_init_func(&data->card, &data->sdio_func2, 2);
	if (ret) {
		rtos_printf("sdio_enable_func WLAN_FUNCTION, error: %d\n", ret);
		return -2;
	}

	if (ret) {
		rtos_printf("Can't set block size for BACKPLANE_FUNCTION, error: %d\n", ret);
		return -2;
	}

	return 0;
}

uint8_t rtos_debug_buffer[512];
void rtos_printf(const char *format, ...)
{
	va_list vl;
	int ret;
	uint32_t time = k_uptime_get();

	memset(rtos_debug_buffer, 0, sizeof(rtos_debug_buffer));

	sprintf(rtos_debug_buffer, "%.8d.%.3d:", time / 1000, time % 1000 );

	va_start(vl, format);
	ret = vsnprintf(&rtos_debug_buffer[strlen(rtos_debug_buffer)], sizeof(rtos_debug_buffer) - 16, format, vl);
	va_end(vl);

	//LOG_ERR("%s\n", rtos_debug_buffer);
	printk("%s", rtos_debug_buffer);
}

/**
 * OOB interrupt handlers
 */
int host_enable_oob_interrupt(void)
{
	return 0;
}

int host_disable_oob_interrupt(void)
{
	return 0;
}

static void syna_gpio_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* TODO: Check if this is really needed */
	/* (void)gpio_pin_interrupt_configure_dt(&config->wifi_host_wake_gpio, GPIO_INT_DISABLE); */
	mhd_thread_notify_irq();
}

/**
 * Data trasfer functions
 */
static struct sdio_func *syna_wifi_get_sdio_func(struct sd_card *sd, int function)
{
	syna_wifi_data_t *data = CONTAINER_OF(sd, syna_wifi_data_t, card);
	struct sdio_func *func[] = {&sd->func0, &data->sdio_func1, &data->sdio_func2};

	if (function > 2) {
		return NULL;
	}

	return func[function];
}

int mhd_bus_sdio_cmd52(int direction, int function,
				uint32_t address, uint8_t value,
				int response_expected, uint8_t *response)
{
	syna_wifi_data_t *drv_data = &syna_wifi_data;
	struct sd_card *sd = &drv_data->card;
	struct sdio_func *func = syna_wifi_get_sdio_func(sd, function);
	int ret;

	if (func == NULL) {
		return -1;
	}

	if (direction) {
		//set out NULL to skip set rawdata flag
		ret = sdio_rw_byte(func, address, value, NULL);
	} else {
		ret = sdio_read_byte(func, address, &syna_wifi_buffer[0]);
	}

	if (response != NULL) {
		*response = syna_wifi_buffer[0];
	}

	if (ret < 0) {
		rtos_printf("mhd_bus_sdio_cmd52: %d, dir: %s", ret,
			direction ? "write" : "read");
		return -2;
	}

	return 0;
}
int mhd_bus_sdio_cmd53(int direction, int function,
				int mode, uint32_t address, uint16_t data_size,
				uint8_t *data, int response_expected,
				uint32_t *response)
{
	syna_wifi_data_t *drv_data = &syna_wifi_data;
	struct sd_card *sd = &drv_data->card;
	struct sdio_func *func = syna_wifi_get_sdio_func(sd, function);
	int ret;
	int len = data_size;

	if (func == NULL) {
		return -1;
	}

	len = ((len + 3) & ~3);
	if (function == 2) {
		int bs = func->block_size;
		if ((len > bs) && (len % bs))
			len = (len / bs + 1) * bs;
	}

	if (function == 2)
		k_usleep(10);

	memset(&syna_wifi_buffer[0], 1, sizeof(syna_wifi_buffer));
	if (direction) {
		memcpy(&syna_wifi_buffer[0], data, data_size);
		ret = sdio_write_addr(func, address, &syna_wifi_buffer[0], len);
	} else {
		ret = sdio_read_addr(func, address, &syna_wifi_buffer[0], len);
		memcpy(data, &syna_wifi_buffer[0], data_size);
	}

	if (ret < 0) {
		rtos_printf("mhd_bus_sdio_cmd53: %d, dir: %s\n", ret,
			direction ? "write" : "read");
		return -2;
	}

	return 0;
}

int host_platform_sdio_enumerate(void)
{
	return 0;
}

int sdio_set_f1_block_size(unsigned int blksz)
{
	int ret;
	syna_wifi_data_t *data = &syna_wifi_data;
	ret = sdio_set_block_size(&data->sdio_func1, blksz);

	return ret;
}
int sdio_set_f2_block_size(unsigned int blksz)
{
	int ret;
	syna_wifi_data_t *data = &syna_wifi_data;

	ret = sdio_set_block_size(&data->sdio_func2, blksz);

	return ret;
}

void host_rtos_delay_milliseconds(uint32_t num_ms)
{
	k_msleep(num_ms);
}

uint32_t host_rtos_get_time(void)
{
	return (uint32_t)k_uptime_get();
}

/**
 * Mutex related functions
 */
int host_rtos_init_mutex(host_mutex_type_t *mutex)
{
	int ret = k_mutex_init(mutex);
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_get_mutex(host_mutex_type_t *mutex, uint32_t timeout_ms)
{
	int ret = k_mutex_lock(mutex, K_MSEC(timeout_ms));
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_set_mutex(host_mutex_type_t *mutex)
{
	int ret = k_mutex_unlock(mutex);
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_deinit_mutex(host_mutex_type_t *mutex)
{
	return host_rtos_set_mutex(mutex);
}

/**
 * Semaphore related functions
 */
int host_rtos_init_binary_semaphore(host_semaphore_type_t *semaphore)
{
	int ret = 0;

	*semaphore = malloc(sizeof(struct k_sem));

	rtos_printf("%s, %d, %p\n", __func__, __LINE__, *semaphore);
	ret = k_sem_init(*semaphore, 0, 1);
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_init_semaphore(host_semaphore_type_t *semaphore)
{
	int ret = 0;

	*semaphore = malloc(sizeof(struct k_sem));

	rtos_printf("%s, %d, %p\n", __func__, __LINE__, *semaphore);
	ret = k_sem_init(*semaphore, 0, 1);
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_get_semaphore(host_semaphore_type_t *semaphore, uint32_t timeout_ms,
				      int will_set_in_isr)
{
	int ret = 0;

	ret = k_sem_take(*semaphore, will_set_in_isr ? K_NO_WAIT : K_MSEC(timeout_ms));
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_set_semaphore(host_semaphore_type_t *semaphore, int called_from_ISR)
{
	ARG_UNUSED(called_from_ISR);

	k_sem_give(*semaphore);

	return 0;
}

int host_rtos_deinit_semaphore(host_semaphore_type_t *semaphore)
{
	rtos_printf("%s, %d\n", __func__, __LINE__);

	k_sem_reset(*semaphore);
	free(*semaphore);

	return 0;
}

int host_rtos_create_thread(host_thread_type_t *thread, int (*entry_function)(void *, void *, void *),
				      const char *name, void *stack, uint32_t stack_size,
				      uint32_t priority)
{
	k_tid_t tid;

	*thread = malloc(sizeof(struct k_thread));
	if (*thread == NULL) {
		return -1;
	}

	memset(*thread, 0, sizeof(struct k_thread));
	rtos_printf("%s, %d, %p, %p, %d\n", __func__, __LINE__, *thread, stack, stack_size);
	tid = k_thread_create(*thread, stack, stack_size, (k_thread_entry_t)entry_function, NULL,
			      NULL, NULL, priority, 0, K_NO_WAIT);
	if (!tid) {
		return -1;
	}

	k_thread_name_set(tid, name);

	return 0;
}

int host_rtos_delete_terminated_thread(host_thread_type_t *thread)
{
	/* Nothing to delete */
	return 0;
}

int host_rtos_join_thread(host_thread_type_t *thread)
{
	int ret;

	ret = k_thread_join(thread, K_MSEC(100));
	if (ret < 0) {
		return -1;
	}

	return 0;
}

int host_rtos_finish_thread(host_thread_type_t *thread)
{
	k_thread_abort(thread);

	return 0;
}

int sdio_get_state(int slot)
{
	return 0;
}

int posix_sdio_get_suspend_state(int slot)
{
	return 0;
}

void *rtos_malloc(int size)
{
	rtos_printf("%s, %d\n", __func__, size);
	return malloc(size);
}

void rtos_free(void *p)
{
	rtos_printf("%s, %d\n", __func__, __LINE__);
	free(p);
}

void host_rtos_get_mhdtask_settings(uint32_t *priority, void **stack, uint32_t *stack_size)
{
	rtos_printf("%s, %d\n", __func__, __LINE__);
	/* TODO */
	*priority = CONFIG_WIFI_SYNA_TASK_PRIO;
	*stack_size = CONFIG_WIFI_SYNA_TASK_STACK_SIZE;
	*stack = &syna_wifi_thread_stack;
}

