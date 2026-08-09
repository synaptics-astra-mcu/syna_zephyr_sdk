/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/cache.h>
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

LOG_MODULE_REGISTER(dmic_sample, LOG_LEVEL_INF);

#define DMIC_NODE DT_ALIAS(dmic_0)

BUILD_ASSERT(DT_NODE_HAS_STATUS(DMIC_NODE, okay),
	     "Missing alias: dmic_0 = &<dmic_node>;");

#if DT_HAS_CHOSEN(syna_i2s_tx)
#define DMIC_I2S_TX_NODE DT_CHOSEN(syna_i2s_tx)
#define DMIC_I2S_NODE_SOURCE "chosen:syna_i2s_tx"
#elif DT_NODE_EXISTS(DT_NODELABEL(i2s0))
#define DMIC_I2S_TX_NODE DT_NODELABEL(i2s0)
#define DMIC_I2S_NODE_SOURCE "nodelabel:i2s0"
#else
#error "No I2S TX device selected. Add /chosen syna,i2s-tx = &i2s0; or provide i2s0."
#endif

enum {
	SAMPLE_AMIN_ENTRY_BYTES = 8U,
	SAMPLE_AMIN_BLOCK_ENTRIES = 0x400U,
	SAMPLE_READ_CHUNK_BYTES = SAMPLE_AMIN_ENTRY_BYTES * SAMPLE_AMIN_BLOCK_ENTRIES,

	SAMPLE_BLOCK_COUNT = 32U,
	SAMPLE_FILE_WRITE_BUFFER_COUNT = CONFIG_DMIC_SHELL_WRITE_BUFFER_COUNT,
	SAMPLE_FILE_WRITE_BUFFER_BYTES = CONFIG_DMIC_SHELL_WRITE_BUFFER_BYTES,
	SAMPLE_LFS_WRITE_CHUNK_BYTES = 8192U,
	SAMPLE_MAX_CHANNELS = 2U,

	SAMPLE_CONTAINER_BITS = CONFIG_AUDIO_DMIC_SYNA_SR100_PCM_WIDTH,
	SAMPLE_CONTAINER_BYTES = SAMPLE_CONTAINER_BITS / 8U,
	SAMPLE_FRAME_BYTES = SAMPLE_CONTAINER_BYTES * SAMPLE_MAX_CHANNELS,

	SAMPLE_DUMP_BYTES = CONFIG_DMIC_CAPTURE_BUFFER_KB * 1024U,
	SAMPLE_FS_PATH_MAX = 128U,
	SAMPLE_META_AUDIO_PATH_MAX = 96U,
	SAMPLE_RAM_FILE_WRITE_BUFFER_COUNT =
		SAMPLE_DUMP_BYTES / SAMPLE_FILE_WRITE_BUFFER_BYTES,
	SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT =
		SAMPLE_FILE_WRITE_BUFFER_COUNT +
		SAMPLE_RAM_FILE_WRITE_BUFFER_COUNT,

	SAMPLE_PAYLOAD_META_MAGIC = 0x444D4943U, /* DMIC */
	SAMPLE_PAYLOAD_META_VERSION = 2U,

	SAMPLE_I2S_PLAY_BLOCK_MS = 10U,
	SAMPLE_I2S_PLAY_BUFFER_BYTES = 4096U,
	SAMPLE_I2S_PLAY_BLOCK_COUNT = 48U,
	SAMPLE_I2S_PLAY_TIMEOUT_MS = 1000U,
	SAMPLE_I2S_TX_PRIME_BLOCKS = 12U,
	SAMPLE_PLAY_THREAD_PRIORITY = 5U,
};

struct dmic_payload_meta {
	uint32_t magic;
	uint32_t version;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t hardware_bits;
	uint32_t container_bits;
	uint32_t stored_bytes;
	char audio_file[SAMPLE_META_AUDIO_PATH_MAX];
};

struct dmic_file_block {
	size_t size;
	uint16_t index;
	bool final;
};

enum dmic_sample_state {
	DMIC_SHELL_IDLE,
	DMIC_SHELL_CONFIGURED,
	DMIC_SHELL_ACTIVE,
};

struct dmic_play_runtime {
	struct k_mutex lock;
	struct k_thread thread;
	k_tid_t tid;
	char path[SAMPLE_FS_PATH_MAX];
	bool ram;
	bool stop;
	int result;
};

K_MEM_SLAB_DEFINE_STATIC(sample_dmic_slab,
			 SAMPLE_READ_CHUNK_BYTES,
			 SAMPLE_BLOCK_COUNT,
			 32);

K_MEM_SLAB_DEFINE_STATIC(sample_i2s_tx_slab,
			 SAMPLE_I2S_PLAY_BUFFER_BYTES,
			 SAMPLE_I2S_PLAY_BLOCK_COUNT,
			 CONFIG_DCACHE_LINE_SIZE);

static uint8_t sample_i2s_play_scratch[2]
					[SAMPLE_I2S_PLAY_BUFFER_BYTES]
	__aligned(CONFIG_DCACHE_LINE_SIZE);

static uint8_t sample_ram_dump[SAMPLE_DUMP_BYTES]
	__aligned(CONFIG_DCACHE_LINE_SIZE);
static uint8_t file_write_buffers[SAMPLE_FILE_WRITE_BUFFER_COUNT]
				      [SAMPLE_FILE_WRITE_BUFFER_BYTES]
	__aligned(CONFIG_DCACHE_LINE_SIZE);

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(dmic_lfs,
				  CONFIG_DCACHE_LINE_SIZE,
				  CONFIG_FS_LITTLEFS_READ_SIZE,
				  CONFIG_FS_LITTLEFS_PROG_SIZE,
				  CONFIG_FS_LITTLEFS_CACHE_SIZE,
				  CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE);

static struct fs_mount_t dmic_lfs_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &dmic_lfs,
	.storage_dev = (void *)DT_FIXED_PARTITION_ID(
		DT_NODELABEL(dmic_payload_partition)),
	.mnt_point = "/lfs",
};

static const struct device *const dmic_dev = DEVICE_DT_GET(DMIC_NODE);
static const struct device *const i2s_tx_dev = DEVICE_DT_GET(DMIC_I2S_TX_NODE);

static struct pcm_stream_cfg dmic_stream;
static struct dmic_cfg dmic_config;

static enum dmic_sample_state dmic_state = DMIC_SHELL_IDLE;
static uint32_t dmic_output_bits = 24U;

static size_t total_bytes;
static size_t dump_offset;
static size_t last_ram_dump_offset;
static size_t last_ram_dump_bytes;
static uint32_t read_count;
static int last_result;
static bool dmic_lfs_mounted;
static char last_audio_path[SAMPLE_FS_PATH_MAX];
static char last_meta_path[SAMPLE_FS_PATH_MAX];

K_THREAD_STACK_DEFINE(capture_stack, CONFIG_DMIC_SHELL_CAPTURE_STACK_SIZE);
static struct k_thread capture_thread;

K_THREAD_STACK_DEFINE(file_writer_stack, CONFIG_DMIC_SHELL_WRITER_STACK_SIZE);
static struct k_thread file_writer_thread;

K_THREAD_STACK_DEFINE(play_stack, CONFIG_DMIC_SHELL_PLAY_STACK_SIZE);
static struct dmic_play_runtime play_runtime;

K_MSGQ_DEFINE(file_block_queue,
	      sizeof(struct dmic_file_block),
	      SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT,
	      4);

K_MSGQ_DEFINE(file_free_index_queue,
	      sizeof(uint16_t),
	      SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT,
	      4);

static atomic_t capture_stop_requested;
static atomic_t capture_thread_running;
static atomic_t file_writer_result;

static char capture_audio_path[SAMPLE_FS_PATH_MAX];
static char capture_meta_path[SAMPLE_FS_PATH_MAX];
static bool capture_to_file;

static struct fs_file_t audio_file;
static bool audio_file_opened;
static size_t file_writer_bytes;
static size_t file_fill;
static uint16_t file_fill_index;
static bool file_fill_acquired;

BUILD_ASSERT(SAMPLE_READ_CHUNK_BYTES != 0U,
	     "DMIC read chunk must not be zero");
BUILD_ASSERT((SAMPLE_READ_CHUNK_BYTES % SAMPLE_FRAME_BYTES) == 0U,
	     "DMIC read chunk must contain complete frames");
BUILD_ASSERT(SAMPLE_I2S_PLAY_BUFFER_BYTES >= 3840U,
	     "I2S play buffer must hold one 48 kHz stereo 32-bit slot block");
BUILD_ASSERT(SAMPLE_LFS_WRITE_CHUNK_BYTES > 0U,
	     "LittleFS write chunk must not be zero");
BUILD_ASSERT(SAMPLE_LFS_WRITE_CHUNK_BYTES <= SAMPLE_FILE_WRITE_BUFFER_BYTES,
	     "LittleFS write chunk must fit in staging buffer");
BUILD_ASSERT((SAMPLE_FILE_WRITE_BUFFER_BYTES % SAMPLE_LFS_WRITE_CHUNK_BYTES) == 0U,
	     "staging buffer must split into whole LittleFS write chunks");
BUILD_ASSERT(SAMPLE_RAM_FILE_WRITE_BUFFER_COUNT > 0U,
	     "RAM dump buffer must hold at least one file staging block");
BUILD_ASSERT(SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT <= UINT16_MAX,
	     "file staging index must fit in dmic_file_block");

static const char *dmic_state_name(enum dmic_sample_state state)
{
	switch (state) {
	case DMIC_SHELL_CONFIGURED:
		return "configured";
	case DMIC_SHELL_ACTIVE:
		return "active";
	default:
		return "idle";
	}
}

static int parse_u32(const char *text, uint32_t *value)
{
	char *end;
	unsigned long parsed;

	if ((text == NULL) || (value == NULL))
		return -EINVAL;

	parsed = strtoul(text, &end, 0);
	if ((end == text) || (*end != '\0') || (parsed > UINT32_MAX))
		return -EINVAL;

	*value = (uint32_t)parsed;
	return 0;
}

static bool rate_supported(uint32_t rate)
{
	return (rate == 16000U) || (rate == 44100U) || (rate == 48000U);
}

static bool bits_supported(uint32_t bits)
{
	return bits == 24U;
}

static bool channels_supported(uint32_t channels)
{
	return (channels >= 1U) && (channels <= SAMPLE_MAX_CHANNELS);
}

static uint32_t active_channels(void)
{
	uint32_t channels = dmic_config.channel.req_num_chan;

	if (!channels_supported(channels))
		return SAMPLE_MAX_CHANNELS;

	return channels;
}

static uint32_t stored_frame_bytes(void)
{
	return sizeof(uint32_t) * active_channels();
}

static uint32_t storage_bytes_per_second(uint32_t rate, uint32_t channels)
{
	return rate * sizeof(uint32_t) * channels;
}

static uint32_t seconds_for_bytes(size_t bytes, uint32_t bytes_per_sec)
{
	if (bytes_per_sec == 0U)
		return 0U;

	return (uint32_t)(bytes / bytes_per_sec);
}

static uint32_t read_timeout_ms(void)
{
	uint32_t rate = dmic_stream.pcm_rate;
	uint64_t chunk_ms;

	if (rate == 0U)
		rate = 48000U;

	chunk_ms = ((uint64_t)SAMPLE_AMIN_BLOCK_ENTRIES * 1000U +
		    rate - 1U) / rate;

	return (uint32_t)((chunk_ms * 2U) + 100U);
}

static uint32_t ram_max_seconds(void)
{
	uint64_t bytes_per_sec =
		(uint64_t)dmic_stream.pcm_rate * stored_frame_bytes();

	if (bytes_per_sec == 0U)
		return 0U;

	return (uint32_t)(sizeof(sample_ram_dump) / bytes_per_sec);
}

static bool ram_can_hold_seconds(uint32_t seconds)
{
	uint64_t bytes = (uint64_t)seconds *
			 dmic_stream.pcm_rate *
			 stored_frame_bytes();

	return bytes <= sizeof(sample_ram_dump);
}

static void reset_stats(void)
{
	total_bytes = 0U;
	dump_offset = 0U;
	last_ram_dump_offset = 0U;
	last_ram_dump_bytes = 0U;
	read_count = 0U;
	file_writer_bytes = 0U;
}

static uint8_t *file_write_buffer(uint16_t index)
{
	if (index < SAMPLE_FILE_WRITE_BUFFER_COUNT)
		return file_write_buffers[index];

	index -= SAMPLE_FILE_WRITE_BUFFER_COUNT;
	if (index >= SAMPLE_RAM_FILE_WRITE_BUFFER_COUNT)
		return NULL;

	return &sample_ram_dump[(size_t)index * SAMPLE_FILE_WRITE_BUFFER_BYTES];
}

static int fs_mount_if_needed(void)
{
	int ret;

	if (dmic_lfs_mounted)
		return 0;

	ret = fs_mount(&dmic_lfs_mount);
	if (ret == 0) {
		dmic_lfs_mounted = true;
		return 0;
	}

	LOG_WRN("LittleFS mount failed: %d; formatting", ret);

	ret = fs_mkfs(FS_LITTLEFS,
		      (uintptr_t)DT_FIXED_PARTITION_ID(
			      DT_NODELABEL(dmic_payload_partition)),
		      NULL,
		      0);
	if (ret != 0)
		return ret;

	ret = fs_mount(&dmic_lfs_mount);
	if (ret == 0)
		dmic_lfs_mounted = true;

	return ret;
}

static int lfs_free_bytes(size_t *free_bytes)
{
	struct fs_statvfs stat;
	size_t block_bytes;
	int ret;

	ret = fs_statvfs(dmic_lfs_mount.mnt_point, &stat);
	if (ret != 0)
		return ret;

	if (stat.f_blocks == 0U)
		return -EINVAL;

	block_bytes = DT_REG_SIZE(DT_NODELABEL(dmic_payload_partition)) /
		      stat.f_blocks;

	*free_bytes = (size_t)stat.f_bfree * block_bytes;

	return 0;
}

static uint32_t file_capacity_seconds_for(uint32_t rate, uint32_t channels)
{
	size_t free_bytes;
	uint32_t bytes_per_sec = storage_bytes_per_second(rate, channels);

	if (bytes_per_sec == 0U)
		return 0U;

	if (fs_mount_if_needed() != 0)
		return 0U;

	if (lfs_free_bytes(&free_bytes) != 0)
		return 0U;

	/* keep some margin for metadata and LittleFS overhead */
	if (free_bytes > (64U * 1024U))
		free_bytes -= (64U * 1024U);

	return seconds_for_bytes(free_bytes, bytes_per_sec);
}

static uint32_t file_stage_seconds_for(uint32_t rate, uint32_t channels)
{
	uint32_t bytes_per_sec = storage_bytes_per_second(rate, channels);
	size_t stage_bytes = SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT *
			     SAMPLE_FILE_WRITE_BUFFER_BYTES;

	return seconds_for_bytes(stage_bytes, bytes_per_sec);
}

static uint32_t file_practical_seconds_for(uint32_t rate, uint32_t channels)
{
	return MIN(file_capacity_seconds_for(rate, channels),
		   file_stage_seconds_for(rate, channels));
}

static int path_validate_relative(const char *path)
{
	if ((path == NULL) || (path[0] == '\0') || (path[0] == '/'))
		return -EINVAL;

	if ((strstr(path, "..") != NULL) || (strstr(path, "//") != NULL))
		return -EINVAL;

	return 0;
}

static int path_resolve(const char *relative, char *resolved, size_t size)
{
	int ret;

	ret = path_validate_relative(relative);
	if (ret != 0)
		return ret;

	ret = snprintk(resolved, size, "%s/%s",
		       dmic_lfs_mount.mnt_point,
		       relative);

	if ((ret < 0) || ((size_t)ret >= size))
		return -ENAMETOOLONG;

	return 0;
}

static int meta_path(const char *resolved, char *out, size_t out_size)
{
	int ret = snprintk(out, out_size, "%s.meta", resolved);

	return ((ret < 0) || ((size_t)ret >= out_size)) ?
		-ENAMETOOLONG : 0;
}

static bool path_has_meta_suffix(const char *path)
{
	size_t len;
	static const char suffix[] = ".meta";

	if (path == NULL)
		return false;

	len = strlen(path);
	return (len >= (sizeof(suffix) - 1U)) &&
	       (strcmp(&path[len - (sizeof(suffix) - 1U)], suffix) == 0);
}

static bool path_is_lfs_file(const char *path)
{
	size_t mount_len = strlen(dmic_lfs_mount.mnt_point);

	return (path != NULL) &&
	       (strncmp(path, dmic_lfs_mount.mnt_point, mount_len) == 0) &&
	       (path[mount_len] == '/') &&
	       (path[mount_len + 1U] != '\0');
}

static int mkdirs_for_path(const char *resolved_path)
{
	char path[SAMPLE_FS_PATH_MAX];
	char *cursor;
	int ret;

	ret = snprintk(path, sizeof(path), "%s", resolved_path);
	if ((ret < 0) || ((size_t)ret >= sizeof(path)))
		return -ENAMETOOLONG;

	cursor = path + strlen(dmic_lfs_mount.mnt_point) + 1U;

	while ((cursor = strchr(cursor, '/')) != NULL) {
		struct fs_dirent entry;

		*cursor = '\0';

		ret = fs_stat(path, &entry);
		if (ret == 0) {
			if (entry.type != FS_DIR_ENTRY_DIR)
				return -ENOTDIR;
		} else if (ret == -ENOENT) {
			ret = fs_mkdir(path);
			if ((ret != 0) && (ret != -EEXIST))
				return ret;
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
	int ret;

	ret = fs_stat(path, &entry);
	if (ret == -ENOENT) {
		return;
	}
	if (ret != 0) {
		LOG_WRN("stat %s failed before unlink: %d", path, ret);
		return;
	}

	ret = fs_unlink(path);
	if (ret != 0) {
		LOG_WRN("unlink %s failed: %d", path, ret);
	}
}

static void audio_file_close(void)
{
	if (audio_file_opened) {
		(void)fs_close(&audio_file);
		audio_file_opened = false;
	}
}

static int audio_file_prepare(const char *path)
{
	int ret;

	audio_file_close();
	fs_file_t_init(&audio_file);

	ret = fs_open(&audio_file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0)
		return ret;

	audio_file_opened = true;
	file_writer_bytes = 0U;

	return 0;
}

static int payload_write_meta(const char *meta_path,
			      const char *audio_path,
			      size_t stored_bytes)
{
	struct dmic_payload_meta meta = {
		.magic = SAMPLE_PAYLOAD_META_MAGIC,
		.version = SAMPLE_PAYLOAD_META_VERSION,
		.sample_rate = dmic_stream.pcm_rate,
		.channels = (uint8_t)active_channels(),
		.hardware_bits = dmic_output_bits,
		.container_bits = SAMPLE_CONTAINER_BITS,
		.stored_bytes = (uint32_t)stored_bytes,
	};
	struct fs_file_t file;
	ssize_t wrote;
	int ret;

	ret = snprintk(meta.audio_file, sizeof(meta.audio_file), "%s", audio_path);
	if ((ret < 0) || ((size_t)ret >= sizeof(meta.audio_file)))
		return -ENAMETOOLONG;

	LOG_INF("capture meta write: rate=%u channels=%u hw_bits=%u container_bits=%u stored_bytes=%u file=%s meta=%s",
		meta.sample_rate,
		meta.channels,
		meta.hardware_bits,
		meta.container_bits,
		meta.stored_bytes,
		meta.audio_file,
		meta_path);

	fs_file_t_init(&file);

	ret = fs_open(&file, meta_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0)
		return ret;

	wrote = fs_write(&file, &meta, sizeof(meta));
	ret = (wrote == sizeof(meta)) ? 0 :
	      ((wrote < 0) ? (int)wrote : -EIO);

	if ((fs_close(&file) != 0) && (ret == 0))
		ret = -EIO;

	return ret;
}

static int payload_read_meta(const char *resolved, struct dmic_payload_meta *meta)
{
	struct fs_file_t file;
	char mp[SAMPLE_FS_PATH_MAX];
	const char *meta_file = mp;
	ssize_t read_len;
	int ret;

	if (meta == NULL)
		return -EINVAL;

	if (path_has_meta_suffix(resolved)) {
		meta_file = resolved;
	} else {
		ret = meta_path(resolved, mp, sizeof(mp));
		if (ret != 0)
			return ret;
	}

	fs_file_t_init(&file);

	ret = fs_open(&file, meta_file, FS_O_READ);
	if (ret != 0)
		return ret;

	read_len = fs_read(&file, meta, sizeof(*meta));
	ret = fs_close(&file);
	if (ret != 0)
		return ret;

	if (read_len != (ssize_t)sizeof(*meta))
		return -EIO;

	if ((meta->magic != SAMPLE_PAYLOAD_META_MAGIC) ||
	    (meta->version != SAMPLE_PAYLOAD_META_VERSION) ||
	    (meta->sample_rate == 0U) ||
	    (meta->channels == 0U) ||
	    (meta->channels > SAMPLE_MAX_CHANNELS) ||
	    (meta->hardware_bits != 24U) ||
	    (meta->container_bits != SAMPLE_CONTAINER_BITS) ||
	    (meta->stored_bytes == 0U) ||
	    (meta->audio_file[0] == '\0')) {
		return -EINVAL;
	}

	return 0;
}

static size_t copy_pcm(uint8_t *dst, size_t dst_size,
		       const void *src, size_t src_size)
{
	size_t copy_bytes = MIN(dst_size, src_size);

	memcpy(dst, src, copy_bytes);
	return copy_bytes;
}

static int parse_bits_channels(uint32_t a, uint32_t b,
			       uint32_t *bits, uint32_t *channels)
{
	if (bits_supported(a) && channels_supported(b)) {
		*bits = a;
		*channels = b;
		return 0;
	}

	if (channels_supported(a) && bits_supported(b)) {
		*bits = b;
		*channels = a;
		return 0;
	}

	return -EINVAL;
}

static bool dmic_play_is_running(void);

static int cmd_dmic_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "DMIC sample");
	shell_print(sh, "  device          : %s (%s)",
		    dmic_dev->name,
		    device_is_ready(dmic_dev) ? "READY" : "NOT READY");
	shell_print(sh, "  i2s tx          : %s (%s)",
		    i2s_tx_dev->name,
		    device_is_ready(i2s_tx_dev) ? "READY" : "NOT READY");
	shell_print(sh, "  state           : %s", dmic_state_name(dmic_state));
	shell_print(sh, "  playback        : %s",
		    dmic_play_is_running() ? "active" : "idle");
	shell_print(sh, "  reads           : %u", read_count);
	shell_print(sh, "  captured bytes  : %u", (uint32_t)total_bytes);
	shell_print(sh, "  RAM dump bytes  : %u", (uint32_t)dump_offset);
	shell_print(sh, "  last RAM bytes  : %u", (uint32_t)last_ram_dump_bytes);
	shell_print(sh, "  file bytes      : %u", (uint32_t)file_writer_bytes);
	shell_print(sh, "  LittleFS        : %s",
		    dmic_lfs_mounted ? "mounted" : "not mounted");
	shell_print(sh,
		    "  cleanup         : fs rm /lfs/captures/<file> and fs rm /lfs/captures/<file>.meta");

	if (dmic_state != DMIC_SHELL_IDLE) {
		shell_print(sh,
			    "  format          : s32le rate=%u channels=%u hw_bits=%u block=%u",
			    dmic_stream.pcm_rate,
			    active_channels(),
			    dmic_output_bits,
			    (uint32_t)dmic_stream.block_size);
	}

	if (last_audio_path[0] != '\0')
		shell_print(sh, "  audio file      : %s", last_audio_path);

	if (last_meta_path[0] != '\0')
		shell_print(sh, "  metadata file   : %s", last_meta_path);

	shell_print(sh, "  last status     : %d", last_result);

	return 0;
}

static int cmd_dmic_caps(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint32_t bits;
	uint32_t channels;
	int ret;

	ARG_UNUSED(argc);

	ret = parse_u32(argv[1], &rate);
	ret |= parse_u32(argv[2], &bits);
	ret |= parse_u32(argv[3], &channels);

	if ((ret != 0) ||
	    !rate_supported(rate) ||
	    !bits_supported(bits) ||
	    !channels_supported(channels)) {
		shell_error(sh,
			    "supported: rate={16000,44100,48000} bits={24} channels={1,2}");
		return -EINVAL;
	}

	shell_print(sh,
		    "ram_max_sec=%u file_stage_sec=%u",
		    seconds_for_bytes(sizeof(sample_ram_dump),
				      storage_bytes_per_second(rate, channels)),
		    file_practical_seconds_for(rate, channels));

	return 0;
}

static int cmd_dmic_config(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint32_t first;
	uint32_t second;
	uint32_t bits;
	uint32_t channels;
	uint32_t block_size = SAMPLE_READ_CHUNK_BYTES;
	int ret;

	ret = parse_u32(argv[1], &rate);

	if (argc == 4U) {
		ret |= parse_u32(argv[2], &first);
		ret |= parse_u32(argv[3], &second);
	} else if (argc == 5U) {
		ret |= parse_u32(argv[2], &first);
		ret |= parse_u32(argv[3], &second);
		ret |= parse_u32(argv[4], &block_size);
	} else {
		shell_error(sh, "usage: dmic config <rate> <bits> <channels> [block]");
		return -EINVAL;
	}

	if ((ret != 0) ||
	    (parse_bits_channels(first, second, &bits, &channels) != 0)) {
		shell_error(sh, "usage: dmic config <rate> <bits> <channels> [block]");
		return -EINVAL;
	}

	if (!rate_supported(rate) ||
	    !bits_supported(bits) ||
	    !channels_supported(channels) ||
	    (block_size != SAMPLE_READ_CHUNK_BYTES)) {
		shell_error(sh,
			    "supported: rate={16000,44100,48000} bits={24} channels={1,2} block=%u",
			    SAMPLE_READ_CHUNK_BYTES);
		return -EINVAL;
	}

	if (!device_is_ready(dmic_dev)) {
		shell_error(sh, "DMIC device not ready");
		return -ENODEV;
	}

	if ((dmic_state == DMIC_SHELL_ACTIVE) ||
	    atomic_get(&capture_thread_running) ||
	    dmic_play_is_running()) {
		shell_error(sh, "stop capture/playback before reconfiguring");
		return -EBUSY;
	}

	memset(&dmic_stream, 0, sizeof(dmic_stream));
	memset(&dmic_config, 0, sizeof(dmic_config));

	dmic_output_bits = bits;

	dmic_stream.pcm_width = SAMPLE_CONTAINER_BITS;
	dmic_stream.mem_slab = &sample_dmic_slab;
	dmic_stream.pcm_rate = rate;
	dmic_stream.block_size = block_size;

	dmic_config.io.min_pdm_clk_freq = 1000000U;
	dmic_config.io.max_pdm_clk_freq = 3500000U;
	dmic_config.io.min_pdm_clk_dc = 40U;
	dmic_config.io.max_pdm_clk_dc = 60U;

	dmic_config.streams = &dmic_stream;
	dmic_config.channel.req_num_streams = 1U;
	dmic_config.channel.req_num_chan = channels;

	dmic_config.channel.req_chan_map_lo =
		dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);

	if (channels > 1U) {
		dmic_config.channel.req_chan_map_lo |=
			dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
	}

	ret = dmic_configure(dmic_dev, &dmic_config);
	last_result = ret;

	if (ret < 0) {
		shell_error(sh, "dmic_configure failed: %d", ret);
		dmic_state = DMIC_SHELL_IDLE;
		return ret;
	}

	dmic_state = DMIC_SHELL_CONFIGURED;
	reset_stats();

	shell_print(sh,
		    "configured: rate=%u container_bits=%u hw_bits=%u channels=%u block=%u",
		    rate,
		    SAMPLE_CONTAINER_BITS,
		    dmic_output_bits,
		    channels,
		    block_size);

	return 0;
}

static int dmic_start_capture(const struct shell *sh)
{
	int ret;

	if (dmic_state != DMIC_SHELL_CONFIGURED) {
		shell_error(sh, "configure first");
		return -EACCES;
	}

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	last_result = ret;

	if (ret < 0) {
		shell_error(sh, "dmic start failed: %d", ret);
		return ret;
	}

	dmic_state = DMIC_SHELL_ACTIVE;
	return 0;
}

static int audio_file_write_all(const uint8_t *buffer, size_t len)
{
	size_t off = 0U;

	while (off < len) {
		size_t chunk = MIN(SAMPLE_LFS_WRITE_CHUNK_BYTES, len - off);
		ssize_t wrote = fs_write(&audio_file, &buffer[off], chunk);

		if (wrote != (ssize_t)chunk)
			return (wrote < 0) ? (int)wrote : -EIO;

		off += chunk;
	}

	return 0;
}

static void file_writer_thread_fn(void *arg1, void *arg2, void *arg3)
{
	struct dmic_file_block item;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (k_msgq_get(&file_block_queue, &item, K_FOREVER) == 0) {
		if (item.final)
			break;

		if (atomic_get(&file_writer_result) == 0) {
			int ret;
			uint8_t *buffer = file_write_buffer(item.index);

			if (buffer == NULL) {
				atomic_set(&file_writer_result, -EINVAL);
				(void)k_msgq_put(&file_free_index_queue,
						  &item.index,
						  K_NO_WAIT);
				continue;
			}

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
			sys_cache_data_flush_range(buffer, item.size);
#endif

			ret = audio_file_write_all(buffer, item.size);
			if (ret != 0)
				atomic_set(&file_writer_result, ret);
			else
				file_writer_bytes += item.size;
		}

		(void)k_msgq_put(&file_free_index_queue, &item.index, K_NO_WAIT);
	}
}

static void capture_thread_fn(void *arg1, void *arg2, void *arg3)
{
	const struct shell *sh = arg1;
	uint32_t seconds = POINTER_TO_UINT(arg2);
	int64_t end_ms = (seconds == 0U) ? 0 :
			 (k_uptime_get() + ((int64_t)seconds * 1000LL));
	bool stream_to_file = capture_to_file;
	bool writer_started = false;
	bool stopped_early = false;
	size_t ram_dump_start = dump_offset;
	size_t target_dump_offset = 0U;
	size_t target_file_bytes = 0U;
	size_t stored_capture_bytes = 0U;
	size_t written_total = 0U;
	uint32_t completed_blocks = 0U;
	int ret = 0;

	ARG_UNUSED(arg3);

	if (stream_to_file) {
		k_msgq_purge(&file_block_queue);
		k_msgq_purge(&file_free_index_queue);
		for (uint16_t i = 0U;
		     i < SAMPLE_FILE_WRITE_TOTAL_BUFFER_COUNT;
		     i++) {
			(void)k_msgq_put(&file_free_index_queue, &i, K_NO_WAIT);
		}
		atomic_set(&file_writer_result, 0);
		file_writer_bytes = 0U;
		file_fill = 0U;
		file_fill_index = 0U;
		file_fill_acquired = false;
		target_file_bytes =
			(seconds == 0U) ? 0U :
			((size_t)seconds *
			 dmic_stream.pcm_rate *
			 stored_frame_bytes());

		/*
		 * File capture follows the I2S shell model: stream DMIC blocks
		 * through staging buffers to a dedicated LittleFS writer thread.
		 * If storage cannot keep up, abort instead of stretching capture
		 * time and creating a file with skipped or uneven audio.
		 */
		k_thread_create(&file_writer_thread,
				file_writer_stack,
				K_THREAD_STACK_SIZEOF(file_writer_stack),
				file_writer_thread_fn,
				NULL,
				NULL,
				NULL,
				K_PRIO_PREEMPT(6),
				0,
				K_NO_WAIT);
		writer_started = true;
	} else {
		target_dump_offset =
			dump_offset +
			((size_t)seconds *
			 dmic_stream.pcm_rate *
			 stored_frame_bytes());
	}

	while ((seconds == 0U) || (k_uptime_get() < end_ms)) {
		void *buffer = NULL;
		uint32_t size = 0U;

		if (atomic_get(&capture_stop_requested)) {
			stopped_early = true;
			break;
		}

		if (writer_started &&
		    (atomic_get(&file_writer_result) != 0)) {
			ret = (int)atomic_get(&file_writer_result);
			break;
		}

		ret = dmic_read(dmic_dev, 0U, &buffer, &size,
				read_timeout_ms());
		last_result = ret;

		if (ret < 0) {
			if (ret == -EAGAIN) {
				ret = 0;
				continue;
			}

			shell_error(sh, "dmic_read failed: %d", ret);
			break;
		}

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_invd_range(buffer, size);
#endif

		total_bytes += size;
		read_count++;
		completed_blocks++;

		if (stream_to_file) {
			size_t store_size = size;
			size_t off = 0U;

			if (target_file_bytes > 0U) {
				size_t remaining;

				if (stored_capture_bytes >= target_file_bytes) {
					k_mem_slab_free(&sample_dmic_slab, buffer);
					buffer = NULL;
					break;
				}

				remaining = target_file_bytes - stored_capture_bytes;

				store_size = MIN(store_size, remaining);
			}

			while (off < store_size) {
				size_t take;

				if (!file_fill_acquired) {
					ret = k_msgq_get(&file_free_index_queue,
							 &file_fill_index,
							 K_NO_WAIT);
					if (ret != 0) {
						shell_error(sh,
							    "file staging buffers full at block=%u stored=%u bytes; LittleFS writer is slower than DMIC capture",
							    completed_blocks,
							    (uint32_t)stored_capture_bytes);
						ret = -EOVERFLOW;
						break;
					}

					file_fill_acquired = true;
					file_fill = 0U;
				}

				take = MIN(SAMPLE_FILE_WRITE_BUFFER_BYTES - file_fill,
					   store_size - off);

				memcpy(&file_write_buffer(file_fill_index)[file_fill],
				       &((const uint8_t *)buffer)[off],
				       take);
				file_fill += take;
				off += take;
				stored_capture_bytes += take;

				if (file_fill == SAMPLE_FILE_WRITE_BUFFER_BYTES) {
					struct dmic_file_block item = {
						.index = file_fill_index,
						.size = file_fill,
						.final = false,
					};

					ret = k_msgq_put(&file_block_queue,
							 &item,
							 K_NO_WAIT);
					if (ret != 0) {
						(void)k_msgq_put(&file_free_index_queue,
								  &file_fill_index,
								  K_NO_WAIT);
						file_fill_acquired = false;
						ret = -EOVERFLOW;
						break;
					}

					file_fill = 0U;
					file_fill_acquired = false;
				}
			}

			k_mem_slab_free(&sample_dmic_slab, buffer);
			buffer = NULL;

			if (ret != 0)
				break;

			if ((target_file_bytes > 0U) &&
			    (stored_capture_bytes >= target_file_bytes)) {
				break;
			}
		} else {
			size_t dst_remaining;
			size_t copy_bytes;

			dst_remaining =
				MIN(sizeof(sample_ram_dump),
				    target_dump_offset) -
				dump_offset;

			copy_bytes = copy_pcm(&sample_ram_dump[dump_offset],
					      dst_remaining,
					      buffer,
					      size);
			dump_offset += copy_bytes;
		}

		if (buffer != NULL)
			k_mem_slab_free(&sample_dmic_slab, buffer);
	}

	if (atomic_get(&capture_stop_requested))
		stopped_early = true;

	{
		int stop_ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);

		if (stop_ret < 0) {
			shell_error(sh, "dmic stop failed: %d", stop_ret);
			if (ret == 0)
				ret = stop_ret;
		} else {
			dmic_state = DMIC_SHELL_CONFIGURED;
		}
	}

	if (stream_to_file) {
		struct dmic_file_block stop_item = {
			.final = true,
		};

		if (writer_started) {
			if (file_fill_acquired && (file_fill > 0U)) {
				struct dmic_file_block item = {
					.index = file_fill_index,
					.size = file_fill,
					.final = false,
				};

				(void)k_msgq_put(&file_block_queue,
						 &item,
						 K_FOREVER);
				file_fill = 0U;
				file_fill_acquired = false;
			}

			(void)k_msgq_put(&file_block_queue,
					 &stop_item,
					 K_FOREVER);
			(void)k_thread_join(&file_writer_thread, K_FOREVER);
		}

		written_total = file_writer_bytes;

		if ((ret == 0) && (atomic_get(&file_writer_result) != 0))
			ret = (int)atomic_get(&file_writer_result);

		audio_file_close();

		if ((ret == 0) && (written_total > 0U)) {
			ret = payload_write_meta(capture_meta_path,
						 capture_audio_path,
						 written_total);
			if (ret == 0) {
				strncpy(last_audio_path,
					capture_audio_path,
					sizeof(last_audio_path) - 1U);
				last_audio_path[sizeof(last_audio_path) - 1U] = '\0';

				strncpy(last_meta_path,
					capture_meta_path,
					sizeof(last_meta_path) - 1U);
				last_meta_path[sizeof(last_meta_path) - 1U] = '\0';
			}
		}

		if (ret == -EOVERFLOW) {
			shell_error(sh,
				    "file capture dropped timing; discard this file and use a shorter capture, larger staging buffer, or faster storage");
		}

		if (ret != 0) {
			unlink_if_exists(capture_meta_path);
			unlink_if_exists(capture_audio_path);
		}
	}

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	if (!stream_to_file && (dump_offset > 0U))
		sys_cache_data_flush_range(sample_ram_dump, dump_offset);
#endif

	shell_print(sh,
		    "%s: blocks=%u read_bytes=%u stored_bytes=%u status=%d",
		    (ret == 0) ?
		    (stopped_early ? "capture stopped" : "capture complete") :
		    "capture failed",
		    completed_blocks,
		    (uint32_t)total_bytes,
		    (uint32_t)(stream_to_file ? written_total :
			       (dump_offset - ram_dump_start)),
		    ret);

	if (!stream_to_file && (dump_offset > ram_dump_start)) {
		uintptr_t dst_start = (uintptr_t)&sample_ram_dump[ram_dump_start];
		uintptr_t dst_end = (uintptr_t)&sample_ram_dump[dump_offset];

		last_ram_dump_offset = ram_dump_start;
		last_ram_dump_bytes = dump_offset - ram_dump_start;

		shell_print(sh,
			    "RAM capture saved in memory: start=0x%08x end=0x%08x bytes=%u",
			    (uint32_t)dst_start,
			    (uint32_t)dst_end,
			    (uint32_t)last_ram_dump_bytes);
		shell_print(sh,
			    "To save it: dump binary memory dmic_audio.bin 0x%08x 0x%08x",
			    (uint32_t)dst_start,
			    (uint32_t)dst_end);
	}

	last_result = ret;
	capture_to_file = false;
	atomic_clear(&capture_stop_requested);
	atomic_clear(&capture_thread_running);
}


static bool dmic_play_is_running(void)
{
	bool running;

	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	running = play_runtime.tid != NULL;
	k_mutex_unlock(&play_runtime.lock);

	return running;
}

static bool dmic_play_stop_requested(void)
{
	bool stop;

	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	stop = play_runtime.stop;
	k_mutex_unlock(&play_runtime.lock);

	return stop;
}

static void dmic_play_request_stop(void)
{
	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	if (play_runtime.tid != NULL)
		play_runtime.stop = true;
	k_mutex_unlock(&play_runtime.lock);
}


static uint16_t i2s_options(void)
{
	return I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER;
}

static uint8_t meta_word_size(const struct dmic_payload_meta *meta)
{
	return (uint8_t)meta->hardware_bits;
}

static uint8_t meta_channels(const struct dmic_payload_meta *meta)
{
	return (uint8_t)meta->channels;
}

static uint8_t meta_i2s_channels(const struct dmic_payload_meta *meta)
{
	return MAX(meta_channels(meta), 2U);
}

static uint8_t meta_storage_word_bytes(const struct dmic_payload_meta *meta)
{
	ARG_UNUSED(meta);

	return sizeof(uint32_t);
}

static uint32_t playback_sample_to_i2s(const struct dmic_payload_meta *meta,
				       const uint8_t *p)
{
	uint8_t storage_bytes = meta_storage_word_bytes(meta);
	uint32_t sample;

	switch (storage_bytes) {
	case 1U:
		sample = ((uint32_t)p[0]) << 24;
		break;
	case 2U:
		sample = ((uint32_t)((const uint16_t *)p)[0]) << 16;
		break;
	default:
		sample = ((const uint32_t *)p)[0];
		break;
	}

	if ((storage_bytes < sizeof(uint32_t)) && (meta->hardware_bits < 32U))
		sample <<= (32U - meta->hardware_bits);

	return sample;
}

static size_t i2s_play_block_size(uint32_t rate, uint8_t bits, uint8_t channels)
{
	uint32_t frame_bytes = channels * sizeof(uint32_t);
	uint32_t frames = MAX((rate * SAMPLE_I2S_PLAY_BLOCK_MS) / 1000U, 1U);
	size_t block_size;

	ARG_UNUSED(bits);

	block_size = ROUND_UP((size_t)frames * frame_bytes, frame_bytes);

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	block_size = ROUND_UP(block_size, CONFIG_DCACHE_LINE_SIZE);
#endif

	return block_size;
}

static void i2s_reset_tx(void)
{
	struct i2s_config cfg = { 0 };

	(void)i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	(void)i2s_configure(i2s_tx_dev, I2S_DIR_TX, &cfg);
}

static int i2s_configure_tx(const struct dmic_payload_meta *meta)
{
	struct i2s_config cfg = {
		.word_size = meta_word_size(meta),
		.channels = meta_i2s_channels(meta),
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = i2s_options(),
		.frame_clk_freq = meta->sample_rate,
		.mem_slab = &sample_i2s_tx_slab,
		.block_size = i2s_play_block_size(meta->sample_rate,
						      meta_word_size(meta),
						      meta_i2s_channels(meta)),
		.timeout = SAMPLE_I2S_PLAY_TIMEOUT_MS,
	};

	return i2s_configure(i2s_tx_dev, I2S_DIR_TX, &cfg);
}


static size_t i2s_file_block_size(const struct dmic_payload_meta *meta)
{
	size_t tx_channels = meta_i2s_channels(meta);
	size_t frames = i2s_play_block_size(meta->sample_rate,
					    meta_word_size(meta),
					    tx_channels) /
			(tx_channels * sizeof(uint32_t));

	return frames * meta_channels(meta) * meta_storage_word_bytes(meta);
}

static int i2s_write_block(void *block, size_t len)
{
	int64_t deadline = k_uptime_get() + SAMPLE_I2S_PLAY_TIMEOUT_MS;
	int ret;

	do {
		ret = i2s_write(i2s_tx_dev, block, len);
		if (ret == -EAGAIN)
			k_msleep(1);
	} while ((ret == -EAGAIN) && (k_uptime_get() < deadline));

	return ret;
}

static int i2s_queue_file_block(const struct dmic_payload_meta *meta,
				 const uint8_t *raw,
				 size_t raw_len)
{
	size_t src_channels = meta_channels(meta);
	size_t tx_channels = meta_i2s_channels(meta);
	size_t sample_bytes = meta_storage_word_bytes(meta);
	size_t frames;
	size_t off = 0U;

	if ((sample_bytes == 0U) || (src_channels == 0U) ||
	    ((raw_len % (src_channels * sample_bytes)) != 0U))
		return -EINVAL;

	frames = raw_len / (src_channels * sample_bytes);

	while (off < frames) {
		void *block;
		size_t bs = i2s_play_block_size(meta->sample_rate,
						  meta_word_size(meta),
						  tx_channels);
		size_t max_frames = bs / (tx_channels * sizeof(uint32_t));
		size_t n = MIN(max_frames, frames - off);
		uint32_t *out;
		int ret;

		ret = k_mem_slab_alloc(&sample_i2s_tx_slab,
				       &block,
				       K_MSEC(SAMPLE_I2S_PLAY_TIMEOUT_MS));
		if (ret != 0)
			return ret;

		out = block;

		for (size_t frame = 0U; frame < n; frame++) {
			uint32_t first_sample = 0U;

			for (size_t ch = 0U; ch < tx_channels; ch++) {
				uint32_t sample = first_sample;

				if (ch < src_channels) {
					size_t src_sample =
						((off + frame) * src_channels) + ch;
					const uint8_t *p =
						&raw[src_sample * sample_bytes];

					sample = playback_sample_to_i2s(meta, p);
					if (ch == 0U)
						first_sample = sample;
				}

				*out++ = sample;
			}
		}

		memset(out, 0, bs - (n * tx_channels * sizeof(uint32_t)));

		ret = i2s_write_block(block, bs);
		if (ret != 0) {
			k_mem_slab_free(&sample_i2s_tx_slab, block);
			return ret;
		}

		off += n;
	}

	return 0;
}

static int dmic_play_xspi(const char *file)
{
	char resolved[SAMPLE_FS_PATH_MAX];
	struct dmic_payload_meta meta;
	struct fs_file_t audio;
	size_t raw_remaining = 0U;
	size_t frame_bytes;
	uint32_t primed = 0U;
	uint32_t duration_ms;
	bool audio_opened = false;
	bool reset_tx = true;
	int ret;

	if ((dmic_state == DMIC_SHELL_ACTIVE) || atomic_get(&capture_thread_running))
		return -EBUSY;

	if (!device_is_ready(i2s_tx_dev))
		return -ENODEV;

	ret = fs_mount_if_needed();
	if (ret != 0)
		return ret;

	ret = path_resolve(file, resolved, sizeof(resolved));
	if (ret != 0)
		return ret;

	ret = payload_read_meta(resolved, &meta);
	if (ret != 0)
		return ret;

	if (!path_is_lfs_file(meta.audio_file))
		return -EINVAL;

	duration_ms = (uint32_t)(((uint64_t)meta.stored_bytes * 1000U) /
				 ((uint64_t)meta.sample_rate *
				  meta.channels *
				  meta_storage_word_bytes(&meta)));

	LOG_INF("play meta read: rate=%u channels=%u hw_bits=%u container_bits=%u stored_bytes=%u duration_ms=%u file=%s",
		meta.sample_rate,
		meta.channels,
		meta.hardware_bits,
		meta.container_bits,
		meta.stored_bytes,
		duration_ms,
		meta.audio_file);

	fs_file_t_init(&audio);
	ret = fs_open(&audio, meta.audio_file, FS_O_READ);
	if (ret != 0)
		return ret;
	audio_opened = true;

	ret = i2s_configure_tx(&meta);
	if (ret != 0)
		goto out_close;

	raw_remaining = meta.stored_bytes;
	frame_bytes = meta_channels(&meta) * meta_storage_word_bytes(&meta);

	while ((primed < SAMPLE_I2S_TX_PRIME_BLOCKS) &&
	       (raw_remaining > 0U) && !dmic_play_stop_requested()) {
		size_t chunk = MIN(i2s_file_block_size(&meta), raw_remaining);
		ssize_t read_len;

		chunk = (chunk / frame_bytes) * frame_bytes;
		if (chunk == 0U) {
			ret = -EINVAL;
			goto out_reset;
		}

		read_len = fs_read(&audio,
				   sample_i2s_play_scratch[primed & 1U],
				   chunk);
		if (read_len != (ssize_t)chunk) {
			ret = (read_len < 0) ? (int)read_len : -EIO;
			goto out_reset;
		}

		ret = i2s_queue_file_block(&meta,
					   sample_i2s_play_scratch[primed & 1U],
					   chunk);
		if (ret != 0)
			goto out_reset;

		raw_remaining -= chunk;
		primed++;
	}

	if (dmic_play_stop_requested()) {
		ret = 0;
		goto out_reset;
	}

	ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0)
		goto out_reset;

	while ((raw_remaining > 0U) && !dmic_play_stop_requested()) {
		size_t chunk = MIN(i2s_file_block_size(&meta), raw_remaining);
		ssize_t read_len;

		chunk = (chunk / frame_bytes) * frame_bytes;
		if (chunk == 0U) {
			ret = -EINVAL;
			goto out_reset;
		}

		read_len = fs_read(&audio, sample_i2s_play_scratch[0], chunk);
		if (read_len != (ssize_t)chunk) {
			ret = (read_len < 0) ? (int)read_len : -EIO;
			goto out_reset;
		}

		ret = i2s_queue_file_block(&meta,
					   sample_i2s_play_scratch[0],
					   chunk);
		if (ret != 0)
			goto out_reset;

		raw_remaining -= chunk;
	}

	if (dmic_play_stop_requested()) {
		ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		if (ret == 0) {
			reset_tx = false;
		}
	} else {
		ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
		if (ret == 0) {
			reset_tx = false;
			k_msleep((SAMPLE_I2S_PLAY_BLOCK_COUNT + 16U) *
				 SAMPLE_I2S_PLAY_BLOCK_MS);
		} else if (ret == -EIO) {
			ret = 0;
			reset_tx = false;
		}
	}

out_reset:
	if (reset_tx)
		i2s_reset_tx();
out_close:
	if (audio_opened)
		(void)fs_close(&audio);
	return ret;
}

static int dmic_play_ram(void)
{
	struct dmic_payload_meta meta = {
		.magic = SAMPLE_PAYLOAD_META_MAGIC,
		.version = SAMPLE_PAYLOAD_META_VERSION,
		.sample_rate = dmic_stream.pcm_rate,
		.channels = active_channels(),
		.hardware_bits = dmic_output_bits,
		.container_bits = SAMPLE_CONTAINER_BITS,
		.stored_bytes = last_ram_dump_bytes,
	};
	const uint8_t *raw = &sample_ram_dump[last_ram_dump_offset];
	size_t raw_remaining = last_ram_dump_bytes;
	size_t raw_offset = 0U;
	size_t frame_bytes = meta_channels(&meta) *
			     meta_storage_word_bytes(&meta);
	uint32_t primed = 0U;
	uint32_t duration_ms;
	bool reset_tx = true;
	int ret;

	if ((dmic_state == DMIC_SHELL_ACTIVE) || atomic_get(&capture_thread_running))
		return -EBUSY;

	if (!device_is_ready(i2s_tx_dev))
		return -ENODEV;

	if (last_ram_dump_bytes == 0U)
		return -ENOENT;

	duration_ms = (uint32_t)(((uint64_t)meta.stored_bytes * 1000U) /
				 ((uint64_t)meta.sample_rate *
				  meta.channels *
				  meta_storage_word_bytes(&meta)));

	LOG_INF("play RAM: rate=%u channels=%u hw_bits=%u container_bits=%u stored_bytes=%u duration_ms=%u offset=%u",
		meta.sample_rate,
		meta.channels,
		meta.hardware_bits,
		meta.container_bits,
		meta.stored_bytes,
		duration_ms,
		(uint32_t)last_ram_dump_offset);

	ret = i2s_configure_tx(&meta);
	if (ret != 0)
		return ret;

	while ((primed < SAMPLE_I2S_TX_PRIME_BLOCKS) &&
	       (raw_remaining > 0U) && !dmic_play_stop_requested()) {
		size_t chunk = MIN(i2s_file_block_size(&meta), raw_remaining);

		chunk = (chunk / frame_bytes) * frame_bytes;
		if (chunk == 0U) {
			ret = -EINVAL;
			goto out_reset;
		}

		ret = i2s_queue_file_block(&meta, &raw[raw_offset], chunk);
		if (ret != 0)
			goto out_reset;

		raw_offset += chunk;
		raw_remaining -= chunk;
		primed++;
	}

	if (dmic_play_stop_requested()) {
		ret = 0;
		goto out_reset;
	}

	ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0)
		goto out_reset;

	while ((raw_remaining > 0U) && !dmic_play_stop_requested()) {
		size_t chunk = MIN(i2s_file_block_size(&meta), raw_remaining);

		chunk = (chunk / frame_bytes) * frame_bytes;
		if (chunk == 0U) {
			ret = -EINVAL;
			goto out_reset;
		}

		ret = i2s_queue_file_block(&meta, &raw[raw_offset], chunk);
		if (ret != 0)
			goto out_reset;

		raw_offset += chunk;
		raw_remaining -= chunk;
	}

	if (dmic_play_stop_requested()) {
		ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
		if (ret == 0)
			reset_tx = false;
	} else {
		ret = i2s_trigger(i2s_tx_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
		if (ret == 0) {
			reset_tx = false;
			k_msleep((SAMPLE_I2S_PLAY_BLOCK_COUNT + 16U) *
				 SAMPLE_I2S_PLAY_BLOCK_MS);
		} else if (ret == -EIO) {
			ret = 0;
			reset_tx = false;
		}
	}

out_reset:
	if (reset_tx)
		i2s_reset_tx();

	return ret;
}

static void dmic_play_thread_fn(void *arg1, void *arg2, void *arg3)
{
	char path[SAMPLE_FS_PATH_MAX];
	bool ram;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	strncpy(path, play_runtime.path, sizeof(path) - 1U);
	path[sizeof(path) - 1U] = '\0';
	ram = play_runtime.ram;
	k_mutex_unlock(&play_runtime.lock);

	ret = ram ? dmic_play_ram() : dmic_play_xspi(path);

	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	play_runtime.result = ret;
	play_runtime.stop = false;
	play_runtime.tid = NULL;
	k_mutex_unlock(&play_runtime.lock);

	last_result = ret;
	LOG_INF("playback complete: %s status=%d",
		ram ? "RAM" : path,
		ret);
}

static int dmic_play_start(const char *file)
{
	int ret = 0;
	bool ram = (file == NULL);

	if (!ram) {
		ret = path_validate_relative(file);
		if (ret != 0)
			return ret;
	}

	if ((dmic_state == DMIC_SHELL_ACTIVE) || atomic_get(&capture_thread_running))
		return -EBUSY;

	if (ram && (last_ram_dump_bytes == 0U))
		return -ENOENT;

	k_mutex_lock(&play_runtime.lock, K_FOREVER);
	if (play_runtime.tid != NULL) {
		ret = -EBUSY;
	} else {
		if (ram) {
			play_runtime.path[0] = '\0';
		} else {
			strncpy(play_runtime.path, file, sizeof(play_runtime.path) - 1U);
			play_runtime.path[sizeof(play_runtime.path) - 1U] = '\0';
		}
		play_runtime.ram = ram;
		play_runtime.stop = false;
		play_runtime.result = 0;
		play_runtime.tid = k_thread_create(&play_runtime.thread,
						      play_stack,
						      K_THREAD_STACK_SIZEOF(play_stack),
						      dmic_play_thread_fn,
						      NULL,
						      NULL,
						      NULL,
						      SAMPLE_PLAY_THREAD_PRIORITY,
						      0,
						      K_NO_WAIT);
	}
	k_mutex_unlock(&play_runtime.lock);

	return ret;
}

static int cmd_dmic_stop(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (atomic_get(&capture_thread_running)) {
		atomic_set(&capture_stop_requested, 1);
		shell_print(sh, "capture stop requested");
		return 0;
	}

	if (dmic_play_is_running()) {
		dmic_play_request_stop();
		shell_print(sh, "playback stop requested");
		return 0;
	}

	if (dmic_state != DMIC_SHELL_ACTIVE) {
		shell_error(sh, "no active capture or playback");
		return -EALREADY;
	}

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
	last_result = ret;

	if (ret < 0) {
		shell_error(sh, "dmic stop failed: %d", ret);
		return ret;
	}

	dmic_state = DMIC_SHELL_CONFIGURED;
	shell_print(sh, "stopped");

	return 0;
}

static int cmd_dmic_capture(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t seconds = 0U;
	bool stream_to_file = false;
	const char *file_arg = NULL;
	int ret;

	if (argc < 2U) {
		shell_error(sh,
			    "usage: dmic capture <seconds> [file] | dmic capture <file>");
		return -EINVAL;
	}

	ret = parse_u32(argv[1], &seconds);
	if (ret != 0) {
		if (argc != 2U) {
			shell_error(sh,
				    "usage: dmic capture <seconds> [file] | dmic capture <file>");
			return -EINVAL;
		}

		stream_to_file = true;
		file_arg = argv[1];
		seconds = 0U;
	} else {
		if (seconds == 0U) {
			shell_error(sh, "seconds must be nonzero unless file-only capture is used");
			return -EINVAL;
		}

		if (argc >= 3U) {
			stream_to_file = true;
			file_arg = argv[2];
		}
	}

	if (atomic_get(&capture_thread_running)) {
		shell_error(sh, "capture already running");
		return -EBUSY;
	}

	if (dmic_state != DMIC_SHELL_CONFIGURED) {
		shell_error(sh, "configure first");
		return -EACCES;
	}

	if (stream_to_file) {
		uint32_t capacity_seconds = 0U;

		ret = fs_mount_if_needed();
		if (ret != 0) {
			shell_error(sh, "LittleFS unavailable: %d", ret);
			return ret;
		}

		ret = path_resolve(file_arg,
				   capture_audio_path,
				   sizeof(capture_audio_path));
		if (ret != 0) {
			shell_error(sh, "invalid audio path");
			return ret;
		}

		ret = meta_path(capture_audio_path,
				capture_meta_path,
				sizeof(capture_meta_path));
		if (ret != 0) {
			shell_error(sh, "metadata path too long");
			return ret;
		}

		ret = mkdirs_for_path(capture_audio_path);
		if (ret != 0) {
			shell_error(sh, "mkdir failed: %d", ret);
			return ret;
		}

		unlink_if_exists(capture_audio_path);
		unlink_if_exists(capture_meta_path);

		capacity_seconds =
			file_capacity_seconds_for(dmic_stream.pcm_rate,
						  active_channels());

		if ((seconds > 0U) && (seconds > capacity_seconds)) {
			shell_error(sh,
				    "requested %u sec exceeds LittleFS available limit %u sec for this format",
				    seconds,
				    capacity_seconds);
			return -ENOSPC;
		}

		ret = audio_file_prepare(capture_audio_path);
		if (ret != 0) {
			shell_error(sh, "audio file open failed: %d", ret);
			return ret;
		}
	} else if (!ram_can_hold_seconds(seconds)) {
		shell_error(sh,
			    "RAM buffer holds up to %u seconds",
			    ram_max_seconds());
		return -ENOMEM;
	}

	reset_stats();

	ret = dmic_start_capture(sh);
	if (ret != 0) {
		audio_file_close();
		return ret;
	}

	atomic_clear(&capture_stop_requested);
	capture_to_file = stream_to_file;
	atomic_set(&capture_thread_running, 1);

	k_thread_create(&capture_thread,
			capture_stack,
			K_THREAD_STACK_SIZEOF(capture_stack),
			capture_thread_fn,
			(void *)sh,
			UINT_TO_POINTER(seconds),
			NULL,
			K_PRIO_PREEMPT(8),
			0,
			K_NO_WAIT);

	if (stream_to_file) {
		if (seconds > 0U) {
			shell_print(sh,
				    "capture started: %u seconds streaming to file audio=%s meta=%s",
				    seconds,
				    capture_audio_path,
				    capture_meta_path);
		} else {
			shell_print(sh,
				    "capture started: streaming to file audio=%s meta=%s",
				    capture_audio_path,
				    capture_meta_path);
		}
	} else {
		shell_print(sh, "capture started: %u seconds to RAM", seconds);
	}

	return 0;
}


static int cmd_dmic_play(const struct shell *sh, size_t argc, char **argv)
{
	const char *file = (argc >= 2U) ? argv[1] : NULL;
	int ret;

	if (argc > 2U) {
		shell_error(sh, "usage: dmic play [meta-file]");
		return -EINVAL;
	}

	ret = dmic_play_start(file);
	last_result = ret;

	if (ret != 0) {
		shell_error(sh, "play start failed: %d", ret);
		return ret;
	}

	shell_print(sh, "play started: %s", (file == NULL) ? "RAM" : file);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	dmic_cmds,
	SHELL_CMD_ARG(info, NULL,
		      "Show DMIC state",
		      cmd_dmic_info, 1, 0),
	SHELL_CMD_ARG(caps, NULL,
		      "Show capacity: dmic caps <rate> <bits> <channels>",
		      cmd_dmic_caps, 4, 0),
	SHELL_CMD_ARG(config, NULL,
		      "Configure: dmic config <rate> <bits> <channels> [block]",
		      cmd_dmic_config, 4, 1),
	SHELL_CMD_ARG(stop, NULL,
		      "Stop capture/playback",
		      cmd_dmic_stop, 1, 0),
	SHELL_CMD_ARG(capture, NULL,
		      "Capture: dmic capture <seconds> [file] | dmic capture <file>",
		      cmd_dmic_capture, 2, 1),
	SHELL_CMD_ARG(play, NULL,
		      "Playback RAM or file over I2S: dmic play [meta-file]",
		      cmd_dmic_play, 1, 1),

	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(dmic, &dmic_cmds, "DMIC capture commands", NULL);

int main(void)
{
	int ret;

	k_mutex_init(&play_runtime.lock);

	if (!device_is_ready(i2s_tx_dev))
		LOG_WRN("I2S TX device not ready: %s", i2s_tx_dev->name);

	ret = fs_mount_if_needed();
	if (ret != 0)
		LOG_WRN("LittleFS unavailable: %d", ret);

	LOG_INF("DMIC shell ready. Use: dmic help");

	return 0;
}
