/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(i2s_shell, CONFIG_I2S_SHELL_LOG_LEVEL);

#if DT_HAS_CHOSEN(syna_i2s_rx) && DT_HAS_CHOSEN(syna_i2s_tx)
#define I2S_SHELL_RX_NODE DT_CHOSEN(syna_i2s_rx)
#define I2S_SHELL_TX_NODE DT_CHOSEN(syna_i2s_tx)
#define I2S_SHELL_NODE_SOURCE "chosen:syna_i2s_rx/syna_i2s_tx"
#elif DT_NODE_EXISTS(DT_NODELABEL(i2s0))
#define I2S_SHELL_RX_NODE DT_NODELABEL(i2s0)
#define I2S_SHELL_TX_NODE DT_NODELABEL(i2s0)
#define I2S_SHELL_NODE_SOURCE "nodelabel:i2s0"
#else
#error "No I2S device selected. Add /chosen syna_i2s_rx/syna_i2s_tx or i2s0."
#endif

#define I2S_SHELL_BLOCK_MS              10U
#define I2S_SHELL_CHANNELS_MAX          2U
#define I2S_SHELL_BLOCK_SIZE            CONFIG_I2S_SHELL_BLOCK_SIZE
#define I2S_SHELL_BLOCK_COUNT           CONFIG_I2S_SHELL_BLOCK_COUNT
#define I2S_SHELL_TX_PRIME_BLOCKS       MIN(6U, MAX(I2S_SHELL_BLOCK_COUNT, 1U) - 1U)
#define I2S_SHELL_TIMEOUT_MS            1000U
#define I2S_SHELL_RX_POLL_MS            5U
#define I2S_SHELL_FS_MOUNT_POINT        "/lfs"
#define I2S_SHELL_PATH_MAX              128U
#define I2S_SHELL_META_AUDIO_PATH_MAX   96U
#define I2S_SHELL_META_MAGIC            0x49325330U /* I2S0 */
#define I2S_SHELL_META_VERSION          3U
#define I2S_SHELL_WRITE_BUFFER_COUNT    CONFIG_I2S_SHELL_WRITE_BUFFER_COUNT
#define I2S_SHELL_WRITE_BUFFER_BYTES    CONFIG_I2S_SHELL_WRITE_BUFFER_BYTES
#define I2S_SHELL_LFS_WRITE_CHUNK_BYTES  8192U /* Write each 32 KB app buffer as safe 8 KB LittleFS chunks. */
#ifndef CONFIG_I2S_SHELL_RAM_BUFFER_BYTES
#define CONFIG_I2S_SHELL_RAM_BUFFER_BYTES \
	(CONFIG_I2S_SHELL_WRITE_BUFFER_COUNT * CONFIG_I2S_SHELL_WRITE_BUFFER_BYTES)
#endif
#define I2S_SHELL_RAM_BUFFER_BYTES       CONFIG_I2S_SHELL_RAM_BUFFER_BYTES
#if CONFIG_I2S_SHELL_PRINT_CAPTURE_INFO
#define I2S_PAYLOAD_PARTITION_NODE       DT_NODELABEL(i2s_payload_partition)
#define I2S_PAYLOAD_FLASH_OFFSET         DT_REG_ADDR(I2S_PAYLOAD_PARTITION_NODE)
#define I2S_PAYLOAD_FLASH_SIZE           DT_REG_SIZE(I2S_PAYLOAD_PARTITION_NODE)
#endif
#define I2S_SHELL_FILE_WRITER_PRIORITY  (CONFIG_I2S_SHELL_WORKER_PRIORITY - 1)
#define I2S_SHELL_CAPTURE_USAGE \
	"usage: i2s capture <format(i2s/left/right)> <rate> <bits> <channels> <sec> [file]"
#define I2S_SHELL_PLAY_SINE_USAGE \
	"usage: i2s play sine <format(i2s/left/right)> <rate> <bits> <channels> " \
	"<sec;0=infinite>"
#define I2S_SHELL_PLAY_USAGE \
	"usage: i2s play [file] [sec;0/full if omitted] | " \
	"i2s play sine <format(i2s/left/right)> <rate> <bits> <channels> <sec>; " \
	"no file means playback RAM"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(i2s_shell_lfs);
K_MEM_SLAB_DEFINE_STATIC(i2s_tx_slab, I2S_SHELL_BLOCK_SIZE, I2S_SHELL_BLOCK_COUNT, 32);
K_MEM_SLAB_DEFINE_STATIC(i2s_rx_slab, I2S_SHELL_BLOCK_SIZE, I2S_SHELL_BLOCK_COUNT, 32);
K_THREAD_STACK_DEFINE(i2s_shell_worker_stack, CONFIG_I2S_SHELL_WORKER_STACK_SIZE);
K_THREAD_STACK_DEFINE(i2s_shell_writer_stack, CONFIG_I2S_SHELL_WRITER_STACK_SIZE);

static const struct device *const rx_dev = DEVICE_DT_GET(I2S_SHELL_RX_NODE);
static const struct device *const tx_dev = DEVICE_DT_GET(I2S_SHELL_TX_NODE);

struct i2s_payload_meta {
	uint32_t magic;
	uint32_t version;
	uint32_t format;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t hardware_bits;
	uint32_t container_bits;
	uint32_t stored_bytes;
	char audio_file[I2S_SHELL_META_AUDIO_PATH_MAX];
};

struct i2s_shell_fs {
	struct fs_mount_t mount;
	bool mounted;
};

static struct i2s_shell_fs shell_fs = {
	.mount = {
		.type = FS_LITTLEFS,
		.fs_data = &i2s_shell_lfs,
		.storage_dev = (void *)DT_FIXED_PARTITION_ID(
			DT_NODELABEL(i2s_payload_partition)),
		.mnt_point = I2S_SHELL_FS_MOUNT_POINT,
	},
};

struct writer_item {
	uint8_t index;
	bool final;
	size_t len;
};

struct writer_ctx {
	struct k_msgq queue;
	char queue_storage[I2S_SHELL_WRITE_BUFFER_COUNT * sizeof(struct writer_item)];
	struct k_sem free_buffers;
	struct k_thread thread;
	struct fs_file_t file;
	atomic_t result;
	bool file_opened;
	size_t bytes_written;
};

struct stream_params {
	uint8_t format;
	uint32_t rate;
	uint8_t word_size;
	uint8_t channels;
	uint32_t duration_sec;
	char path[I2S_SHELL_PATH_MAX];
};

enum stream_op {
	STREAM_IDLE,
	STREAM_CAPTURE_RAM,
	STREAM_CAPTURE_FILE,
	STREAM_PLAY_RAM,
	STREAM_PLAY_FILE,
	STREAM_PLAY_SINE,
};

struct stream_runtime {
	struct k_mutex lock;
	struct k_thread thread;
	k_tid_t tid;
	enum stream_op op;
	struct stream_params params;
	bool stop;
	int last_result;
};

static uint8_t write_buffers[I2S_SHELL_WRITE_BUFFER_COUNT][I2S_SHELL_WRITE_BUFFER_BYTES]
	__aligned(8);
static uint8_t file_read_buffer[I2S_SHELL_WRITE_BUFFER_BYTES] __aligned(8);
static uint8_t scratch[2][I2S_SHELL_BLOCK_SIZE] __aligned(8);
static uint8_t ram_dump[I2S_SHELL_RAM_BUFFER_BYTES] __aligned(8);
static size_t last_ram_bytes;
static struct i2s_payload_meta last_ram_meta;

BUILD_ASSERT(I2S_SHELL_RAM_BUFFER_BYTES > 0U, "RAM buffer must be non-zero");
BUILD_ASSERT(I2S_SHELL_LFS_WRITE_CHUNK_BYTES > 0U, "LFS write chunk must be non-zero");
BUILD_ASSERT(I2S_SHELL_LFS_WRITE_CHUNK_BYTES <= I2S_SHELL_WRITE_BUFFER_BYTES,
	     "LFS write chunk must fit in write buffer");
BUILD_ASSERT((I2S_SHELL_WRITE_BUFFER_BYTES % I2S_SHELL_LFS_WRITE_CHUNK_BYTES) == 0U,
	     "write buffer must be an integer multiple of LFS chunk");

static struct writer_ctx writer;
static struct stream_runtime runtime;

static char last_audio_path[I2S_SHELL_PATH_MAX];
static char last_meta_path[I2S_SHELL_PATH_MAX];

static const char *format_name(uint8_t format);

static uint8_t word_bytes(uint8_t bits)
{
	return (bits <= 8U) ? 1U : ((bits <= 16U) ? 2U : 4U);
}

static uint8_t i2s_io_word_bytes(uint8_t bits)
{
	ARG_UNUSED(bits);
	return 4U; /* DMA-friendly 32-bit slots, including 8/16/24-bit samples. */
}

static uint8_t file_word_bytes(uint8_t bits)
{
	return (bits <= 16U) ? word_bytes(bits) : 4U;
}

/*
 * The SR100 standard I2S/left/right bus uses two physical 32-bit slots.
 * Shell channels=1 is treated as logical mono: capture stores one channel,
 * playback duplicates that channel into both physical slots.  Keeping two
 * physical slots avoids the no-data mono behavior seen with true 1-slot I2S.
 */
static uint8_t physical_channels(uint8_t channels)
{
	return (channels == 1U) ? 2U : channels;
}

static size_t block_size(uint32_t rate, uint8_t bits, uint8_t channels)
{
	uint32_t frame_bytes = physical_channels(channels) * i2s_io_word_bytes(bits);
	uint32_t frames = MAX((rate * I2S_SHELL_BLOCK_MS) / 1000U, 1U);

	return ROUND_UP((size_t)frames * frame_bytes, frame_bytes);
}


static bool target_role(void)
{
	return IS_ENABLED(CONFIG_I2S_SHELL_TARGET_ROLE);
}

static uint16_t i2s_options(void)
{
	return target_role() ?
		(I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET) :
		(I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER);
}

static bool stop_requested(void)
{
	bool stop;

	k_mutex_lock(&runtime.lock, K_FOREVER);
	stop = runtime.stop;
	k_mutex_unlock(&runtime.lock);
	return stop;
}

static void reset_stream(const struct device *dev, enum i2s_dir dir)
{
	struct i2s_config cfg = { 0 };

	(void)i2s_trigger(dev, dir, I2S_TRIGGER_DROP);
	(void)i2s_configure(dev, dir, &cfg);
}

static int fs_mount_once(void)
{
	int ret;

	if (shell_fs.mounted) {
		return 0;
	}

	ret = fs_mount(&shell_fs.mount);
	if (ret == 0) {
		shell_fs.mounted = true;
	}

	return ret;
}

static int path_validate_relative(const char *path)
{
	if ((path == NULL) || (path[0] == '\0') || (path[0] == '/') ||
	    (strstr(path, "..") != NULL) || (strstr(path, "//") != NULL)) {
		return -EINVAL;
	}
	return 0;
}

static int path_resolve(const char *relative, char *resolved, size_t size)
{
	int ret;

	ret = path_validate_relative(relative);
	if (ret != 0) {
		return ret;
	}

	ret = snprintk(resolved, size, "%s/%s", I2S_SHELL_FS_MOUNT_POINT, relative);
	return ((ret < 0) || ((size_t)ret >= size)) ? -ENAMETOOLONG : 0;
}

static bool path_is_lfs_file(const char *path)
{
	size_t mount_len = strlen(I2S_SHELL_FS_MOUNT_POINT);

	return (path != NULL) &&
	       (strncmp(path, I2S_SHELL_FS_MOUNT_POINT, mount_len) == 0) &&
	       (path[mount_len] == '/') &&
	       (path[mount_len + 1U] != '\0');
}

static int meta_path_from_audio_path(const char *audio_path, char *meta_path, size_t size)
{
	int ret = snprintk(meta_path, size, "%s.meta", audio_path);

	return ((ret < 0) || ((size_t)ret >= size)) ? -ENAMETOOLONG : 0;
}

static int mkdirs_for_file(const char *resolved)
{
	char tmp[I2S_SHELL_PATH_MAX];
	char *cursor;
	int ret;

	ret = snprintk(tmp, sizeof(tmp), "%s", resolved);
	if ((ret < 0) || ((size_t)ret >= sizeof(tmp))) {
		return -ENAMETOOLONG;
	}

	cursor = tmp + strlen(I2S_SHELL_FS_MOUNT_POINT) + 1U;
	while ((cursor = strchr(cursor, '/')) != NULL) {
		struct fs_dirent entry;

		*cursor = '\0';
		ret = fs_stat(tmp, &entry);
		if (ret == 0) {
			if (entry.type != FS_DIR_ENTRY_DIR) {
				return -ENOTDIR;
			}
		} else if (ret == -ENOENT) {
			ret = fs_mkdir(tmp);
			if ((ret != 0) && (ret != -EEXIST)) {
				return ret;
			}
		} else {
			return ret;
		}
		*cursor = '/';
		cursor++;
	}

	return 0;
}

static void unlink_if_exists(const char *path)
{
	struct fs_dirent entry;

	if (fs_stat(path, &entry) == 0) {
		(void)fs_unlink(path);
	}
}

static bool meta_format_valid(uint32_t format)
{
	switch (format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		return true;
	default:
		return false;
	}
}

static int read_meta_file(const char *meta_path, struct i2s_payload_meta *meta)
{
	struct fs_file_t f;
	ssize_t rd;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, meta_path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}

	rd = fs_read(&f, meta, sizeof(*meta));
	ret = fs_close(&f);
	if (ret != 0) {
		return ret;
	}

	if (rd != (ssize_t)sizeof(*meta)) {
		return -EIO;
	}

	if ((meta->magic != I2S_SHELL_META_MAGIC) ||
	    (meta->version != I2S_SHELL_META_VERSION) ||
	    !meta_format_valid(meta->format) ||
	    (meta->sample_rate == 0U) || (meta->channels == 0U) ||
	    (meta->container_bits == 0U) || (meta->audio_file[0] == '\0')) {
		return -EINVAL;
	}

	return 0;
}

static int write_meta_file(const char *meta_path,
			   const char *audio_path,
			   uint8_t format,
			   uint32_t rate,
			   uint8_t bits,
			   uint8_t channels,
			   size_t stored_bytes)
{
	if (stored_bytes > UINT32_MAX) {
		return -EOVERFLOW;
	}

	struct i2s_payload_meta meta = {
		.magic = I2S_SHELL_META_MAGIC,
		.version = I2S_SHELL_META_VERSION,
		.format = format,
		.sample_rate = rate,
		.channels = channels,
		.hardware_bits = bits,
		.container_bits = file_word_bytes(bits) * 8U,
		.stored_bytes = (uint32_t)stored_bytes,
	};
	struct fs_file_t f;
	ssize_t wr;
	int ret;

	ret = snprintk(meta.audio_file, sizeof(meta.audio_file), "%s", audio_path);
	if ((ret < 0) || ((size_t)ret >= sizeof(meta.audio_file))) {
		return -ENAMETOOLONG;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, meta_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}

	wr = fs_write(&f, &meta, sizeof(meta));
	ret = fs_close(&f);
	if (ret != 0) {
		return ret;
	}

	return (wr == (ssize_t)sizeof(meta)) ? 0 : ((wr < 0) ? (int)wr : -EIO);
}

#if CONFIG_I2S_SHELL_PRINT_CAPTURE_INFO
static const char *raw_format_name(uint8_t bits)
{
	switch (file_word_bytes(bits)) {
	case 1U:
		return "u8";
	case 2U:
		return "s16le";
	case 4U:
		return "s32le";
	default:
		return "unknown";
	}
}

static void log_capture_info(const char *audio_path, const char *meta_path,
			     uint8_t format, uint32_t rate, uint8_t bits,
			     uint8_t channels, size_t stored_bytes)
{
	LOG_INF("capture audio: %s", audio_path);
	LOG_INF("capture meta : %s", meta_path);
	LOG_INF("capture format: format=%s rate=%u hardware_bits=%u channels=%u raw=%s stored=%u",
		format_name(format), rate, bits, channels, raw_format_name(bits),
		(uint32_t)stored_bytes);
	LOG_INF("storage partition: i2s_payload flash_offset=0x%08x size=0x%08x",
		(uint32_t)I2S_PAYLOAD_FLASH_OFFSET,
		(uint32_t)I2S_PAYLOAD_FLASH_SIZE);
	LOG_INF("dump LittleFS image: python /tools/openocd/scripts/read_xspi_tcl.py --cfg_path /tools/openocd/sr100_m55.cfg --flash-offset 0x%08x --size 0x%08x --output i2s_payload_lfs.bin",
		(uint32_t)I2S_PAYLOAD_FLASH_OFFSET,
		(uint32_t)I2S_PAYLOAD_FLASH_SIZE);
	LOG_INF("extract %s from i2s_payload_lfs.bin, then validate with:", audio_path);
	LOG_INF("ffplay -f %s -ar %u -ac %u <extracted-file>",
		raw_format_name(bits), rate, channels);
}
#endif

static int pack_rx_for_file(uint8_t bits, uint8_t channels, const uint8_t *src, size_t src_len,
			    uint8_t *dst, size_t dst_size, size_t *dst_len)
{
	size_t in_b = i2s_io_word_bytes(bits);
	size_t out_b = file_word_bytes(bits);
	size_t phys_ch = physical_channels(channels);
	size_t frames;
	size_t out_samples;

	if ((src_len % (in_b * phys_ch)) != 0U) {
		return -EINVAL;
	}

	frames = src_len / (in_b * phys_ch);
	out_samples = frames * channels;
	if ((out_samples * out_b) > dst_size) {
		return -ENOMEM;
	}

	for (size_t f = 0U; f < frames; f++) {
		for (size_t ch = 0U; ch < channels; ch++) {
			size_t out_i = (f * channels) + ch;
			size_t src_off = ((f * phys_ch) + ch) * in_b;
			size_t dst_off = out_i * out_b;
			uint32_t raw = 0U;

			memcpy(&raw, &src[src_off], in_b);

			if (out_b == 1U) {
				dst[dst_off] = (uint8_t)(raw >> 24);
			} else if (out_b == 2U) {
				uint16_t sample = (uint16_t)(raw >> 16);

				memcpy(&dst[dst_off], &sample, sizeof(sample));
			} else {
				memcpy(&dst[dst_off], &raw, sizeof(raw));
			}
		}
	}

	*dst_len = out_samples * out_b;
	return 0;
}

static uint32_t sine_sample(uint8_t bits, uint32_t frame)
{
	static const uint16_t sine16[] = {
		0x0000, 0x18f8, 0x30fb, 0x471c, 0x5a82, 0x6a6d, 0x7641, 0x7d89,
		0x7fff, 0x7d89, 0x7641, 0x6a6d, 0x5a82, 0x471c, 0x30fb, 0x18f8,
		0x0000, 0xe708, 0xcf05, 0xb8e4, 0xa57e, 0x9593, 0x89bf, 0x8277,
		0x8000, 0x8277, 0x89bf, 0x9593, 0xa57e, 0xb8e4, 0xcf05, 0xe708,
	};
	uint32_t s = sine16[frame % ARRAY_SIZE(sine16)];

	if (bits == 8U) {
		return (s >> 8) << 24;
	}
	if (bits == 16U) {
		return s << 16;
	}
	return s << 8;
}

static int write_i2s_block(const struct device *dev, void *block, size_t len)
{
	int64_t deadline = k_uptime_get() + I2S_SHELL_TIMEOUT_MS;
	int ret;

	do {
		ret = i2s_write(dev, block, len);
		if (ret == -EAGAIN) {
			k_msleep(1);
		}
	} while ((ret == -EAGAIN) && (k_uptime_get() < deadline) && !stop_requested());
	return ret;
}

static int queue_sine_block(uint32_t rate, uint8_t bits, uint8_t channels, uint32_t *frame)
{
	void *block;
	size_t bs = block_size(rate, bits, channels);
	size_t phys_ch = physical_channels(channels);
	uint32_t frames = bs / (phys_ch * sizeof(uint32_t));
	uint32_t *out;
	int ret;

	ret = k_mem_slab_alloc(&i2s_tx_slab, &block, K_MSEC(I2S_SHELL_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}

	out = block;
	for (uint32_t f = 0U; f < frames; f++) {
		uint32_t sample = sine_sample(bits, (*frame)++);

		for (uint8_t ch = 0U; ch < phys_ch; ch++) {
			*out++ = sample;
		}
	}

	ret = write_i2s_block(tx_dev, block, bs);
	if (ret != 0) {
		k_mem_slab_free(&i2s_tx_slab, block);
	}
	return ret;
}

static size_t file_bytes_per_tx_block(uint32_t rate, uint8_t bits, uint8_t channels)
{
	size_t phys_ch = physical_channels(channels);
	size_t frames = block_size(rate, bits, channels) / (phys_ch * sizeof(uint32_t));

	return frames * channels * file_word_bytes(bits);
}

static int queue_file_block(uint32_t rate, uint8_t bits, uint8_t channels,
				    const uint8_t *raw, size_t raw_len)
{
	size_t storage_word_bytes = file_word_bytes(bits);
	size_t samples = raw_len / storage_word_bytes;
	size_t phys_ch = physical_channels(channels);
	size_t frames;
	void *block;
	size_t bs = block_size(rate, bits, channels);
	size_t max_frames = bs / (phys_ch * sizeof(uint32_t));
	uint32_t *out;
	int ret;

	if ((raw_len == 0U) || ((raw_len % storage_word_bytes) != 0U) ||
	    ((samples % channels) != 0U)) {
		return -EINVAL;
	}

	frames = samples / channels;
	if (frames > max_frames) {
		return -EINVAL;
	}

	ret = k_mem_slab_alloc(&i2s_tx_slab, &block, K_MSEC(I2S_SHELL_TIMEOUT_MS));
	if (ret != 0) {
		return ret;
	}

	out = block;
	for (size_t f = 0U; f < frames; f++) {
		uint32_t first_sample = 0U;

		for (size_t ch = 0U; ch < channels; ch++) {
			const uint8_t *sample_ptr = &raw[((f * channels) + ch) * storage_word_bytes];
			uint32_t sample;

			if (storage_word_bytes == 1U) {
				sample = ((uint32_t)sample_ptr[0]) << 24;
			} else if (storage_word_bytes == 2U) {
				uint16_t v;

				memcpy(&v, sample_ptr, sizeof(v));
				sample = ((uint32_t)v) << 16;
			} else {
				memcpy(&sample, sample_ptr, sizeof(sample));
			}

			if (ch == 0U) {
				first_sample = sample;
			}
			*out++ = sample;
		}

		/* Logical mono is played on the 2-slot I2S bus by duplicating L into R. */
		for (size_t ch = channels; ch < phys_ch; ch++) {
			*out++ = first_sample;
		}
	}

	memset(out, 0, bs - (frames * phys_ch * sizeof(uint32_t)));

	ret = write_i2s_block(tx_dev, block, bs);
	if (ret != 0) {
		k_mem_slab_free(&i2s_tx_slab, block);
	}
	return ret;
}

static int read_and_queue_file_block(struct fs_file_t *file,
				     uint32_t rate, uint8_t bits, uint8_t channels,
				     size_t remaining, size_t *consumed)
{
	size_t raw_chunk = MIN(file_bytes_per_tx_block(rate, bits, channels), remaining);
	ssize_t rd;

	*consumed = 0U;
	raw_chunk = (raw_chunk / file_word_bytes(bits)) * file_word_bytes(bits);
	if (raw_chunk == 0U) {
		return -ENODATA;
	}

	rd = fs_read(file, file_read_buffer, raw_chunk);
	if (rd < 0) {
		return (int)rd;
	}
	if (rd == 0) {
		return -ENODATA;
	}

	rd = (rd / (ssize_t)file_word_bytes(bits)) * (ssize_t)file_word_bytes(bits);
	if (rd == 0) {
		return -EIO;
	}

	*consumed = (size_t)rd;
	return queue_file_block(rate, bits, channels, file_read_buffer, (size_t)rd);
}

static int configure_i2s(const struct device *dev, enum i2s_dir dir,
			 uint8_t format, uint32_t rate, uint8_t bits, uint8_t channels,
			 struct k_mem_slab *slab)
{
	struct i2s_config cfg = {
		.word_size = bits,
		.channels = physical_channels(channels),
		.format = format,
		.options = i2s_options(),
		.frame_clk_freq = rate,
		.mem_slab = slab,
		.block_size = block_size(rate, bits, channels),
		.timeout = (dir == I2S_DIR_RX) ? I2S_SHELL_RX_POLL_MS : I2S_SHELL_TIMEOUT_MS,
	};

	reset_stream(dev, dir);
	return i2s_configure(dev, dir, &cfg);
}

static int validate_format(uint32_t rate, uint8_t bits, uint8_t channels)
{
	if ((channels == 0U) || (channels > I2S_SHELL_CHANNELS_MAX)) {
		return -EINVAL;
	}
	if ((bits != 8U) && (bits != 16U) && (bits != 24U)) {
		return -EINVAL;
	}
	if ((rate != 8000U) && (rate != 16000U) &&
	    (rate != 44100U) && (rate != 48000U)) {
		return -EINVAL;
	}
	return (block_size(rate, bits, channels) <= I2S_SHELL_BLOCK_SIZE) ? 0 : -ENOMEM;
}

static void writer_file_close(void)
{
	if (writer.file_opened) {
		(void)fs_close(&writer.file);
		writer.file_opened = false;
	}
}


static int writer_file_prepare(const char *path)
{
	int ret;

	writer_file_close();
	fs_file_t_init(&writer.file);

	ret = fs_open(&writer.file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}

	writer.file_opened = true;
	writer.bytes_written = 0U;

	/* No LittleFS preallocation/pre-erase here: capture writes stream data only. */
	return 0;
}

static void writer_thread(void *a, void *b, void *c)
{
	struct writer_ctx *ctx = a;
	struct writer_item item;

	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (k_msgq_get(&ctx->queue, &item, K_FOREVER) == 0) {
		bool release_buffer = item.len > 0U;

		if ((item.len > 0U) && (atomic_get(&ctx->result) == 0)) {
			size_t off = 0U;

			while (off < item.len) {
				size_t chunk = MIN(I2S_SHELL_LFS_WRITE_CHUNK_BYTES, item.len - off);
				ssize_t wr = fs_write(&ctx->file, &write_buffers[item.index][off], chunk);

				if (wr != (ssize_t)chunk) {
					int err = (wr < 0) ? (int)wr : -EIO;

					LOG_ERR("audio fs_write failed: ret=%d wrote=%d expected=%u total=%u",
						err, (int)wr, (uint32_t)chunk,
						(uint32_t)ctx->bytes_written);
					atomic_set(&ctx->result, (atomic_val_t)err);
					break;
				}

				ctx->bytes_written += chunk;
				off += chunk;
			}
		}

		/* Only data-bearing items hold a free_buffers slot lease. */
		if (release_buffer) {
			k_sem_give(&ctx->free_buffers);
		}

		if (item.final) {
			break;
		}
	}
}

static int capture_file(const struct stream_params *p)
{
	char audio_path[I2S_SHELL_PATH_MAX];
	char meta_path[I2S_SHELL_PATH_MAX];
	size_t fill = 0U;
	uint8_t fill_index = 0U;
	bool fill_acquired = false;
	int64_t end_ms = 0;
	bool timed = p->duration_sec != 0U;
	int ret;

	ret = fs_mount_once();
	if (ret != 0) {
		return ret;
	}
	ret = path_resolve(p->path, audio_path, sizeof(audio_path));
	if (ret != 0) {
		return ret;
	}
	ret = meta_path_from_audio_path(audio_path, meta_path, sizeof(meta_path));
	if (ret != 0) {
		return ret;
	}
	ret = mkdirs_for_file(audio_path);
	if (ret != 0) {
		return ret;
	}

	memset(&writer, 0, sizeof(writer));
	atomic_set(&writer.result, 0);
	k_msgq_init(&writer.queue, writer.queue_storage, sizeof(struct writer_item),
		   I2S_SHELL_WRITE_BUFFER_COUNT);
	k_sem_init(&writer.free_buffers, I2S_SHELL_WRITE_BUFFER_COUNT, I2S_SHELL_WRITE_BUFFER_COUNT);

	ret = writer_file_prepare(audio_path);
	if (ret != 0) {
		return ret;
	}

	(void)k_thread_create(&writer.thread, i2s_shell_writer_stack,
			    K_THREAD_STACK_SIZEOF(i2s_shell_writer_stack), writer_thread,
			    &writer, NULL, NULL, I2S_SHELL_FILE_WRITER_PRIORITY, 0, K_NO_WAIT);

	ret = configure_i2s(rx_dev, I2S_DIR_RX, p->format, p->rate, p->word_size,
			    p->channels, &i2s_rx_slab);
	if (ret != 0) {
		goto out_writer;
	}
	ret = i2s_trigger(rx_dev, I2S_DIR_RX, I2S_TRIGGER_START);
	if (ret != 0) {
		goto out_reset;
	}

	if (timed) {
		end_ms = k_uptime_get() + ((int64_t)p->duration_sec * 1000LL);
	}

	while ((!timed || (k_uptime_get() < end_ms)) && !stop_requested()) {
		void *block;
		size_t size;
		size_t packed;

		ret = i2s_read(rx_dev, &block, &size);
		if (ret == -EAGAIN) {
			ret = 0;
			continue;
		}
		if (ret != 0) {
			LOG_ERR("i2s_read failed during capture: ret=%d rate=%u bits=%u ch=%u",
				ret, p->rate, p->word_size, p->channels);
			goto out_reset;
		}

		ret = pack_rx_for_file(p->word_size, p->channels, block, size, scratch[0], sizeof(scratch[0]), &packed);
		k_mem_slab_free(&i2s_rx_slab, block);
		if (ret != 0) {
			goto out_reset;
		}

		for (size_t off = 0U; off < packed;) {
			size_t take;

			if (!fill_acquired) {
				ret = k_sem_take(&writer.free_buffers, K_MSEC(I2S_SHELL_TIMEOUT_MS));
				if (ret != 0) {
					goto out_reset;
				}
				fill_acquired = true;
				fill = 0U;
			}

			take = MIN(I2S_SHELL_WRITE_BUFFER_BYTES - fill, packed - off);
			memcpy(&write_buffers[fill_index][fill], &scratch[0][off], take);
			fill += take;
			off += take;

			if (fill == I2S_SHELL_WRITE_BUFFER_BYTES) {
				struct writer_item item = { .index = fill_index, .len = fill };

				ret = k_msgq_put(&writer.queue, &item, K_NO_WAIT);
				if (ret != 0) {
					k_sem_give(&writer.free_buffers);
					fill_acquired = false;
					goto out_reset;
				}
				fill_index = (fill_index + 1U) % I2S_SHELL_WRITE_BUFFER_COUNT;
				fill = 0U;
				fill_acquired = false;
			}
		}

		ret = atomic_get(&writer.result);
		if (ret != 0) {
			LOG_ERR("writer failed during capture: ret=%d rate=%u bits=%u ch=%u stored=%u",
				ret, p->rate, p->word_size, p->channels,
				(uint32_t)writer.bytes_written);
			goto out_reset;
		}
	}

out_reset:
	reset_stream(rx_dev, I2S_DIR_RX);
out_writer:
	{
		struct writer_item item = { .index = fill_index, .final = true, .len = 0U };

		if (fill_acquired) {
			item.len = fill;
		} else if (ret != 0) {
			item.len = 0U;
		}
		(void)k_msgq_put(&writer.queue, &item, K_FOREVER);
	}
	(void)k_thread_join(&writer.thread, K_SECONDS(5));
	if (ret == 0) {
		ret = atomic_get(&writer.result);
	}
	writer_file_close();

	if ((ret == 0) && (writer.bytes_written == 0U)) {
		LOG_ERR("capture completed with no stored audio: rate=%u bits=%u ch=%u",
			p->rate, p->word_size, p->channels);
		ret = -ENODATA;
	}
	if ((ret == 0) && (writer.bytes_written > 0U)) {
		ret = write_meta_file(meta_path, audio_path, p->format, p->rate,
				      p->word_size, p->channels, writer.bytes_written);
	}
	if (ret == 0) {
		(void)snprintk(last_audio_path, sizeof(last_audio_path), "%s", audio_path);
		(void)snprintk(last_meta_path, sizeof(last_meta_path), "%s", meta_path);
	}
#if CONFIG_I2S_SHELL_PRINT_CAPTURE_INFO
	if (ret == 0) {
		log_capture_info(audio_path, meta_path, p->format, p->rate,
				 p->word_size, p->channels, writer.bytes_written);
	}
#endif
	if (ret != 0) {
		unlink_if_exists(meta_path);
		unlink_if_exists(audio_path);
	}
	return ret;
}


static size_t bytes_per_second(uint32_t rate, uint8_t bits, uint8_t channels)
{
	return (size_t)rate * channels * file_word_bytes(bits);
}

static int capture_ram(const struct stream_params *p)
{
	size_t max_bytes;
	size_t stored = 0U;
	int64_t end_ms;
	int ret;

	if (p->duration_sec == 0U) {
		return -EINVAL;
	}

	max_bytes = bytes_per_second(p->rate, p->word_size, p->channels) *
		     p->duration_sec;
	if (max_bytes > sizeof(ram_dump)) {
		return -ENOMEM;
	}

	last_ram_bytes = 0U;
	memset(&last_ram_meta, 0, sizeof(last_ram_meta));

	ret = configure_i2s(rx_dev, I2S_DIR_RX, p->format, p->rate,
			    p->word_size, p->channels, &i2s_rx_slab);
	if (ret != 0) {
		return ret;
	}

	ret = i2s_trigger(rx_dev, I2S_DIR_RX, I2S_TRIGGER_START);
	if (ret != 0) {
		goto out_reset;
	}

	end_ms = k_uptime_get() + ((int64_t)p->duration_sec * 1000LL);
	while ((k_uptime_get() < end_ms) && !stop_requested()) {
		void *block;
		size_t size;
		size_t packed;
		size_t take;

		ret = i2s_read(rx_dev, &block, &size);
		if (ret == -EAGAIN) {
			ret = 0;
			continue;
		}
		if (ret != 0) {
			goto out_reset;
		}

		ret = pack_rx_for_file(p->word_size, p->channels, block, size,
				       scratch[0], sizeof(scratch[0]), &packed);
		k_mem_slab_free(&i2s_rx_slab, block);
		if (ret != 0) {
			goto out_reset;
		}

		take = MIN(packed, sizeof(ram_dump) - stored);
		memcpy(&ram_dump[stored], scratch[0], take);
		stored += take;
		if ((stored >= max_bytes) || (stored >= sizeof(ram_dump))) {
			break;
		}
	}

out_reset:
	reset_stream(rx_dev, I2S_DIR_RX);
	if ((ret == 0) && (stored == 0U)) {
		ret = -ENODATA;
	}
	if (ret == 0) {
		last_ram_meta.magic = I2S_SHELL_META_MAGIC;
		last_ram_meta.version = I2S_SHELL_META_VERSION;
		last_ram_meta.format = p->format;
		last_ram_meta.sample_rate = p->rate;
		last_ram_meta.channels = p->channels;
		last_ram_meta.hardware_bits = p->word_size;
		last_ram_meta.container_bits = file_word_bytes(p->word_size) * 8U;
		last_ram_meta.stored_bytes = (uint32_t)stored;
		(void)snprintk(last_ram_meta.audio_file,
			       sizeof(last_ram_meta.audio_file), "RAM");
		last_ram_bytes = stored;
	}
	return ret;
}

static int queue_ram_audio(const struct i2s_payload_meta *meta,
			   size_t *offset, size_t *remaining)
{
	size_t frame_bytes = meta->channels * file_word_bytes((uint8_t)meta->hardware_bits);
	size_t chunk = MIN(file_bytes_per_tx_block(meta->sample_rate,
						    (uint8_t)meta->hardware_bits,
						    (uint8_t)meta->channels),
			   *remaining);
	int ret;

	chunk = (chunk / frame_bytes) * frame_bytes;
	if (chunk == 0U) {
		return -ENODATA;
	}

	ret = queue_file_block(meta->sample_rate, (uint8_t)meta->hardware_bits,
			       (uint8_t)meta->channels, &ram_dump[*offset], chunk);
	if (ret == 0) {
		*offset += chunk;
		*remaining -= chunk;
	}
	return ret;
}

static int play_ram(const struct stream_params *p)
{
	struct i2s_payload_meta meta = last_ram_meta;
	size_t raw_remaining;
	size_t raw_offset = 0U;
	uint32_t primed = 0U;
	bool tx_started = false;
	bool reset_tx = true;
	int ret;

	if (last_ram_bytes == 0U) {
		return -ENOENT;
	}

	raw_remaining = last_ram_bytes;
	if (p->duration_sec != 0U) {
		raw_remaining = MIN(raw_remaining,
			bytes_per_second(meta.sample_rate,
					 (uint8_t)meta.hardware_bits,
					 (uint8_t)meta.channels) * p->duration_sec);
	}
	if (raw_remaining == 0U) {
		return -ENODATA;
	}

	ret = configure_i2s(tx_dev, I2S_DIR_TX, (uint8_t)meta.format,
			    meta.sample_rate, (uint8_t)meta.hardware_bits,
			    (uint8_t)meta.channels, &i2s_tx_slab);
	if (ret != 0) {
		return ret;
	}

	while ((primed < I2S_SHELL_TX_PRIME_BLOCKS) &&
	       (raw_remaining > 0U) && !stop_requested()) {
		ret = queue_ram_audio(&meta, &raw_offset, &raw_remaining);
		if (ret == -ENODATA) {
			ret = 0;
			break;
		}
		if (ret != 0) {
			goto out_reset;
		}
		primed++;
	}
	if (primed == 0U) {
		ret = stop_requested() ? 0 : -ENODATA;
		goto out_reset;
	}

	ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		goto out_reset;
	}
	tx_started = true;

	while ((raw_remaining > 0U) && !stop_requested()) {
		ret = queue_ram_audio(&meta, &raw_offset, &raw_remaining);
		if (ret == -ENODATA) {
			ret = 0;
			break;
		}
		if (ret != 0) {
			goto out_reset;
		}
	}

	if (tx_started) {
		if (stop_requested()) {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			if (ret == 0) {
				reset_tx = false;
			}
		} else {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
			if (ret == 0) {
				reset_tx = false;
				k_msleep((I2S_SHELL_TX_PRIME_BLOCKS + 2U) * I2S_SHELL_BLOCK_MS);
			} else if (ret == -EIO) {
				ret = 0;
				reset_tx = false;
			}
		}
	}

out_reset:
	if (reset_tx) {
		reset_stream(tx_dev, I2S_DIR_TX);
	}
	return ret;
}

static int play_file(const struct stream_params *p)
{
	char audio_path[I2S_SHELL_PATH_MAX];
	char meta_path[I2S_SHELL_PATH_MAX];
	struct i2s_payload_meta meta;
	struct fs_file_t f;
	size_t raw_remaining;
	uint32_t primed = 0U;
	bool tx_started = false;
	bool reset_tx = true;
	int ret;

	ret = fs_mount_once();
	if (ret != 0) {
		return ret;
	}
	ret = path_resolve(p->path, audio_path, sizeof(audio_path));
	if (ret != 0) {
		return ret;
	}
	ret = meta_path_from_audio_path(audio_path, meta_path, sizeof(meta_path));
	if (ret != 0) {
		return ret;
	}
	ret = read_meta_file(meta_path, &meta);
	if (ret != 0) {
		return ret;
	}
	if (meta.container_bits != (file_word_bytes((uint8_t)meta.hardware_bits) * 8U)) {
		return -EINVAL;
	}
	if (validate_format(meta.sample_rate, (uint8_t)meta.hardware_bits,
			    (uint8_t)meta.channels) != 0) {
		return -EINVAL;
	}

	if (!path_is_lfs_file(meta.audio_file)) {
		return -EINVAL;
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, meta.audio_file, FS_O_READ);
	if (ret != 0) {
		return ret;
	}

	raw_remaining = meta.stored_bytes;
	if (p->duration_sec != 0U) {
		raw_remaining = MIN(raw_remaining,
			(size_t)meta.sample_rate * meta.channels *
			file_word_bytes((uint8_t)meta.hardware_bits) *
			p->duration_sec);
	}
	if (raw_remaining == 0U) {
		ret = -ENODATA;
		goto out_close;
	}

	ret = configure_i2s(tx_dev, I2S_DIR_TX, (uint8_t)meta.format,
			    meta.sample_rate, (uint8_t)meta.hardware_bits,
			    (uint8_t)meta.channels, &i2s_tx_slab);
	if (ret != 0) {
		goto out_close;
	}

	while ((primed < I2S_SHELL_TX_PRIME_BLOCKS) &&
	       (raw_remaining > 0U) && !stop_requested()) {
		size_t consumed;

		ret = read_and_queue_file_block(&f, meta.sample_rate,
						(uint8_t)meta.hardware_bits,
						(uint8_t)meta.channels,
						raw_remaining, &consumed);
		if (ret == -ENODATA) {
			ret = 0;
			break;
		}
		if (ret != 0) {
			goto out_reset;
		}

		raw_remaining -= MIN(raw_remaining, consumed);
		primed++;
	}

	if (primed == 0U) {
		ret = stop_requested() ? 0 : -ENODATA;
		goto out_reset;
	}

	ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		goto out_reset;
	}
	tx_started = true;

	while ((raw_remaining > 0U) && !stop_requested()) {
		size_t consumed;

		ret = read_and_queue_file_block(&f, meta.sample_rate,
						(uint8_t)meta.hardware_bits,
						(uint8_t)meta.channels,
						raw_remaining, &consumed);
		if (ret == -ENODATA) {
			ret = 0;
			break;
		}
		if (ret != 0) {
			goto out_reset;
		}

		raw_remaining -= MIN(raw_remaining, consumed);
	}

	if (tx_started) {
		if (stop_requested()) {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			if (ret == 0) {
				reset_tx = false;
			}
		} else {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
			if (ret == 0) {
				reset_tx = false;
				k_msleep((I2S_SHELL_TX_PRIME_BLOCKS + 2U) * I2S_SHELL_BLOCK_MS);
			} else if (ret == -EIO) {
				ret = 0;
				reset_tx = false;
			}
		}
	}

out_reset:
	if (reset_tx) {
		reset_stream(tx_dev, I2S_DIR_TX);
	}
out_close:
	(void)fs_close(&f);
	return ret;
}

static int play_sine(const struct stream_params *p)
{
	uint32_t frame = 0U;
	uint32_t blocks;
	bool tx_started = false;
	bool reset_tx = true;
	int ret;

	ret = configure_i2s(tx_dev, I2S_DIR_TX, p->format, p->rate, p->word_size,
			    p->channels, &i2s_tx_slab);
	if (ret != 0) {
		return ret;
	}

	blocks = (p->duration_sec == 0U) ? UINT32_MAX :
		(p->duration_sec * 1000U) / I2S_SHELL_BLOCK_MS;

	for (uint32_t i = 0U; (i < I2S_SHELL_TX_PRIME_BLOCKS) && (i < blocks); i++) {
		ret = queue_sine_block(p->rate, p->word_size, p->channels, &frame);
		if (ret != 0) {
			goto out;
		}
	}

	ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		goto out;
	}
	tx_started = true;

	for (uint32_t i = I2S_SHELL_TX_PRIME_BLOCKS; (i < blocks) && !stop_requested(); i++) {
		ret = queue_sine_block(p->rate, p->word_size, p->channels, &frame);
		if (ret != 0) {
			goto out;
		}
	}

	if (tx_started) {
		if (stop_requested()) {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			if (ret == 0) {
				reset_tx = false;
			}
		} else {
			ret = i2s_trigger(tx_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
			if (ret == 0) {
				reset_tx = false;
			} else if (ret == -EIO) {
				ret = 0;
				reset_tx = false;
			}
		}
	}
out:
	if (reset_tx) {
		reset_stream(tx_dev, I2S_DIR_TX);
	}
	return ret;
}

static void worker_fn(void *a, void *b, void *c)
{
	struct stream_params params = *(struct stream_params *)a;
	enum stream_op op = (enum stream_op)(uintptr_t)b;
	int ret;

	ARG_UNUSED(c);


	switch (op) {
	case STREAM_CAPTURE_RAM:
		ret = capture_ram(&params);
		break;
	case STREAM_CAPTURE_FILE:
		ret = capture_file(&params);
		break;
	case STREAM_PLAY_RAM:
		ret = play_ram(&params);
		break;
	case STREAM_PLAY_FILE:
		ret = play_file(&params);
		break;
	case STREAM_PLAY_SINE:
		ret = play_sine(&params);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	k_mutex_lock(&runtime.lock, K_FOREVER);
	runtime.last_result = ret;
	runtime.stop = false;
	runtime.op = STREAM_IDLE;
	runtime.tid = NULL;
	k_mutex_unlock(&runtime.lock);
	LOG_INF("operation done: %d", ret);
}

static int start_op(enum stream_op op, const struct stream_params *params)
{
	int ret = 0;

	k_mutex_lock(&runtime.lock, K_FOREVER);
	if (runtime.tid != NULL) {
		ret = -EBUSY;
	} else {
		runtime.params = *params;
		runtime.stop = false;
		runtime.op = op;
		runtime.tid = k_thread_create(&runtime.thread, i2s_shell_worker_stack,
			K_THREAD_STACK_SIZEOF(i2s_shell_worker_stack), worker_fn,
			&runtime.params, (void *)(uintptr_t)op, NULL,
			CONFIG_I2S_SHELL_WORKER_PRIORITY, 0, K_NO_WAIT);
	}
	k_mutex_unlock(&runtime.lock);
	return ret;
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *endptr;
	unsigned long parsed;

	if ((text == NULL) || (text[0] == '\0') || (value == NULL)) {
		return -EINVAL;
	}

	parsed = strtoul(text, &endptr, 0);
	if ((endptr == NULL) || (*endptr != '\0') || (parsed > UINT32_MAX)) {
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static const char *format_name(uint8_t format)
{
	switch (format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
		return "i2s";
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
		return "left";
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		return "right";
	default:
		return "unknown";
	}
}

static int parse_i2s_format(const char *text, uint8_t *format)
{
	if ((text == NULL) || (format == NULL)) {
		return -EINVAL;
	}

	if (strcmp(text, "i2s") == 0) {
		*format = I2S_FMT_DATA_FORMAT_I2S;
	} else if ((strcmp(text, "left") == 0) || (strcmp(text, "lj") == 0)) {
		*format = I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED;
	} else if ((strcmp(text, "right") == 0) || (strcmp(text, "rj") == 0)) {
		*format = I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED;
	} else {
		return -EINVAL;
	}
	return 0;
}

static int parse_common(char **argv, struct stream_params *p, bool with_path)
{
	uint32_t tmp;
	int ret;

	ret = parse_i2s_format(argv[0], &p->format);
	ret |= parse_u32(argv[1], &p->rate);
	ret |= parse_u32(argv[2], &tmp);
	p->word_size = (uint8_t)tmp;
	ret |= parse_u32(argv[3], &tmp);
	p->channels = (uint8_t)tmp;
	ret |= parse_u32(argv[4], &p->duration_sec);
	if (ret != 0) {
		return -EINVAL;
	}

	ret = validate_format(p->rate, p->word_size, p->channels);
	if (ret != 0) {
		return ret;
	}

	if (with_path) {
		ret = path_validate_relative(argv[5]);
		if (ret != 0) {
			return ret;
		}
		strncpy(p->path, argv[5], sizeof(p->path) - 1U);
		p->path[sizeof(p->path) - 1U] = '\0';
	}

	return 0;
}

static int cmd_capture(const struct shell *sh, size_t argc, char **argv)
{
	struct stream_params p = { 0 };
	int ret;

	if ((argc != 6U) && (argc != 7U)) {
		shell_error(sh, I2S_SHELL_CAPTURE_USAGE);
		return -EINVAL;
	}

	ret = parse_common(&argv[1], &p, argc == 7U);
	if (ret != 0) {
		shell_error(sh, I2S_SHELL_CAPTURE_USAGE);
		return ret;
	}
	if ((argc == 6U) && (p.duration_sec == 0U)) {
		shell_error(sh, "seconds must be nonzero for RAM capture");
		return -EINVAL;
	}

	ret = start_op((argc == 7U) ? STREAM_CAPTURE_FILE : STREAM_CAPTURE_RAM, &p);
	if (ret == 0) {
		if (argc == 7U) {
			shell_print(sh,
				    "capture started: format=%s %u Hz %u-bit %u ch sec=%u -> /lfs/%s and /lfs/%s.meta",
				    format_name(p.format), p.rate, p.word_size, p.channels,
				    p.duration_sec, p.path, p.path);
		} else {
			shell_print(sh,
				    "capture started: format=%s %u Hz %u-bit %u ch sec=%u -> RAM",
				    format_name(p.format), p.rate, p.word_size, p.channels,
				    p.duration_sec);
		}
	}
	return ret;
}

static int cmd_play(const struct shell *sh, size_t argc, char **argv)
{
	struct stream_params p = { 0 };
	int ret;

	if ((argc >= 2U) && (strcmp(argv[1], "sine") == 0)) {
		if (argc != 7U) {
			shell_error(sh, I2S_SHELL_PLAY_SINE_USAGE);
			return -EINVAL;
		}

		ret = parse_common(&argv[2], &p, false);
		if (ret != 0) {
			shell_error(sh, I2S_SHELL_PLAY_SINE_USAGE);
			return ret;
		}
		ret = start_op(STREAM_PLAY_SINE, &p);
		if (ret == 0) {
			shell_print(sh, "sine playback started: format=%s %u Hz %u-bit %u ch sec=%u",
				    format_name(p.format), p.rate, p.word_size,
				    p.channels, p.duration_sec);
		}
		return ret;
	}

	if (argc == 1U) {
		ret = start_op(STREAM_PLAY_RAM, &p);
		if (ret == 0) {
			shell_print(sh, "playback started: RAM");
		} else {
			shell_error(sh, "RAM playback failed to start: %d", ret);
		}
		return ret;
	}

	if ((argc != 2U) && (argc != 3U)) {
		shell_error(sh, I2S_SHELL_PLAY_USAGE);
		return -EINVAL;
	}

	ret = path_validate_relative(argv[1]);
	if (ret != 0) {
		shell_error(sh, I2S_SHELL_PLAY_USAGE);
		return ret;
	}
	strncpy(p.path, argv[1], sizeof(p.path) - 1U);
	p.path[sizeof(p.path) - 1U] = '\0';

	if (argc == 3U) {
		ret = parse_u32(argv[2], &p.duration_sec);
		if (ret != 0) {
			shell_error(sh, I2S_SHELL_PLAY_USAGE);
			return ret;
		}
	}

	ret = start_op(STREAM_PLAY_FILE, &p);
	if (ret == 0) {
		shell_print(sh, "playback started: /lfs/%s%s",
			    p.path,
			    (p.duration_sec == 0U) ? "" : " (limited duration)");
	}
	return ret;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	k_tid_t tid;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&runtime.lock, K_FOREVER);
	tid = runtime.tid;
	runtime.stop = true;
	k_mutex_unlock(&runtime.lock);

	if (tid == NULL) {
		shell_print(sh, "idle");
		return 0;
	}
	(void)k_thread_join(tid, K_SECONDS(2));
	reset_stream(tx_dev, I2S_DIR_TX);
	reset_stream(rx_dev, I2S_DIR_RX);
	return 0;
}

static const char *stream_op_name(enum stream_op op)
{
	switch (op) {
	case STREAM_CAPTURE_RAM:
		return "capture-ram";
	case STREAM_CAPTURE_FILE:
		return "capture-file";
	case STREAM_PLAY_RAM:
		return "play-ram";
	case STREAM_PLAY_FILE:
		return "play-file";
	case STREAM_PLAY_SINE:
		return "play-sine";
	default:
		return "idle";
	}
}

static int cmd_caps(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint32_t tmp;
	uint8_t bits;
	uint8_t channels;
	int ret;

	if (argc != 4U) {
		return -EINVAL;
	}

	ret = parse_u32(argv[1], &rate);
	ret |= parse_u32(argv[2], &tmp);
	bits = (uint8_t)tmp;
	ret |= parse_u32(argv[3], &tmp);
	channels = (uint8_t)tmp;
	if ((ret != 0) || (validate_format(rate, bits, channels) != 0)) {
		shell_error(sh,
			    "supported: rate={8000,16000,44100,48000} bits={8,16,24} channels={1,2}");
		return -EINVAL;
	}

	shell_print(sh,
		    "bytes_per_sec=%u block_size=%u tx_file_block_bytes=%u littlefs_file_mode=yes",
		    rate * channels * file_word_bytes(bits),
		    (uint32_t)block_size(rate, bits, channels),
		    (uint32_t)file_bytes_per_tx_block(rate, bits, channels));
	return 0;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "I2S shell");
	shell_print(sh, "  rx              : %s %s", rx_dev->name, device_is_ready(rx_dev) ? "ready" : "not-ready");
	shell_print(sh, "  tx              : %s %s", tx_dev->name, device_is_ready(tx_dev) ? "ready" : "not-ready");
	shell_print(sh, "  source          : %s", I2S_SHELL_NODE_SOURCE);
	shell_print(sh, "  role            : %s", target_role() ? "target" : "controller");
	shell_print(sh, "  state           : %s", stream_op_name(runtime.op));
	shell_print(sh, "  LittleFS        : %s (%s)", shell_fs.mounted ? "mounted" : "not mounted",
		    I2S_SHELL_FS_MOUNT_POINT);
	shell_print(sh, "  file bytes      : %u", (uint32_t)writer.bytes_written);
	shell_print(sh, "  RAM bytes       : %u", (uint32_t)last_ram_bytes);
	if (last_audio_path[0] != '\0') {
		shell_print(sh, "  audio file      : %s", last_audio_path);
	}
	if (last_meta_path[0] != '\0') {
		shell_print(sh, "  metadata file   : %s", last_meta_path);
	}
	shell_print(sh, "  last result     : %d", runtime.last_result);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(i2s_cmds,
	SHELL_CMD(info, NULL, "Show I2S shell information", cmd_info),
	SHELL_CMD_ARG(caps, NULL, "caps <rate> <bits> <channels>", cmd_caps, 4, 0),
	SHELL_CMD(capture, NULL, "capture <format> <rate> <bits> <channels> <sec> [file]; no file means RAM", cmd_capture),
	SHELL_CMD(play, NULL,
		  "Playback RAM or file: play [file] [sec;0/full if omitted] | "
		  "play sine <format> <rate> <bits> <channels> <sec>",
		  cmd_play),
	SHELL_CMD(stop, NULL, "Stop active stream", cmd_stop),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(i2s, &i2s_cmds, "I2S shell sample", NULL);

int main(void)
{
	int ret;

	k_mutex_init(&runtime.lock);

	if (I2S_SHELL_WRITE_BUFFER_BYTES > I2S_SHELL_LFS_WRITE_CHUNK_BYTES) {
		LOG_INF("LittleFS streaming buffer: buffers=%u buffer_bytes=%u fs_chunk=%u",
			I2S_SHELL_WRITE_BUFFER_COUNT, I2S_SHELL_WRITE_BUFFER_BYTES,
			I2S_SHELL_LFS_WRITE_CHUNK_BYTES);
	}

	if (!device_is_ready(rx_dev) || !device_is_ready(tx_dev)) {
		LOG_ERR("I2S device not ready");
		return -ENODEV;
	}

	ret = fs_mount_once();
	if (ret != 0) {
		LOG_WRN("LittleFS unavailable: %d", ret);
	}

	LOG_INF("I2S shell ready: rx=%s tx=%s source=%s role=%s fs=%s",
		rx_dev->name, tx_dev->name, I2S_SHELL_NODE_SOURCE,
		target_role() ? "target" : "controller",
		(ret == 0) ? I2S_SHELL_FS_MOUNT_POINT : "unavailable");
	return 0;
}
