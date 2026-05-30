/*
 * shell_lfs_test.c — LFS write/read throughput and CRC32 integrity test.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/crc.h>

#define TEST_PATH       "/lfs/lfstest.bin"
#define DEFAULT_SIZE_KB 1
#define MAX_SIZE_KB     8192
#define CHUNK_SIZE      512
#define LFSR_SEED       0xDEADBEEFu

static uint32_t _lfsr32(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void _fill_buf(uint8_t *p_buf, size_t len, uint32_t *p_state)
{
    for (size_t i = 0; i < len; i += 4)
    {
        *p_state         = _lfsr32(*p_state);
        size_t remaining = len - i;
        size_t copy      = (remaining < 4) ? remaining : 4;
        memcpy(&p_buf[i], p_state, copy);
    }
}

/* Re-reads the file and compares against a freshly regenerated stream to
 * report the exact offset and bytes of the first mismatch.
 */
static void _diagnose_mismatch(const struct shell *sh, uint32_t test_size)
{
    static uint8_t   file_buf[CHUNK_SIZE] = {0};
    static uint8_t   ref_buf[CHUNK_SIZE]  = {0};
    struct fs_file_t f;
    uint32_t         lfsr_state = LFSR_SEED;
    uint32_t         offset     = 0;

    fs_file_t_init(&f);
    int ret = fs_open(&f, TEST_PATH, FS_O_READ);
    if (ret)
    {
        shell_error(sh, "Diag open: %d", ret);
        return;
    }

    while (offset < test_size)
    {
        uint32_t chunk = (test_size - offset < CHUNK_SIZE) ? (test_size - offset) : CHUNK_SIZE;
        _fill_buf(ref_buf, chunk, &lfsr_state);

        ssize_t rd = fs_read(&f, file_buf, chunk);
        if (rd != (ssize_t)chunk)
        {
            shell_error(sh, "Diag short read @ %u: %d", offset, (int)rd);
            break;
        }

        if (memcmp(ref_buf, file_buf, chunk) != 0)
        {
            for (uint32_t i = 0; i < chunk; i++)
            {
                if (ref_buf[i] != file_buf[i])
                {
                    shell_error(sh,
                                "First mismatch @ offset %u: expected 0x%02X, got 0x%02X",
                                offset + i,
                                ref_buf[i],
                                file_buf[i]);
                    break;
                }
            }
            break;
        }

        offset += chunk;
    }

    fs_close(&f);
}

static int _cmd_lfs_test(const struct shell *sh, size_t argc, char **argv)
{
    static uint8_t   buf[CHUNK_SIZE];
    static uint8_t   verify_buf[CHUNK_SIZE];
    struct fs_file_t f;
    struct fs_dirent stat       = {0};
    uint32_t         size_kb    = DEFAULT_SIZE_KB;
    uint32_t         test_size  = 0;
    uint32_t         crc_write  = 0;
    uint32_t         crc_read   = 0;
    uint32_t         lfsr_state = LFSR_SEED;
    uint32_t         remaining  = 0;
    int64_t          t_start    = 0;
    int64_t          t_write    = 0;
    int64_t          t_read     = 0;
    int              ret        = 0;

    if (argc >= 2)
    {
        char         *p_end = NULL;
        unsigned long kb    = strtoul(argv[1], &p_end, 0);
        if (p_end == argv[1] || *p_end != '\0' || kb == 0 || kb > MAX_SIZE_KB)
        {
            shell_error(
                sh, "Usage: lfs_test [size_kb]  (1..%u, default %u)", MAX_SIZE_KB, DEFAULT_SIZE_KB);
            return -EINVAL;
        }
        size_kb = (uint32_t)kb;
    }
    test_size = size_kb * 1024;
    remaining = test_size;

    shell_print(
        sh, "LFS test: %u KB write (verify each %u B chunk) + full readback", size_kb, CHUNK_SIZE);

    /* Ensure a clean slate: FS_O_CREATE alone does not truncate an existing
     * file, so stale bytes from a prior interrupted run could corrupt the test.
     * Only call fs_unlink if the file exists to avoid a spurious ENOENT log.
     */
    {
        struct fs_dirent probe = {0};
        if (fs_stat(TEST_PATH, &probe) == 0)
        {
            (void)fs_unlink(TEST_PATH);
        }
    }

    /* ── Write phase ─────────────────────────────────────────────── */
    fs_file_t_init(&f);
    /* RDWR (not just WRITE) is required so we can read each chunk back for
     * verification on the same file handle. LittleFS asserts on reads from
     * a file that was not opened with RDONLY or RDWR.
     */
    ret = fs_open(&f, TEST_PATH, FS_O_CREATE | FS_O_RDWR);
    if (ret)
    {
        shell_error(sh, "Open for write: %d", ret);
        return ret;
    }

    t_start = k_uptime_get();

    while (remaining > 0)
    {
        uint32_t chunk  = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        uint32_t offset = test_size - remaining;
        _fill_buf(buf, chunk, &lfsr_state);
        crc_write = crc32_ieee_update(crc_write, buf, chunk);

        ssize_t wr = fs_write(&f, buf, chunk);
        if (wr < 0)
        {
            shell_error(sh, "Write err: %d @ offset %u", (int)wr, offset);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return (int)wr;
        }
        if ((uint32_t)wr != chunk)
        {
            shell_error(sh, "Short write: got %d, want %u @ offset %u", (int)wr, chunk, offset);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return -EIO;
        }

        /* Flush cache, seek back, and verify the chunk landed correctly
         * before moving on. This catches corruption at the exact chunk
         * it occurs rather than only surfacing at the final read phase.
         */
        ret = fs_sync(&f);
        if (ret)
        {
            shell_error(sh, "Sync @ offset %u: %d", offset, ret);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return ret;
        }

        off_t pos = fs_seek(&f, offset, FS_SEEK_SET);
        if (pos < 0)
        {
            shell_error(sh, "Seek back @ offset %u: %d", offset, (int)pos);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return (int)pos;
        }

        memset(verify_buf, 0, chunk);
        ssize_t rd = fs_read(&f, verify_buf, chunk);
        if (rd < 0)
        {
            shell_error(sh, "Verify read @ offset %u: %d", offset, (int)rd);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return (int)rd;
        }
        if ((uint32_t)rd != chunk)
        {
            shell_error(
                sh, "Verify short read: got %d, want %u @ offset %u", (int)rd, chunk, offset);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return -EIO;
        }

        if (memcmp(buf, verify_buf, chunk) != 0)
        {
            for (uint32_t i = 0; i < chunk; i++)
            {
                if (buf[i] != verify_buf[i])
                {
                    shell_error(sh,
                                "Verify FAIL @ offset %u: wrote 0x%02X, read 0x%02X",
                                offset + i,
                                buf[i],
                                verify_buf[i]);
                    break;
                }
            }
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return -EIO;
        }

        /* Position is now at offset+chunk, which is already the next write
         * position, so no additional seek is needed before the next fs_write.
         */
        remaining -= chunk;
    }

    ret = fs_sync(&f);
    if (ret)
    {
        shell_error(sh, "Sync: %d", ret);
        fs_close(&f);
        fs_unlink(TEST_PATH);
        return ret;
    }

    ret = fs_close(&f);
    if (ret)
    {
        shell_error(sh, "Close after write: %d", ret);
        fs_unlink(TEST_PATH);
        return ret;
    }
    t_write = k_uptime_delta(&t_start);

    /* Verify the file actually landed on disk at the expected size. */
    ret = fs_stat(TEST_PATH, &stat);
    if (ret)
    {
        shell_error(sh, "Stat: %d", ret);
        fs_unlink(TEST_PATH);
        return ret;
    }
    if (stat.size != test_size)
    {
        shell_error(sh, "Size mismatch: on-disk=%zu, expected=%u", stat.size, test_size);
        fs_unlink(TEST_PATH);
        return -EIO;
    }

    shell_print(sh,
                "Write+verify: %u KB in %lld ms (%lld KB/s)",
                size_kb,
                t_write,
                (t_write > 0) ? ((int64_t)test_size / 1024 * 1000 / t_write) : 0);
    shell_print(sh, "Write CRC32: 0x%08X", crc_write);

    /* ── Read phase ──────────────────────────────────────────────── */
    fs_file_t_init(&f);
    ret = fs_open(&f, TEST_PATH, FS_O_READ);
    if (ret)
    {
        shell_error(sh, "Open for read: %d", ret);
        fs_unlink(TEST_PATH);
        return ret;
    }

    t_start   = k_uptime_get();
    remaining = test_size;

    while (remaining > 0)
    {
        uint32_t chunk  = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        uint32_t offset = test_size - remaining;
        ssize_t  rd     = fs_read(&f, buf, chunk);
        if (rd < 0)
        {
            shell_error(sh, "Read err: %d @ offset %u", (int)rd, offset);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return (int)rd;
        }
        if ((uint32_t)rd != chunk)
        {
            shell_error(sh, "Short read: got %d, want %u @ offset %u", (int)rd, chunk, offset);
            fs_close(&f);
            fs_unlink(TEST_PATH);
            return -EIO;
        }
        crc_read = crc32_ieee_update(crc_read, buf, chunk);
        remaining -= chunk;
    }

    fs_close(&f);
    t_read = k_uptime_delta(&t_start);

    shell_print(sh,
                "Read:  %u KB in %lld ms (%lld KB/s)",
                size_kb,
                t_read,
                (t_read > 0) ? ((int64_t)test_size / 1024 * 1000 / t_read) : 0);
    shell_print(sh, "Read  CRC32: 0x%08X", crc_read);

    /* ── Verify ──────────────────────────────────────────────────── */
    if (crc_write == crc_read)
    {
        shell_print(sh, "PASS: CRC32 match");
    }
    else
    {
        shell_error(sh, "FAIL: CRC32 mismatch (write=0x%08X read=0x%08X)", crc_write, crc_read);
        _diagnose_mismatch(sh, test_size);
    }

    (void)fs_unlink(TEST_PATH);

    return (crc_write == crc_read) ? 0 : -EIO;
}

SHELL_CMD_REGISTER(lfs_test, NULL, NULL, _cmd_lfs_test);
