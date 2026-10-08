/*
 * Copyright 2026, Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_TIMEOUT_MS 1000U
#define SAMPLE_TIMEOUT K_MSEC(SAMPLE_TIMEOUT_MS)
#define SAMPLE_CLASSIC_DLC 8U
#define SAMPLE_FD_DLC 15U
#define SAMPLE_RX_MSGQ_DEPTH 8U
#define SAMPLE_DEFAULT_CLASSIC_ID 0x123U
#define SAMPLE_DEFAULT_EXT_ID 0x001abcdeU
#define SAMPLE_TEST_STD_ID_1 0x555U
#define SAMPLE_TEST_STD_ID_2 0x565U
#define SAMPLE_TEST_STD_MASK_ID_1 0x55AU
#define SAMPLE_TEST_STD_MASK_ID_2 0x56AU
#define SAMPLE_TEST_STD_MASK 0x7F0U
#define SAMPLE_TEST_EXT_ID_1 0x15555555U
#define SAMPLE_TEST_EXT_ID_2 0x15555565U
#define SAMPLE_TEST_EXT_MASK_ID_1 0x1555555AU
#define SAMPLE_TEST_EXT_MASK_ID_2 0x1555556AU
#define SAMPLE_TEST_EXT_MASK 0x1FFFFFF0U
#define SAMPLE_TEST_FILTER_CAPACITY_LIMIT 64U
#define SAMPLE_TEST_BITRATE_1 125000U
#define SAMPLE_TEST_BITRATE_2 250000U
#define SAMPLE_TEST_BITRATE_3 1000000U
#define SAMPLE_TEST_SAMPLE_POINT 875U
#define SAMPLE_PAYLOAD_PREVIEW_BYTES 16U

#define CAN_PRIMARY_NODE DT_CHOSEN(zephyr_canbus)

#if IS_ENABLED(CONFIG_SAMPLE_SYNA_CAN_VERBOSE_LOGS)
#define sample_log_verbose(...) printk(__VA_ARGS__)
#else
#define sample_log_verbose(...) do { } while (false)
#endif

CAN_MSGQ_DEFINE(sample_rx_msgq, SAMPLE_RX_MSGQ_DEPTH);

static K_SEM_DEFINE(tx_done_sem, 0, 1);
static K_SEM_DEFINE(rx_done_sem, 0, 2);
static K_SEM_DEFINE(state_done_sem, 0, 2);

static int callback_rx_error;
static struct can_frame callback_rx_frame[2];
static uint8_t callback_rx_count;
static struct can_bus_err_cnt state_callback_err_cnt;
static enum can_state state_callback_state;
static uint8_t state_callback_count;

static const uint8_t classic_payload[SAMPLE_CLASSIC_DLC] = {
	0x53, 0x4c, 0x32, 0x36, 0x31, 0x78, 0x43, 0x41,
};

static const uint8_t fd_payload[CAN_MAX_DLEN] = {
	0x53, 0x79, 0x6e, 0x61, 0x70, 0x74, 0x69, 0x63,
	0x73, 0x20, 0x53, 0x4c, 0x20, 0x43, 0x41, 0x4e,
	0x20, 0x46, 0x44, 0x20, 0x6c, 0x6f, 0x6f, 0x70,
	0x62, 0x61, 0x63, 0x6b, 0x20, 0x74, 0x65, 0x73,
	0x74, 0x20, 0x70, 0x61, 0x79, 0x6c, 0x6f, 0x61,
	0x64, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
	0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44,
	0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
};

static const uint8_t native_payload_1[SAMPLE_CLASSIC_DLC] = {
	1, 2, 3, 4, 5, 6, 7, 8,
};

static const uint8_t native_payload_2[SAMPLE_CLASSIC_DLC] = {
	8, 7, 6, 5, 4, 3, 2, 1,
};

static void tx_callback(const struct device *dev, int error, void *user_data)
{
	int *tx_error = user_data;

	ARG_UNUSED(dev);

	if (tx_error != NULL) {
		*tx_error = error;
	}
	k_sem_give(&tx_done_sem);
}

// static bool can_device_supports_fd(const struct device *dev)
// {
// 	can_mode_t caps;
// 	int ret;

// 	ret = can_get_capabilities(dev, &caps);
// 	if (ret != 0) {
// 		return false;
// 	}

// 	return (caps & CAN_MODE_FD) != 0U;
// }

static can_mode_t sample_get_run_mode(const struct device *dev)
{
	can_mode_t caps;
	can_mode_t mode = CAN_MODE_LOOPBACK;
	int ret;

	ret = can_get_capabilities(dev, &caps);
	if (ret != 0) {
		return mode;
	}

	if ((caps & CAN_MODE_FD) != 0U) {
		mode |= CAN_MODE_FD;
	}

	return mode;
}

static void rx_callback(const struct device *dev, struct can_frame *frame, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (callback_rx_count >= ARRAY_SIZE(callback_rx_frame)) {
		callback_rx_error = -ENOSPC;
	} else {
		callback_rx_frame[callback_rx_count] = *frame;
		callback_rx_count++;
	}

	k_sem_give(&rx_done_sem);
}

static const char *state_name(enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return "error-active";
	case CAN_STATE_ERROR_WARNING:
		return "error-warning";
	case CAN_STATE_ERROR_PASSIVE:
		return "error-passive";
	case CAN_STATE_BUS_OFF:
		return "bus-off";
	case CAN_STATE_STOPPED:
		return "stopped";
	default:
		return "unknown";
	}
}

static void state_change_callback(const struct device *dev, enum can_state state,
				  struct can_bus_err_cnt err_cnt, void *user_data)
{
	ARG_UNUSED(user_data);

	state_callback_state = state;
	state_callback_err_cnt = err_cnt;
	state_callback_count++;
	k_sem_give(&state_done_sem);

	printk("%s state changed: %s, rx_err=%u, tx_err=%u\n", dev->name,
	       state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
}

static void print_separator(const char *title)
{
	if (IS_ENABLED(CONFIG_SAMPLE_SYNA_CAN_VERBOSE_LOGS)) {
		printk("\n============================================================\n");
		printk("[TEST] %s\n", title);
		printk("============================================================\n");
	} else {
		printk("\n[TEST] %s\n", title);
	}
}

static void log_step(const char *fmt, ...)
{
	va_list args;

	printk("  - ");
	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static const char *ret_name(int ret)
{
	switch (ret) {
	case 0:
		return "OK";
	case -EAGAIN:
		return "-EAGAIN";
	case -EINVAL:
		return "-EINVAL";
	case -ENOTSUP:
		return "-ENOTSUP";
	case -EALREADY:
		return "-EALREADY";
	case -EBUSY:
		return "-EBUSY";
	case -ENETDOWN:
		return "-ENETDOWN";
	case -ENOSPC:
		return "-ENOSPC";
	case -ENOSYS:
		return "-ENOSYS";
	case -ETIMEDOUT:
		return "-ETIMEDOUT";
	case -EIO:
		return "-EIO";
	case -ENODEV:
		return "-ENODEV";
	default:
		return "UNKNOWN";
	}
}

static const char *ret_action(int ret)
{
	switch (ret) {
	case 0:
		return "Done";
	case -EAGAIN:
		return "Timed out; no frame/data available";
	case -EINVAL:
		return "Rejected invalid input";
	case -ENOTSUP:
		return "Rejected unsupported operation";
	case -EALREADY:
		return "Already in the requested state";
	case -EBUSY:
		return "Rejected because controller is busy/running";
	case -ENETDOWN:
		return "Rejected because controller is stopped";
	case -ENOSPC:
		return "No free slot available";
	case -ENOSYS:
		return "Operation is not implemented";
	case -ETIMEDOUT:
		return "Timed out";
	case -EIO:
		return "I/O validation failed";
	case -ENODEV:
		return "Device is not ready";
	default:
		return "Unexpected result";
	}
}

static void log_ret(const char *label, int ret)
{
	log_step("%s: %s\n", label, ret_action(ret));
}

static void log_pass(const char *label)
{
	printk("[PASS] %s\n", label);
}

static void log_payload(const char *prefix, const uint8_t *data, uint8_t len)
{
	uint8_t shown = MIN(len, SAMPLE_PAYLOAD_PREVIEW_BYTES);

	if (len == 0U) {
		printk("  - %s payload[0]: <empty>\n", prefix);
		return;
	}

	printk("  - %s payload[%u]:", prefix, len);
	for (uint8_t i = 0U; i < shown; i++) {
		printk(" %02x", data[i]);
	}
	if (shown < len) {
		printk(" ...");
	}
	printk("\n");
}

static const char *frame_type_name(const struct can_frame *frame)
{
	if ((frame->flags & CAN_FRAME_FDF) != 0U) {
		return "fd";
	}

	if ((frame->flags & CAN_FRAME_RTR) != 0U) {
		return "classic-rtr";
	}

	return "classic";
}

static const char *frame_id_name(const struct can_frame *frame)
{
	return (frame->flags & CAN_FRAME_IDE) != 0U ? "extended" : "standard";
}

static void log_frame(const char *prefix, const struct can_frame *frame)
{
	uint8_t bytes = can_dlc_to_bytes(frame->dlc);

	printk("  - %s: id=0x%08x (%s) type=%s dlc=%u bytes=%u flags=0x%x "
	       "rtr=%u fdf=%u brs=%u esi=%u\n",
	       prefix, frame->id, frame_id_name(frame), frame_type_name(frame),
	       frame->dlc, bytes, frame->flags,
	       (frame->flags & CAN_FRAME_RTR) != 0U,
	       (frame->flags & CAN_FRAME_FDF) != 0U,
	       (frame->flags & CAN_FRAME_BRS) != 0U,
	       (frame->flags & CAN_FRAME_ESI) != 0U);

	if ((frame->flags & CAN_FRAME_RTR) == 0U) {
		log_payload(prefix, frame->data, bytes);
	}
}

static void log_filter(const struct device *dev, int filter_id,
		       const struct can_filter *filter)
{
	printk("  - RX filter: dev=%s handle=%d id=0x%08x mask=0x%08x flags=0x%x "
	       "id_type=%s\n",
	       dev->name, filter_id, filter->id, filter->mask, filter->flags,
	       (filter->flags & CAN_FILTER_IDE) != 0U ? "extended" : "standard");
}

static void log_device_state(const struct device *dev, const char *label)
{
	enum can_state state;
	struct can_bus_err_cnt err_cnt;
	int ret;

	ret = can_get_state(dev, &state, &err_cnt);
	if (ret != 0) {
		printk("%s %s state read failed: %d\n", dev->name, label, ret);
		return;
	}

	printk("%s %s state: %s, rx_err=%u, tx_err=%u\n",
	       dev->name, label, state_name(state),
	       err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
}

static void log_device_caps(const struct device *dev)
{
	can_mode_t caps;
	uint32_t core_clock;
	int ret;

	ret = can_get_capabilities(dev, &caps);
	if (ret == 0) {
		printk("%s capabilities: 0x%x loopback=%u fd=%u listen-only=%u\n",
		       dev->name, caps,
		       (caps & CAN_MODE_LOOPBACK) != 0U,
		       (caps & CAN_MODE_FD) != 0U,
		       (caps & CAN_MODE_LISTENONLY) != 0U);
	} else {
		printk("%s capabilities read failed: %d\n", dev->name, ret);
	}

	ret = can_get_core_clock(dev, &core_clock);
	if (ret == 0) {
		printk("%s core clock: %u Hz, bitrate range: %u..%u bps\n",
		       dev->name, core_clock,
		       can_get_bitrate_min(dev), can_get_bitrate_max(dev));
	} else {
		printk("%s core clock read failed: %d\n", dev->name, ret);
	}
}

static void fill_payload(struct can_frame *frame, const uint8_t *payload)
{
	memcpy(frame->data, payload, can_dlc_to_bytes(frame->dlc));
}

static bool frame_payload_matches(const struct can_frame *actual, const struct can_frame *expected)
{
	uint8_t len = can_dlc_to_bytes(expected->dlc);

	return memcmp(actual->data, expected->data, len) == 0;
}

static bool frame_matches(const struct can_frame *actual, const struct can_frame *expected)
{
	uint32_t match_flags = CAN_FRAME_IDE | CAN_FRAME_RTR | CAN_FRAME_FDF |
			       CAN_FRAME_BRS | CAN_FRAME_ESI;

	if (actual->id != expected->id) {
		printk("ID mismatch: got 0x%x expected 0x%x\n", actual->id, expected->id);
		return false;
	}

	if ((actual->flags & match_flags) != (expected->flags & match_flags)) {
		printk("flag mismatch: got 0x%x expected 0x%x\n",
		       actual->flags & match_flags, expected->flags & match_flags);
		return false;
	}

	if (actual->dlc != expected->dlc) {
		printk("DLC mismatch: got %u expected %u\n", actual->dlc, expected->dlc);
		return false;
	}

	if ((expected->flags & CAN_FRAME_RTR) != 0U) {
		return true;
	}

	if (!frame_payload_matches(actual, expected)) {
		uint8_t len = can_dlc_to_bytes(expected->dlc);

		printk("payload mismatch for ID 0x%x\n", expected->id);
		log_payload("expected", expected->data, len);
		log_payload("actual", actual->data, len);
		return false;
	}

	return true;
}

static bool frame_matches_with_mask(const struct can_frame *actual,
				    const struct can_frame *expected,
				    uint32_t id_mask)
{
	uint32_t match_flags = CAN_FRAME_IDE | CAN_FRAME_RTR | CAN_FRAME_FDF |
			       CAN_FRAME_BRS | CAN_FRAME_ESI;

	if (((actual->id ^ expected->id) & id_mask) != 0U) {
		printk("ID mismatch: got 0x%x expected 0x%x mask 0x%x\n",
		       actual->id, expected->id, id_mask);
		return false;
	}

	if ((actual->flags & match_flags) != (expected->flags & match_flags)) {
		printk("flag mismatch: got 0x%x expected 0x%x\n",
		       actual->flags & match_flags, expected->flags & match_flags);
		return false;
	}

	if (actual->dlc != expected->dlc) {
		printk("DLC mismatch: got %u expected %u\n", actual->dlc, expected->dlc);
		return false;
	}

	if ((expected->flags & CAN_FRAME_RTR) != 0U) {
		return true;
	}

	if (!frame_payload_matches(actual, expected)) {
		uint8_t len = can_dlc_to_bytes(expected->dlc);

		printk("payload mismatch for ID 0x%x\n", expected->id);
		log_payload("expected", expected->data, len);
		log_payload("actual", actual->data, len);
		return false;
	}

	return true;
}

static int send_and_expect(const struct device *tx_dev, const struct can_frame *frame,
			   const char *label)
{
	struct can_frame rx_frame;
	int tx_error = 0;
	int ret;

	print_separator(label);
	log_step("TX device=%s timeout=%u ms\n", tx_dev->name, SAMPLE_TIMEOUT_MS);
	log_frame("TX request", frame);

	k_sem_reset(&tx_done_sem);
	k_msgq_purge(&sample_rx_msgq);
	log_step("RX queue purged\n");

	ret = can_send(tx_dev, frame, SAMPLE_TIMEOUT, tx_callback, &tx_error);
	if (ret != 0) {
		printk("%s: can_send failed: %d\n", label, ret);
		return ret;
	}
	log_step("can_send accepted, waiting for TX callback\n");

	ret = k_sem_take(&tx_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: TX completion timeout\n", label);
		log_device_state(tx_dev, "after TX timeout");
		return -ETIMEDOUT;
	}
	log_step("TX callback completed, error=%d\n", tx_error);

	if (tx_error != 0) {
		printk("%s: TX callback error: %d\n", label, tx_error);
		log_device_state(tx_dev, "after TX error");
		return tx_error;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: RX timeout\n", label);
		printk("%s: TX completed but no RX frame arrived before timeout\n", label);
		log_device_state(tx_dev, "TX device after RX timeout");
		return -ETIMEDOUT;
	}
	log_frame("RX frame", &rx_frame);

	if (!frame_matches(&rx_frame, frame)) {
		log_device_state(tx_dev, "after RX validation failure");
		return -EIO;
	}

	printk("[PASS] %s: id=0x%x dlc=%u flags=0x%x\n",
	       label, rx_frame.id, rx_frame.dlc, rx_frame.flags);

	return 0;
}

static int configure_device(const struct device *dev, can_mode_t *run_mode)
{
	can_mode_t caps;
	can_mode_t mode;
	int ret;

	if (!device_is_ready(dev)) {
		printk("%s is not ready\n", dev->name);
		return -ENODEV;
	}

	ret = can_get_capabilities(dev, &caps);
	if (ret != 0) {
		printk("%s: can_get_capabilities failed: %d\n", dev->name, ret);
		return ret;
	}

	if ((caps & CAN_MODE_LOOPBACK) == 0U) {
		printk("%s: loopback is not supported\n", dev->name);
		return -ENOTSUP;
	}

	mode = CAN_MODE_LOOPBACK;

	if ((caps & CAN_MODE_FD) != 0U) {
		mode |= CAN_MODE_FD;
	}

	log_device_caps(dev);

	printk("%s configuring: mode=0x%x loopback=1 fd=%u\n",
	       dev->name, mode, (mode & CAN_MODE_FD) != 0U);

	ret = can_set_mode(dev, mode);
	if (ret != 0) {
		printk("%s: can_set_mode(0x%x) failed: %d\n",
		       dev->name, mode, ret);
		return ret;
	}

	can_set_state_change_callback(dev, state_change_callback, NULL);

	ret = can_start(dev);
	if (ret != 0) {
		printk("%s: can_start failed: %d\n", dev->name, ret);
		return ret;
	}

	if (run_mode != NULL) {
		*run_mode = mode;
	}

	log_device_state(dev, "started");
	return 0;
}

static void prepare_frame(struct can_frame *frame, uint32_t id, uint8_t flags,
			  const uint8_t *payload)
{
	memset(frame, 0, sizeof(*frame));
	frame->id = id;
	frame->flags = flags;
	frame->dlc = SAMPLE_CLASSIC_DLC;
	fill_payload(frame, payload);
}

static int wait_for_tx(const struct device *dev, const char *label, int *tx_error)
{
	int ret;

	ret = k_sem_take(&tx_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: TX callback timeout\n", label);
		log_device_state(dev, "after TX callback timeout");
		return -ETIMEDOUT;
	}

	if (*tx_error != 0) {
		printk("%s: TX callback error: %d\n", label, *tx_error);
		log_device_state(dev, "after TX callback error");
		return *tx_error;
	}

	return 0;
}

static int send_frame_wait_tx(const struct device *dev, const struct can_frame *frame,
			      const char *label)
{
	int tx_error = 0;
	int ret;

	k_sem_reset(&tx_done_sem);

	ret = can_send(dev, frame, SAMPLE_TIMEOUT, tx_callback, &tx_error);
	if (ret != 0) {
		printk("%s: can_send failed: %d\n", label, ret);
		return ret;
	}

	return wait_for_tx(dev, label, &tx_error);
}

static int null_callback_send_test(const struct device *dev,
				   const struct can_filter *filter,
				   const struct can_frame *frame)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API NULL TX callback send");
	log_step("send valid frame with callback=NULL and user_data=NULL\n");
	log_frame("TX request", frame);
	k_msgq_purge(&sample_rx_msgq);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("NULL TX callback: can_add_rx_filter_msgq failed: %d\n",
		       filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = can_send(dev, frame, SAMPLE_TIMEOUT, NULL, NULL);
	log_ret("can_send(NULL callback)", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	can_remove_rx_filter(dev, filter_id);
	if (ret != 0) {
		printk("NULL TX callback: RX timeout after accepted send\n");
		return -ETIMEDOUT;
	}

	log_frame("RX frame", &rx_frame);
	if (!frame_matches(&rx_frame, frame)) {
		return -EIO;
	}

	log_step("valid frame was accepted without a TX callback and looped back\n");
	log_pass("Zephyr API NULL TX callback send");
	return 0;
}

static int msgq_filter_transfer(const struct device *dev, const struct can_filter *filter,
				const struct can_frame *frame, uint32_t id_mask,
				const char *label)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator(label);
	log_step("RX path=message queue depth=%u timeout=%u ms id_mask=0x%08x\n",
		 SAMPLE_RX_MSGQ_DEPTH, SAMPLE_TIMEOUT_MS,
		 id_mask);
	log_frame("TX request", frame);

	k_msgq_purge(&sample_rx_msgq);
	log_step("RX queue purged\n");
	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("%s: can_add_rx_filter_msgq failed: %d\n", label, filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_frame_wait_tx(dev, frame, label);
	if (ret != 0) {
		goto out;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: RX msgq timeout\n", label);
		ret = -ETIMEDOUT;
		goto out;
	}

	log_frame("RX msgq frame", &rx_frame);
	if (!frame_matches_with_mask(&rx_frame, frame, id_mask)) {
		ret = -EIO;
		goto out;
	}

	log_pass(label);
	ret = 0;

out:
	can_remove_rx_filter(dev, filter_id);
	return ret;
}

static int callback_filter_transfer(const struct device *dev, const struct can_filter *filter_1,
				    const struct can_filter *filter_2,
				    const struct can_frame *frame_1,
				    const struct can_frame *frame_2,
				    uint32_t id_mask,
				    const char *label)
{
	int filter_id_1;
	int filter_id_2;
	int ret;

	print_separator(label);
	log_step("RX path=callback filters=2 timeout=%u ms id_mask=0x%08x\n",
		 SAMPLE_TIMEOUT_MS, id_mask);
	k_sem_reset(&rx_done_sem);
	callback_rx_error = 0;
	callback_rx_count = 0U;

	filter_id_1 = can_add_rx_filter(dev, rx_callback, NULL, filter_1);
	if (filter_id_1 < 0) {
		printk("%s: first can_add_rx_filter failed: %d\n", label, filter_id_1);
		return filter_id_1;
	}
	log_filter(dev, filter_id_1, filter_1);

	filter_id_2 = can_add_rx_filter(dev, rx_callback, NULL, filter_2);
	if (filter_id_2 < 0) {
		printk("%s: second can_add_rx_filter failed: %d\n", label, filter_id_2);
		can_remove_rx_filter(dev, filter_id_1);
		return filter_id_2;
	}
	log_filter(dev, filter_id_2, filter_2);

	log_frame("TX request 1", frame_1);
	ret = send_frame_wait_tx(dev, frame_1, label);
	if (ret != 0) {
		goto out;
	}

	log_frame("TX request 2", frame_2);
	ret = send_frame_wait_tx(dev, frame_2, label);
	if (ret != 0) {
		goto out;
	}

	ret = k_sem_take(&rx_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: first RX callback timeout\n", label);
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = k_sem_take(&rx_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: second RX callback timeout\n", label);
		ret = -ETIMEDOUT;
		goto out;
	}

	if (callback_rx_error != 0) {
		printk("%s: RX callback overflow/error: %d\n", label, callback_rx_error);
		ret = callback_rx_error;
		goto out;
	}

	if (callback_rx_count != 2U) {
		printk("%s: RX callback count mismatch: got %u expected 2\n",
		       label, callback_rx_count);
		ret = -EIO;
		goto out;
	}
	log_step("RX callback count=%u expected=2\n", callback_rx_count);

	log_frame("RX callback frame 1", &callback_rx_frame[0]);
	log_frame("RX callback frame 2", &callback_rx_frame[1]);

	if (!frame_matches_with_mask(&callback_rx_frame[0], frame_1, id_mask) ||
	    !frame_matches_with_mask(&callback_rx_frame[1], frame_2, id_mask)) {
		ret = -EIO;
		goto out;
	}

	log_pass(label);
	ret = 0;

out:
	can_remove_rx_filter(dev, filter_id_1);
	can_remove_rx_filter(dev, filter_id_2);
	return ret;
}

static int filter_removal_test(const struct device *dev, const struct can_filter *filter,
			       const struct can_frame *frame)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API filter removal");
	k_msgq_purge(&sample_rx_msgq);
	log_step("install filter, verify one frame, remove filter, verify timeout\n");
	log_frame("TX request", frame);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("filter removal: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_frame_wait_tx(dev, frame, "filter removal before remove");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("filter removal: RX before remove timed out\n");
		can_remove_rx_filter(dev, filter_id);
		return -ETIMEDOUT;
	}
	log_frame("RX before removal", &rx_frame);

	can_remove_rx_filter(dev, filter_id);
	log_step("removed RX filter handle=%d\n", filter_id);
	k_msgq_purge(&sample_rx_msgq);

	ret = send_frame_wait_tx(dev, frame, "filter removal after remove");
	if (ret != 0) {
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, K_MSEC(100));
	if (ret == 0) {
		printk("filter removal: received frame after filter removal\n");
		log_frame("unexpected RX frame", &rx_frame);
		return -EIO;
	}
	log_step("post-removal RX timed out as expected; removed filter blocked the frame\n");

	log_pass("Zephyr API filter removal");
	return 0;
}

static int restart_controller(const struct device *dev, can_mode_t mode, const char *label)
{
	int ret;

	log_step("%s: stopping controller\n", label);
	ret = can_stop(dev);
	log_ret("can_stop()", ret);
	if (ret != 0) {
		return ret;
	}

	log_device_state(dev, "after stop");

	log_step("%s: setting mode=0x%x\n", label, mode);
	ret = can_set_mode(dev, mode);
	log_ret("can_set_mode()", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("%s: restarting controller\n", label);
	ret = can_start(dev);
	log_ret("can_start()", ret);
	if (ret != 0) {
		return ret;
	}

	log_device_state(dev, "after restart");
	return 0;
}

static int send_and_receive_on_installed_filter(const struct device *dev,
						const struct can_frame *frame,
						const char *label)
{
	struct can_frame rx_frame;
	int ret;

	k_msgq_purge(&sample_rx_msgq);
	log_frame("TX request", frame);

	ret = send_frame_wait_tx(dev, frame, label);
	if (ret != 0) {
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: RX timeout\n", label);
		return -ETIMEDOUT;
	}

	log_frame("RX frame", &rx_frame);
	if (!frame_matches(&rx_frame, frame)) {
		return -EIO;
	}

	return 0;
}

static int expect_error(const char *label, int ret, int expected)
{
	if (ret != expected) {
		printk("%s: FAIL expected \"%s\", observed \"%s\" (%s)\n",
		       label, ret_action(expected), ret_action(ret), ret_name(ret));
		return -EIO;
	}

	log_step("%s: %s as expected\n", label, ret_action(ret));
	return 0;
}

static int invalid_frame_tests(const struct device *dev)
{
	struct can_frame invalid_std_id = {
		.id = CAN_STD_ID_MASK + 1U,
	};
	struct can_frame invalid_ext_id = {
		.flags = CAN_FRAME_IDE,
		.id = CAN_EXT_ID_MASK + 1U,
	};
	struct can_frame invalid_std_dlc = {
		.id = SAMPLE_TEST_STD_ID_1,
		.dlc = CAN_MAX_DLC + 1U,
	};
	struct can_frame invalid_ext_dlc = {
		.flags = CAN_FRAME_IDE,
		.id = SAMPLE_TEST_EXT_ID_1,
		.dlc = CAN_MAX_DLC + 1U,
	};
	int ret;

	print_separator("Zephyr API invalid frame checks");

	ret = expect_error("invalid frame: NULL frame",
			   can_send(dev, NULL, SAMPLE_TIMEOUT, NULL, NULL), -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid frame: standard ID out of range",
			   can_send(dev, &invalid_std_id, SAMPLE_TIMEOUT, NULL, NULL),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid frame: extended ID out of range",
			   can_send(dev, &invalid_ext_id, SAMPLE_TIMEOUT, NULL, NULL),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid frame: standard DLC out of range",
			   can_send(dev, &invalid_std_dlc, SAMPLE_TIMEOUT, NULL, NULL),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid frame: extended DLC out of range",
			   can_send(dev, &invalid_ext_dlc, SAMPLE_TIMEOUT, NULL, NULL),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API invalid frame checks");
	return 0;
}

static int invalid_filter_tests(const struct device *dev)
{
	struct can_filter invalid_std_filter = {
		.id = CAN_STD_ID_MASK + 1U,
		.mask = CAN_STD_ID_MASK,
	};
	struct can_filter invalid_std_mask = {
		.id = CAN_STD_ID_MASK,
		.mask = CAN_STD_ID_MASK + 1U,
	};
	struct can_filter invalid_ext_filter = {
		.flags = CAN_FILTER_IDE,
		.id = CAN_EXT_ID_MASK + 1U,
		.mask = CAN_EXT_ID_MASK,
	};
	struct can_filter invalid_ext_mask = {
		.flags = CAN_FILTER_IDE,
		.id = CAN_EXT_ID_MASK,
		.mask = CAN_EXT_ID_MASK + 1U,
	};
	const struct can_filter valid_filter = {
		.id = SAMPLE_TEST_STD_ID_1,
		.mask = CAN_STD_ID_MASK,
	};
	int ret;

	print_separator("Zephyr API invalid filter checks");

	ret = expect_error("invalid filter: NULL callback",
			   can_add_rx_filter(dev, NULL, NULL, &valid_filter), -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid filter: NULL filter",
			   can_add_rx_filter(dev, rx_callback, NULL, NULL), -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid filter: standard ID out of range",
			   can_add_rx_filter(dev, rx_callback, NULL, &invalid_std_filter),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid filter: standard mask out of range",
			   can_add_rx_filter(dev, rx_callback, NULL, &invalid_std_mask),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid filter: extended ID out of range",
			   can_add_rx_filter(dev, rx_callback, NULL, &invalid_ext_filter),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("invalid filter: extended mask out of range",
			   can_add_rx_filter(dev, rx_callback, NULL, &invalid_ext_mask),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API invalid filter checks");
	return 0;
}

static int receive_timeout_test(const struct device *dev, const struct can_filter *filter)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API receive timeout");
	k_msgq_purge(&sample_rx_msgq);
	log_step("install filter and wait 100 ms without sending a frame\n");

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("receive timeout: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, K_MSEC(100));
	can_remove_rx_filter(dev, filter_id);

	if (ret == 0) {
		printk("receive timeout: received a frame without sending one\n");
		log_frame("unexpected RX frame", &rx_frame);
		return -EIO;
	}
	log_step("RX wait timed out as expected; no frame was sent\n");

	log_pass("Zephyr API receive timeout");
	return 0;
}

static int wrong_id_test(const struct device *dev, const struct can_filter *filter,
			 const struct can_frame *wrong_frame)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API wrong ID rejection");
	k_msgq_purge(&sample_rx_msgq);
	log_step("send a non-matching ID and expect RX timeout\n");
	log_frame("TX wrong-ID request", wrong_frame);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("wrong ID: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_frame_wait_tx(dev, wrong_frame, "wrong ID rejection");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, K_MSEC(100));
	can_remove_rx_filter(dev, filter_id);

	if (ret == 0) {
		printk("wrong ID: received a non-matching frame\n");
		log_frame("unexpected RX frame", &rx_frame);
		return -EIO;
	}
	log_step("wrong-ID frame was rejected; RX wait timed out as expected\n");

	log_pass("Zephyr API wrong ID rejection");
	return 0;
}

static int queue_buffering_test(const struct device *dev, const struct can_filter *filter,
				const struct can_frame *frame)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API RX message queue buffering");
	k_msgq_purge(&sample_rx_msgq);
	log_step("queue depth=%u, sending %u matching frames before draining RX\n",
		 SAMPLE_RX_MSGQ_DEPTH, SAMPLE_RX_MSGQ_DEPTH);
	log_frame("TX repeated frame", frame);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("queue buffering: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	for (uint8_t i = 0U; i < SAMPLE_RX_MSGQ_DEPTH; i++) {
		ret = send_frame_wait_tx(dev, frame, "queue buffering TX");
		if (ret != 0) {
			can_remove_rx_filter(dev, filter_id);
			return ret;
		}
	}
	log_step("queued %u frames, now reading them back\n", SAMPLE_RX_MSGQ_DEPTH);

	for (uint8_t i = 0U; i < SAMPLE_RX_MSGQ_DEPTH; i++) {
		ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
		if (ret != 0) {
			printk("queue buffering: RX timeout at index %u\n", i);
			can_remove_rx_filter(dev, filter_id);
			return -ETIMEDOUT;
		}

		if (!frame_matches(&rx_frame, frame)) {
			can_remove_rx_filter(dev, filter_id);
			return -EIO;
		}
	}
	log_step("received and validated %u queued frames\n", SAMPLE_RX_MSGQ_DEPTH);

	can_remove_rx_filter(dev, filter_id);
	log_pass("Zephyr API RX message queue buffering");
	return 0;
}

static int filter_capacity_test_one(const struct device *dev, bool extended)
{
	int filter_ids[SAMPLE_TEST_FILTER_CAPACITY_LIMIT];
	struct can_filter filter = {
		.flags = extended ? CAN_FILTER_IDE : 0U,
		.mask = extended ? CAN_EXT_ID_MASK : CAN_STD_ID_MASK,
	};
	int max_filters;
	int filter_id;
	int ret = 0;

	max_filters = can_get_max_filters(dev, extended);
	log_step("filter capacity %s: reported max_filters=%d\n",
		 extended ? "extended" : "standard", max_filters);
	if ((max_filters == -ENOSYS) || (max_filters == 0)) {
		printk("filter capacity %s: skipped, max_filters=%d\n",
		       extended ? "extended" : "standard", max_filters);
		return 0;
	}

	if ((max_filters < 0) || (max_filters > SAMPLE_TEST_FILTER_CAPACITY_LIMIT)) {
		printk("filter capacity %s: skipped, unsupported max_filters=%d\n",
		       extended ? "extended" : "standard", max_filters);
		return 0;
	}

	for (int i = 0; i < max_filters; i++) {
		filter_ids[i] = -1;
	}

	for (int i = 0; i < max_filters; i++) {
		filter.id = i + 1U;
		filter_ids[i] = can_add_rx_filter_msgq(dev, &sample_rx_msgq, &filter);
		if (filter_ids[i] < 0) {
			printk("filter capacity %s: add failed at %d: %d\n",
			       extended ? "extended" : "standard", i, filter_ids[i]);
			ret = filter_ids[i];
			goto out;
		}
	}
	log_step("filter capacity %s: installed %d filters\n",
		 extended ? "extended" : "standard", max_filters);

	filter.id++;
	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, &filter);
	if (filter_id != -ENOSPC) {
		printk("filter capacity %s: expected no free filter slots, observed \"%s\" (%s)\n",
		       extended ? "extended" : "standard",
		       ret_action(filter_id), ret_name(filter_id));
		if (filter_id >= 0) {
			can_remove_rx_filter(dev, filter_id);
		}
		ret = -EIO;
	}
	if (ret == 0) {
		log_step("filter capacity %s: extra filter rejected because no slot is available\n",
			 extended ? "extended" : "standard");
	}

out:
	for (int i = 0; i < max_filters; i++) {
		if (filter_ids[i] >= 0) {
			can_remove_rx_filter(dev, filter_ids[i]);
		}
	}

	return ret;
}

static int filter_capacity_tests(const struct device *dev)
{
	int ret;

	print_separator("Zephyr API RX filter capacity");

	ret = filter_capacity_test_one(dev, false);
	if (ret != 0) {
		return ret;
	}

	ret = filter_capacity_test_one(dev, true);
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API RX filter capacity");
	return 0;
}

static int state_api_test(const struct device *dev)
{
	struct can_bus_err_cnt err_cnt;
	enum can_state state;
	int ret;

	print_separator("Zephyr API state accessors");

	ret = can_get_state(dev, NULL, NULL);
	log_ret("can_get_state(NULL, NULL)", ret);
	if (ret != 0) {
		printk("state API: null destinations failed: %d\n", ret);
		return ret;
	}

	ret = can_get_state(dev, &state, NULL);
	log_step("can_get_state(state, NULL): %s, state=%s\n",
		 ret_action(ret), ret == 0 ? state_name(state) : "n/a");
	if (ret != 0) {
		printk("state API: state-only failed: %d\n", ret);
		return ret;
	}

	ret = can_get_state(dev, NULL, &err_cnt);
	log_step("can_get_state(NULL, err_cnt): %s, rx_err=%u tx_err=%u\n",
		 ret_action(ret), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
	if (ret != 0) {
		printk("state API: error-counter-only failed: %d\n", ret);
		return ret;
	}

	log_step("current mode=0x%x\n", can_get_mode(dev));
	log_pass("Zephyr API state accessors");
	return 0;
}

static int wait_for_state_callback(const char *label, enum can_state expected)
{
	int ret;

	ret = k_sem_take(&state_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("%s: state-change callback timeout\n", label);
		return -ETIMEDOUT;
	}

	log_step("%s: callback state=%s rx_err=%u tx_err=%u\n", label,
		 state_name(state_callback_state),
		 state_callback_err_cnt.rx_err_cnt,
		 state_callback_err_cnt.tx_err_cnt);
	if (state_callback_state != expected) {
		printk("%s: unexpected state-change callback state=%s expected=%s\n",
		       label, state_name(state_callback_state), state_name(expected));
		return -EIO;
	}

	return 0;
}

static int state_change_callback_test(const struct device *dev, can_mode_t run_mode)
{
	int ret;

	print_separator("Zephyr API state-change callback delivery");
	log_step("forcing stop/start to validate registered callback events\n");
	k_sem_reset(&state_done_sem);
	state_callback_count = 0U;

	ret = can_stop(dev);
	log_ret("can_stop()", ret);
	if (ret != 0) {
		return ret;
	}

	ret = wait_for_state_callback("state callback after stop", CAN_STATE_STOPPED);
	if (ret != 0) {
		return ret;
	}

	ret = can_set_mode(dev, run_mode);
	log_ret("can_set_mode(run mode)", ret);
	if (ret != 0) {
		return ret;
	}

	ret = can_start(dev);
	log_ret("can_start()", ret);
	if (ret != 0) {
		return ret;
	}

	ret = k_sem_take(&state_done_sem, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("state callback after start: state-change callback timeout\n");
		return -ETIMEDOUT;
	}

	log_step("state callback after start: callback state=%s rx_err=%u tx_err=%u\n",
		 state_name(state_callback_state),
		 state_callback_err_cnt.rx_err_cnt,
		 state_callback_err_cnt.tx_err_cnt);
	if (state_callback_state == CAN_STATE_STOPPED) {
		printk("state callback after start: controller stayed stopped\n");
		return -EIO;
	}

	if (state_callback_count < 2U) {
		printk("state callback: expected at least 2 events, got %u\n",
		       state_callback_count);
		return -EIO;
	}

	log_step("observed %u state-change callback events\n", state_callback_count);
	log_pass("Zephyr API state-change callback delivery");
	return 0;
}

static int timing_api_tests(const struct device *dev)
{
	struct can_timing timing = {0};
	uint32_t min;
	uint32_t max;
	int ret;

	print_separator("Zephyr API timing checks");

	min = can_get_bitrate_min(dev);
	max = can_get_bitrate_max(dev);
	log_step("bitrate range=%u..%u bps\n", min, max);
	if (min > max) {
		printk("timing: invalid bitrate range %u..%u\n", min, max);
		return -EIO;
	}

	ret = can_calc_timing(dev, &timing, SAMPLE_TEST_BITRATE_1, 1000U);
	log_step("can_calc_timing(%u bps, sample_point=1000): %s\n",
		 SAMPLE_TEST_BITRATE_1, ret_action(ret));
	if (ret != -EINVAL) {
		printk("timing: invalid sample point accepted: %d\n", ret);
		return -EIO;
	}

	ret = can_calc_timing(dev, &timing, SAMPLE_TEST_BITRATE_1, SAMPLE_TEST_SAMPLE_POINT);
	log_step("can_calc_timing(%u bps, sample_point=%u): %s, "
		 "sjw=%u prop=%u phase1=%u phase2=%u prescaler=%u\n",
		 SAMPLE_TEST_BITRATE_1, SAMPLE_TEST_SAMPLE_POINT, ret_action(ret),
		 timing.sjw, timing.prop_seg, timing.phase_seg1,
		 timing.phase_seg2, timing.prescaler);
	if (ret < 0) {
		printk("timing: can_calc_timing failed: %d\n", ret);
		return ret;
	}

	ret = expect_error("timing: set bitrate while started",
			   can_set_bitrate(dev, SAMPLE_TEST_BITRATE_2), -EBUSY);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("timing: set timing while started",
			   can_set_timing(dev, &timing), -EBUSY);
	if (ret != 0) {
		return ret;
	}

	ret = can_calc_timing_data(dev, &timing, SAMPLE_TEST_BITRATE_3, 1000U);
	log_step("can_calc_timing_data(%u bps, sample_point=1000) %s\n",
		 SAMPLE_TEST_BITRATE_3, ret_action(ret));
	if (ret != -EINVAL) {
		printk("timing: invalid data sample point accepted: %d\n", ret);
		return -EIO;
	}

	ret = can_calc_timing_data(dev, &timing, SAMPLE_TEST_BITRATE_3,
				   SAMPLE_TEST_SAMPLE_POINT);
	log_step("can_calc_timing_data(%u bps, sample_point=%u) %s, "
		 "sjw=%u prop=%u phase1=%u phase2=%u prescaler=%u\n",
		 SAMPLE_TEST_BITRATE_3, SAMPLE_TEST_SAMPLE_POINT, ret_action(ret),
		 timing.sjw, timing.prop_seg, timing.phase_seg1,
		 timing.phase_seg2, timing.prescaler);
	if (ret < 0) {
		printk("timing: can_calc_timing_data failed: %d\n", ret);
		return ret;
	}

	ret = expect_error("timing: set data bitrate while started",
			   can_set_bitrate_data(dev, SAMPLE_TEST_BITRATE_3), -EBUSY);
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("timing: set data timing while started",
			   can_set_timing_data(dev, &timing), -EBUSY);
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API timing checks");
	return 0;
}

static int timing_mutation_tests(const struct device *dev, can_mode_t run_mode)
{
	const struct can_timing *timing_min;
	const struct can_timing *timing_max;
	uint32_t bitrate_min;
	uint32_t bitrate_max;
	int ret;

	print_separator("Zephyr API stopped-state bitrate/timing mutation");

	log_step("stopping controller before bitrate/timing changes\n");
	ret = can_stop(dev);
	log_ret("can_stop()", ret);
	if (ret != 0) {
		return ret;
	}
	log_device_state(dev, "timing mutation stopped");

	bitrate_min = can_get_bitrate_min(dev);
	bitrate_max = can_get_bitrate_max(dev);
	timing_min = can_get_timing_min(dev);
	timing_max = can_get_timing_max(dev);
	log_step("bitrate range=%u..%u default=%u\n",
		 bitrate_min, bitrate_max, CONFIG_CAN_DEFAULT_BITRATE);

	if (bitrate_min > 0U) {
		ret = expect_error("timing mutation: bitrate too low",
				   can_set_bitrate(dev, bitrate_min - 1U), -ENOTSUP);
		if (ret != 0) {
			return ret;
		}
	}

	if (bitrate_max < UINT32_MAX) {
		ret = expect_error("timing mutation: bitrate too high",
				   can_set_bitrate(dev, bitrate_max + 1U), -ENOTSUP);
		if (ret != 0) {
			return ret;
		}
	}

	log_step("setting bitrate=%u\n", SAMPLE_TEST_BITRATE_2);
	ret = can_set_bitrate(dev, SAMPLE_TEST_BITRATE_2);
	log_step("can_set_bitrate(%u): %s\n",
		 SAMPLE_TEST_BITRATE_2, ret_action(ret));
	if (ret != 0) {
		return ret;
	}

	log_step("restoring default bitrate=%u\n", CONFIG_CAN_DEFAULT_BITRATE);
	ret = can_set_bitrate(dev, CONFIG_CAN_DEFAULT_BITRATE);
	log_ret("can_set_bitrate(default)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("setting timing_min=%p\n", timing_min);
	ret = can_set_timing(dev, timing_min);
	log_ret("can_set_timing(min)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("setting timing_max=%p\n", timing_max);
	ret = can_set_timing(dev, timing_max);
	log_ret("can_set_timing(max)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("restoring default bitrate=%u after timing min/max\n",
		 CONFIG_CAN_DEFAULT_BITRATE);
	ret = can_set_bitrate(dev, CONFIG_CAN_DEFAULT_BITRATE);
	log_ret("can_set_bitrate(default)", ret);
	if (ret != 0) {
		return ret;
	}

	const struct can_timing *timing_data_min = can_get_timing_data_min(dev);
	const struct can_timing *timing_data_max = can_get_timing_data_max(dev);

	if (bitrate_min > 0U) {
		ret = expect_error("timing mutation: data bitrate too low",
				   can_set_bitrate_data(dev, bitrate_min - 1U),
				   -ENOTSUP);
		if (ret != 0) {
			return ret;
		}
	}

	if (bitrate_max < UINT32_MAX) {
		ret = expect_error("timing mutation: data bitrate too high",
				   can_set_bitrate_data(dev, bitrate_max + 1U),
				   -ENOTSUP);
		if (ret != 0) {
			return ret;
		}
	}

	log_step("setting data timing_min=%p\n", timing_data_min);
	ret = can_set_timing_data(dev, timing_data_min);
	log_ret("can_set_timing_data(min)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("setting data timing_max=%p\n", timing_data_max);
	ret = can_set_timing_data(dev, timing_data_max);
	log_ret("can_set_timing_data(max)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("restoring default data bitrate=%u\n",
		 CONFIG_CAN_DEFAULT_BITRATE_DATA);
	ret = can_set_bitrate_data(dev, CONFIG_CAN_DEFAULT_BITRATE_DATA);
	log_ret("can_set_bitrate_data(default)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("restoring run mode=0x%x\n", run_mode);
	ret = can_set_mode(dev, run_mode);
	log_ret("can_set_mode(restore)", ret);
	if (ret != 0) {
		return ret;
	}

	log_step("restarting controller after timing mutation\n");
	ret = can_start(dev);
	log_ret("can_start()", ret);
	if (ret != 0) {
		return ret;
	}

	log_device_state(dev, "timing mutation restarted");
	log_pass("Zephyr API stopped-state bitrate/timing mutation");
	return 0;
}

static int classic_mode_fd_rejection_test(const struct device *dev, can_mode_t run_mode)
{
	struct can_frame fd_frame = {
		.id = SAMPLE_TEST_STD_ID_1,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
		.dlc = SAMPLE_FD_DLC,
	};
	can_mode_t classic_mode = run_mode & ~CAN_MODE_FD;
	int ret;

	print_separator("Zephyr API classic-mode CAN FD rejection");
	fill_payload(&fd_frame, fd_payload);
	log_frame("FD frame rejected in classic mode", &fd_frame);

	ret = restart_controller(dev, classic_mode, "classic-mode FD rejection");
	if (ret != 0) {
		return ret;
	}

	ret = expect_error("classic-mode FD rejection: can_send(FDF)",
			   can_send(dev, &fd_frame, SAMPLE_TIMEOUT, NULL, NULL), -ENOTSUP);
	if (ret != 0) {
		return ret;
	}

	ret = restart_controller(dev, run_mode, "classic-mode FD rejection restore");
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API classic-mode CAN FD rejection");
	return 0;
}

static int fd_classic_filter_transition_test(const struct device *dev, can_mode_t run_mode,
					     const struct can_filter *filter,
					     const struct can_frame *frame)
{
	can_mode_t classic_mode = run_mode & ~CAN_MODE_FD;
	bool restore_run_mode = false;
	int filter_id;
	int ret;

	print_separator("Zephyr API filter preservation across FD/classic modes");
	log_step("install one RX filter and keep it across FD-to-classic and "
		 "classic-to-FD restarts\n");
	k_msgq_purge(&sample_rx_msgq);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("FD/classic filter transition: add filter failed: %d\n",
		       filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_and_receive_on_installed_filter(dev, frame,
						   "FD/classic transition: initial FD mode");
	if (ret != 0) {
		goto out;
	}

	ret = restart_controller(dev, classic_mode, "FD-to-classic transition");
	if (ret != 0) {
		goto out;
	}
	restore_run_mode = true;

	ret = send_and_receive_on_installed_filter(dev, frame,
						   "FD/classic transition: classic mode");
	if (ret != 0) {
		goto out;
	}

	ret = restart_controller(dev, run_mode, "classic-to-FD transition");
	if (ret != 0) {
		goto out;
	}
	restore_run_mode = false;

	ret = send_and_receive_on_installed_filter(dev, frame,
						   "FD/classic transition: restored FD mode");
	if (ret != 0) {
		goto out;
	}

	log_step("same RX filter worked before, during, and after FD mode changes\n");
	log_pass("Zephyr API filter preservation across FD/classic modes");

out:
	if (restore_run_mode) {
		int restore_ret;

		restore_ret = restart_controller(dev, run_mode,
						 "FD/classic transition restore");
		if (ret == 0) {
			ret = restore_ret;
		}
	}

	can_remove_rx_filter(dev, filter_id);
	return ret;
}

static int rtr_rejection_test(const struct device *dev, const struct can_filter *filter,
			      const struct can_frame *rtr_frame, const char *label)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	if (IS_ENABLED(CONFIG_CAN_ACCEPT_RTR)) {
		return 0;
	}

	print_separator(label);
	log_step("CONFIG_CAN_ACCEPT_RTR=n, matching RTR frame must not reach RX filter\n");
	log_frame("TX RTR request", rtr_frame);
	k_msgq_purge(&sample_rx_msgq);

	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("RTR rejection: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_frame_wait_tx(dev, rtr_frame, "RTR rejection TX");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, K_MSEC(100));
	can_remove_rx_filter(dev, filter_id);
	if (ret == 0) {
		printk("RTR rejection: received RTR frame while CONFIG_CAN_ACCEPT_RTR=n\n");
		log_frame("unexpected RX frame", &rx_frame);
		return -EIO;
	}

	log_step("RTR frame was blocked; RX wait timed out as expected\n");
	log_pass(label);
	return 0;
}

static int stop_start_tests(const struct device *dev, can_mode_t run_mode,
			    const struct can_filter *filter, const struct can_frame *frame)
{
	struct can_frame rx_frame;
	int filter_id;
	int ret;

	print_separator("Zephyr API stop/restart and stopped-state checks");

	log_step("checking can_start() while already started\n");
	ret = can_start(dev);
	ret = expect_error("start/stop: start while started", ret, -EALREADY);
	if (ret != 0) {
		return ret;
	}

	log_step("checking can_set_mode() while started\n");
	ret = can_set_mode(dev, CAN_MODE_NORMAL);
	ret = expect_error("start/stop: set mode while started", ret, -EBUSY);
	if (ret != 0) {
		return ret;
	}

	log_step("installing filter before mode-change restart\n");
	k_msgq_purge(&sample_rx_msgq);
	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("stop/restart: can_add_rx_filter_msgq failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = send_and_receive_on_installed_filter(dev, frame,
						   "stop/restart before stop");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	log_step("stopping controller with filter installed\n");
	ret = can_stop(dev);
	log_ret("can_stop()", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = can_get_state(dev, NULL, NULL);
	log_step("can_get_state(NULL, NULL) while stopped: %s\n", ret_action(ret));
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	log_step("checking stopped state\n");
	ret = can_get_state(dev, NULL, NULL);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}
	log_device_state(dev, "stopped");

	log_step("checking can_stop() while already stopped\n");
	ret = can_stop(dev);
	ret = expect_error("stop/restart: stop while stopped", ret, -EALREADY);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	log_step("checking can_send() while stopped\n");
	ret = expect_error("stop/restart: send while stopped",
			   can_send(dev, frame, SAMPLE_TIMEOUT, NULL, NULL), -ENETDOWN);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	log_step("changing mode while stopped: normal then run mode 0x%x\n", run_mode);
	ret = can_set_mode(dev, CAN_MODE_NORMAL);
	log_ret("can_set_mode(normal)", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = can_set_mode(dev, run_mode);
	log_ret("can_set_mode(run mode)", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	log_step("restarting controller, pre-stop filter should still work\n");
	ret = can_start(dev);
	log_ret("can_start()", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}
	log_device_state(dev, "restarted");

	ret = send_and_receive_on_installed_filter(dev, frame,
						   "stop/restart after restart");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}
	can_remove_rx_filter(dev, filter_id);

	log_step("stopping again to add a filter while stopped\n");
	ret = can_stop(dev);
	log_ret("can_stop()", ret);
	if (ret != 0) {
		return ret;
	}

	k_msgq_purge(&sample_rx_msgq);
	filter_id = can_add_rx_filter_msgq(dev, &sample_rx_msgq, filter);
	if (filter_id < 0) {
		printk("stop/restart: add filter while stopped failed: %d\n", filter_id);
		return filter_id;
	}
	log_filter(dev, filter_id, filter);

	ret = can_set_mode(dev, run_mode);
	log_ret("can_set_mode(run mode before restart)", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = can_start(dev);
	log_ret("can_start() with stopped-state filter", ret);
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = send_frame_wait_tx(dev, frame, "filter added while stopped");
	if (ret != 0) {
		can_remove_rx_filter(dev, filter_id);
		return ret;
	}

	ret = k_msgq_get(&sample_rx_msgq, &rx_frame, SAMPLE_TIMEOUT);
	if (ret != 0) {
		printk("stop/restart: RX timeout after adding filter while stopped\n");
		can_remove_rx_filter(dev, filter_id);
		return -ETIMEDOUT;
	}
	log_frame("RX after stopped-state filter", &rx_frame);
	can_remove_rx_filter(dev, filter_id);

	if (!frame_matches(&rx_frame, frame)) {
		return -EIO;
	}

	log_pass("Zephyr API stop/restart and stopped-state checks");
	return 0;
}

static int recovery_api_test(const struct device *dev)
{
	can_mode_t caps;
	int ret;

	print_separator("Zephyr API recovery checks");

	ret = can_get_capabilities(dev, &caps);
	if (ret != 0) {
		printk("recovery: can_get_capabilities failed: %d\n", ret);
		return ret;
	}

	ret = can_recover(dev, K_NO_WAIT);
	if ((caps & CAN_MODE_MANUAL_RECOVERY) == 0U) {
		printk("recovery: manual recovery capability is not advertised\n");
		return -EIO;
	}

	if (ret != -ENOTSUP) {
		printk("recovery: expected unsupported outside manual recovery mode, "
		       "observed \"%s\" (%s)\n",
		       ret_action(ret), ret_name(ret));
		return -EIO;
	}

	log_pass("Zephyr API recovery checks");
	return 0;
}

static int fd_api_tests(const struct device *dev, const struct can_filter *filter_1,
			const struct can_filter *filter_2, const struct can_frame *classic_frame)
{
	struct can_frame fd_frame_1 = {
		.id = SAMPLE_TEST_STD_ID_1,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
		.dlc = SAMPLE_FD_DLC,
	};
	struct can_frame fd_frame_2 = {
		.id = SAMPLE_TEST_STD_ID_2,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
		.dlc = SAMPLE_FD_DLC,
	};
	struct can_frame invalid_fd_dlc = {
		.id = SAMPLE_TEST_STD_ID_1,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
		.dlc = CANFD_MAX_DLC + 1U,
	};
	struct can_frame fd_esi_frame = {
		.id = SAMPLE_TEST_STD_ID_1,
		.flags = CAN_FRAME_FDF | CAN_FRAME_ESI,
		.dlc = SAMPLE_FD_DLC,
	};
	int ret;

	fill_payload(&fd_frame_1, fd_payload);
	fill_payload(&fd_frame_2, fd_payload);
	fill_payload(&fd_esi_frame, fd_payload);

	print_separator("Zephyr API CAN FD negative checks");

	ret = expect_error("CAN FD: DLC out of range",
			   can_send(dev, &invalid_fd_dlc, SAMPLE_TIMEOUT, NULL, NULL),
			   -EINVAL);
	if (ret != 0) {
		return ret;
	}

	log_pass("Zephyr API CAN FD negative checks");

	ret = msgq_filter_transfer(dev, filter_1, &fd_esi_frame, 0U,
				   "Zephyr API CAN FD ESI loopback");
	if (ret != 0) {
		return ret;
	}

	ret = callback_filter_transfer(dev, filter_1, filter_2, &fd_frame_1, &fd_frame_2,
				       0U, "Zephyr API two CAN FD callback filters");
	if (ret != 0) {
		return ret;
	}

	return callback_filter_transfer(dev, filter_1, filter_2, &fd_frame_1, classic_frame,
					0U, "Zephyr API mixed CAN FD/classic filters");
}

static int native_api_metadata_test(const struct device *dev)
{
	can_mode_t caps;
	uint32_t core_clock;
	uint32_t bitrate_min;
	uint32_t bitrate_max;
	const struct can_timing *timing_min;
	const struct can_timing *timing_max;
	int ret;

	print_separator("Zephyr API metadata/state/timing");

	ret = can_get_capabilities(dev, &caps);
	if (ret != 0) {
		printk("metadata: can_get_capabilities failed: %d\n", ret);
		return ret;
	}

	ret = can_get_core_clock(dev, &core_clock);
	if (ret != 0) {
		printk("metadata: can_get_core_clock failed: %d\n", ret);
		return ret;
	}

	bitrate_min = can_get_bitrate_min(dev);
	bitrate_max = can_get_bitrate_max(dev);
	timing_min = can_get_timing_min(dev);
	timing_max = can_get_timing_max(dev);

	log_step("caps=0x%x loopback=%u fd=%u listen-only=%u\n",
		 caps, (caps & CAN_MODE_LOOPBACK) != 0U,
		 (caps & CAN_MODE_FD) != 0U,
		 (caps & CAN_MODE_LISTENONLY) != 0U);
	log_step("core_clock=%u Hz bitrate_min=%u bitrate_max=%u\n",
		 core_clock, bitrate_min, bitrate_max);
	log_step("timing_min=%p timing_max=%p max_filters_std=%d max_filters_ext=%d\n",
		 timing_min, timing_max, can_get_max_filters(dev, false),
		 can_get_max_filters(dev, true));

	if (((caps & CAN_MODE_LOOPBACK) == 0U) || (core_clock == 0U) ||
	    (bitrate_min > bitrate_max) || (timing_min == NULL) || (timing_max == NULL)) {
		printk("metadata: invalid API result\n");
		return -EIO;
	}

	log_device_state(dev, "metadata check");
	log_pass("Zephyr API metadata/state/timing");
	return 0;
}

static int native_api_stats_test(const struct device *dev)
{
	if (!IS_ENABLED(CONFIG_CAN_STATS)) {
		return 0;
	}

#ifdef CONFIG_CAN_STATS
	print_separator("Zephyr API statistics");
	log_step("stats bit=%u bit0=%u bit1=%u stuff=%u crc=%u form=%u ack=%u "
		 "rx_overrun=%u\n",
		 can_stats_get_bit_errors(dev), can_stats_get_bit0_errors(dev),
		 can_stats_get_bit1_errors(dev), can_stats_get_stuff_errors(dev),
		 can_stats_get_crc_errors(dev), can_stats_get_form_errors(dev),
		 can_stats_get_ack_errors(dev), can_stats_get_rx_overruns(dev));
	log_pass("Zephyr API statistics");
#endif

	return 0;
}

static int native_api_coverage_tests(const struct device *dev)
{
	can_mode_t run_mode;
	struct can_frame std_frame_1;
	struct can_frame std_frame_2;
	struct can_frame std_empty_frame = {
		.id = SAMPLE_TEST_STD_ID_1,
	};
	struct can_frame std_rtr_frame = {
		.id = SAMPLE_TEST_STD_ID_1,
		.flags = CAN_FRAME_RTR,
		.dlc = 0U,
	};
	struct can_frame ext_frame_1;
	struct can_frame ext_frame_2;
	struct can_frame ext_rtr_frame = {
		.id = SAMPLE_TEST_EXT_ID_1,
		.flags = CAN_FRAME_IDE | CAN_FRAME_RTR,
		.dlc = 0U,
	};
	const struct can_filter std_filter_1 = {
		.id = SAMPLE_TEST_STD_ID_1,
		.mask = CAN_STD_ID_MASK,
	};
	const struct can_filter std_filter_2 = {
		.id = SAMPLE_TEST_STD_ID_2,
		.mask = CAN_STD_ID_MASK,
	};
	const struct can_filter std_masked_filter_1 = {
		.id = SAMPLE_TEST_STD_MASK_ID_1,
		.mask = SAMPLE_TEST_STD_MASK,
	};
	const struct can_filter std_masked_filter_2 = {
		.id = SAMPLE_TEST_STD_MASK_ID_2,
		.mask = SAMPLE_TEST_STD_MASK,
	};
	const struct can_filter ext_filter_1 = {
		.flags = CAN_FILTER_IDE,
		.id = SAMPLE_TEST_EXT_ID_1,
		.mask = CAN_EXT_ID_MASK,
	};
	const struct can_filter ext_filter_2 = {
		.flags = CAN_FILTER_IDE,
		.id = SAMPLE_TEST_EXT_ID_2,
		.mask = CAN_EXT_ID_MASK,
	};
	const struct can_filter ext_masked_filter_1 = {
		.flags = CAN_FILTER_IDE,
		.id = SAMPLE_TEST_EXT_MASK_ID_1,
		.mask = SAMPLE_TEST_EXT_MASK,
	};
	const struct can_filter ext_masked_filter_2 = {
		.flags = CAN_FILTER_IDE,
		.id = SAMPLE_TEST_EXT_MASK_ID_2,
		.mask = SAMPLE_TEST_EXT_MASK,
	};
	int ret;

	run_mode = sample_get_run_mode(dev);

	prepare_frame(&std_frame_1, SAMPLE_TEST_STD_ID_1, 0U, native_payload_1);
	prepare_frame(&std_frame_2, SAMPLE_TEST_STD_ID_2, 0U, native_payload_2);
	prepare_frame(&ext_frame_1, SAMPLE_TEST_EXT_ID_1, CAN_FRAME_IDE, native_payload_1);
	prepare_frame(&ext_frame_2, SAMPLE_TEST_EXT_ID_2, CAN_FRAME_IDE, native_payload_2);

	ret = native_api_metadata_test(dev);
	if (ret != 0) {
		return ret;
	}

	ret = state_api_test(dev);
	if (ret != 0) {
		return ret;
	}

	ret = state_change_callback_test(dev, run_mode);
	if (ret != 0) {
		return ret;
	}

	ret = receive_timeout_test(dev, &std_filter_1);
	if (ret != 0) {
		return ret;
	}

	ret = null_callback_send_test(dev, &std_filter_1, &std_frame_1);
	if (ret != 0) {
		return ret;
	}

	ret = msgq_filter_transfer(dev, &std_filter_1, &std_frame_1, 0U,
				   "Zephyr API exact standard msgq filter");
	if (ret != 0) {
		return ret;
	}

	ret = msgq_filter_transfer(dev, &std_filter_1, &std_empty_frame, 0U,
				   "Zephyr API standard no-data msgq filter");
	if (ret != 0) {
		return ret;
	}

	ret = callback_filter_transfer(dev, &std_filter_1, &std_filter_2,
				       &std_frame_1, &std_frame_2,
				       0U,
				       "Zephyr API two standard callback filters");
	if (ret != 0) {
		return ret;
	}

	ret = msgq_filter_transfer(dev, &ext_filter_1, &ext_frame_1, 0U,
				   "Zephyr API exact extended msgq filter");
	if (ret != 0) {
		return ret;
	}

	ret = callback_filter_transfer(dev, &ext_filter_1, &ext_filter_2,
				       &ext_frame_1, &ext_frame_2,
				       0U,
				       "Zephyr API two extended callback filters");
	if (ret != 0) {
		return ret;
	}

	ret = msgq_filter_transfer(dev, &std_masked_filter_1, &std_frame_1, 0x0FU,
				   "Zephyr API masked standard msgq filter");
	if (ret != 0) {
		return ret;
	}

	ret = callback_filter_transfer(dev, &std_masked_filter_1, &std_masked_filter_2,
				       &std_frame_1, &std_frame_2,
				       0x0FU,
				       "Zephyr API two masked standard callback filters");
	if (ret != 0) {
		return ret;
	}

	ret = msgq_filter_transfer(dev, &ext_masked_filter_1, &ext_frame_1,
				   0x0FU, "Zephyr API masked extended msgq filter");
	if (ret != 0) {
		return ret;
	}

	ret = callback_filter_transfer(dev, &ext_masked_filter_1,
				       &ext_masked_filter_2,
				       &ext_frame_1, &ext_frame_2,
				       0x0FU,
				       "Zephyr API two masked extended callback filters");
	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_CAN_ACCEPT_RTR)) {
		ret = msgq_filter_transfer(dev, &std_filter_1, &std_rtr_frame, 0U,
					   "Zephyr API standard RTR through data filter");
		if (ret != 0) {
			return ret;
		}

		ret = msgq_filter_transfer(dev, &ext_filter_1, &ext_rtr_frame, 0U,
					   "Zephyr API extended RTR through data filter");
		if (ret != 0) {
			return ret;
		}
	}

	ret = rtr_rejection_test(dev, &std_filter_1, &std_rtr_frame,
				 "Zephyr API standard RTR rejection");
	if (ret != 0) {
		return ret;
	}

	ret = rtr_rejection_test(dev, &ext_filter_1, &ext_rtr_frame,
				 "Zephyr API extended RTR rejection");
	if (ret != 0) {
		return ret;
	}

	ret = wrong_id_test(dev, &std_filter_1, &std_frame_2);
	if (ret != 0) {
		return ret;
	}

	ret = queue_buffering_test(dev, &std_filter_1, &std_frame_1);
	if (ret != 0) {
		return ret;
	}

	ret = filter_removal_test(dev, &std_filter_1, &std_frame_1);
	if (ret != 0) {
		return ret;
	}

	ret = invalid_frame_tests(dev);
	if (ret != 0) {
		return ret;
	}

	ret = invalid_filter_tests(dev);
	if (ret != 0) {
		return ret;
	}

	ret = filter_capacity_tests(dev);
	if (ret != 0) {
		return ret;
	}

	ret = timing_api_tests(dev);
	if (ret != 0) {
		return ret;
	}

	ret = timing_mutation_tests(dev, run_mode);
	if (ret != 0) {
		return ret;
	}

	if ((run_mode & CAN_MODE_FD) != 0U) {
		ret = classic_mode_fd_rejection_test(dev, run_mode);
		if (ret != 0) {
			return ret;
		}

		ret = fd_classic_filter_transition_test(dev, run_mode, &std_filter_1,
							&std_frame_1);
		if (ret != 0) {
			return ret;
		}

		ret = fd_api_tests(dev, &std_filter_1, &std_filter_2, &std_frame_2);
		if (ret != 0) {
			return ret;
		}
	} else {
		printk("CAN FD not supported, skipping FD API tests\n");
	}

	ret = recovery_api_test(dev);
	if (ret != 0) {
		return ret;
	}

	ret = stop_start_tests(dev, run_mode, &std_filter_1, &std_frame_1);
	if (ret != 0) {
		return ret;
	}

	ret = native_api_stats_test(dev);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static int run_tests(const struct device *tx_dev, const struct device *rx_dev)
{
	struct can_frame classic = {
		.id = SAMPLE_DEFAULT_CLASSIC_ID,
		.dlc = SAMPLE_CLASSIC_DLC,
	};
	struct can_frame classic_ext = {
		.id = SAMPLE_DEFAULT_EXT_ID,
		.dlc = SAMPLE_CLASSIC_DLC,
		.flags = CAN_FRAME_IDE,
	};
	struct can_frame rtr = {
		.id = SAMPLE_DEFAULT_CLASSIC_ID,
		.dlc = 0U,
		.flags = CAN_FRAME_RTR,
	};
	struct can_frame fd = {
		.id = SAMPLE_DEFAULT_CLASSIC_ID,
		.dlc = SAMPLE_FD_DLC,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
	};
	struct can_frame fd_ext = {
		.id = SAMPLE_DEFAULT_EXT_ID,
		.dlc = SAMPLE_FD_DLC,
		.flags = CAN_FRAME_IDE | CAN_FRAME_FDF | CAN_FRAME_BRS,
	};
	const struct can_filter accept_all_filter = {
		.id = 0U,
		.mask = 0U,
	};
	const struct can_filter accept_all_ext_filter = {
		.id = 0U,
		.mask = 0U,
		.flags = CAN_FILTER_IDE,
	};
	int filter_id;
	int ret;

	fill_payload(&classic, classic_payload);
	fill_payload(&classic_ext, classic_payload);
	fill_payload(&fd, fd_payload);
	fill_payload(&fd_ext, fd_payload);

	filter_id = can_add_rx_filter_msgq(rx_dev, &sample_rx_msgq, &accept_all_filter);
	if (filter_id < 0) {
		printk("%s: failed to add RX filter: %d\n", rx_dev->name, filter_id);
		return filter_id;
	}

	log_filter(rx_dev, filter_id, &accept_all_filter);

	ret = send_and_expect(tx_dev, &classic, "classic standard loopback");
	if (ret != 0) {
		goto out;
	}

	if (IS_ENABLED(CONFIG_CAN_ACCEPT_RTR)) {
		ret = send_and_expect(tx_dev, &rtr, "classic RTR loopback");
		if (ret != 0) {
			goto out;
		}
	}

out:
	sample_log_verbose("Removing RX filter from %s: handle=%d\n", rx_dev->name, filter_id);
	can_remove_rx_filter(rx_dev, filter_id);

	if ((ret == 0) && true) {
		k_msgq_purge(&sample_rx_msgq);
		filter_id = can_add_rx_filter_msgq(rx_dev, &sample_rx_msgq,
						   &accept_all_ext_filter);
		if (filter_id < 0) {
			printk("%s: failed to add extended RX filter: %d\n",
			       rx_dev->name, filter_id);
			return filter_id;
		}
		log_filter(rx_dev, filter_id, &accept_all_ext_filter);

		ret = send_and_expect(tx_dev, &classic_ext, "classic extended loopback");
		sample_log_verbose("Removing RX filter from %s: handle=%d\n",
				   rx_dev->name, filter_id);
		can_remove_rx_filter(rx_dev, filter_id);
	}

	if ((ret == 0) && (tx_dev == rx_dev)) {
		ret = native_api_coverage_tests(rx_dev);
	}

	if (ret != 0) {
		return ret;
	}

	k_msgq_purge(&sample_rx_msgq);
	filter_id = can_add_rx_filter_msgq(rx_dev, &sample_rx_msgq, &accept_all_filter);
	if (filter_id < 0) {
		printk("%s: failed to add RX filter for FD: %d\n", rx_dev->name, filter_id);
		return filter_id;
	}
	log_filter(rx_dev, filter_id, &accept_all_filter);

	ret = send_and_expect(tx_dev, &fd, "CAN FD standard loopback");
	sample_log_verbose("Removing RX filter from %s: handle=%d\n",
			   rx_dev->name, filter_id);
	can_remove_rx_filter(rx_dev, filter_id);

	if ((ret == 0) && true) {
		k_msgq_purge(&sample_rx_msgq);
		filter_id = can_add_rx_filter_msgq(rx_dev, &sample_rx_msgq,
						   &accept_all_ext_filter);
		if (filter_id < 0) {
			printk("%s: failed to add extended RX filter for FD: %d\n",
			       rx_dev->name, filter_id);
			return filter_id;
		}
		log_filter(rx_dev, filter_id, &accept_all_ext_filter);

		ret = send_and_expect(tx_dev, &fd_ext, "CAN FD extended loopback");
		sample_log_verbose("Removing RX filter from %s: handle=%d\n",
				   rx_dev->name, filter_id);
		can_remove_rx_filter(rx_dev, filter_id);
	}

	return ret;
}

int main(void)
{
	const struct device *primary = DEVICE_DT_GET(CAN_PRIMARY_NODE);
	const struct device *tx_dev = primary;
	const struct device *rx_dev = primary;
	can_mode_t run_mode = 0U;
	enum can_state state;
	struct can_bus_err_cnt err_cnt;
	int ret;

	printk("CAN Sample App\n");
	ret = configure_device(rx_dev, &run_mode);
	if (ret != 0) {
	printk("CAN Sample App: FAIL: configure failed %s (%s)\n",
		ret_action(ret), ret_name(ret));
		return 0;
	}
		
	printk("TX=%s RX=%s mode=0x%x loopback=%u fd=%u\n",
		tx_dev->name, rx_dev->name, run_mode,
		(run_mode & CAN_MODE_LOOPBACK) != 0U,
		(run_mode & CAN_MODE_FD) != 0U);

	ret = can_get_state(rx_dev, &state, &err_cnt);
	if (ret == 0) {
		printk("%s initial state: %s, rx_err=%u, tx_err=%u\n",
		       rx_dev->name, state_name(state), err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
	}

	ret = run_tests(tx_dev, rx_dev);
	if (ret == 0) {
		printk("CAN Sample App: PASS\n");
	} else {
		printk("CAN Sample App: FAIL: %s (%s)\n", ret_action(ret), ret_name(ret));
	}

	(void)can_stop(tx_dev);

	return 0;
}
