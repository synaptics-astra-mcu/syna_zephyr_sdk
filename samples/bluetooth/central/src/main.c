/* main.c - Central with device selection
 * Mode A (CONFIG_BT_TARGET_NAME): auto-connect by name
 * Mode B (CONFIG_BT_TARGET_ADDR): auto-connect by MAC
 * Mode C (default): scan 5s, show list, pick by number
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

#ifdef CONFIG_ARCH_POSIX
#include <fcntl.h>
#include <unistd.h>
#else
#include <zephyr/console/console.h>
#endif

#define MAX_DEVICES  16
#define SCAN_SECONDS  5

struct scanned_dev {
bt_addr_le_t addr;
char         name[32];
int8_t       rssi;
};

static struct scanned_dev devs[MAX_DEVICES];
static int    dev_cnt;
static struct bt_conn *default_conn;

static void ad_get_name(struct net_buf_simple *ad, char *out, size_t len)
{
while (ad->len > 1) {
uint8_t elen = net_buf_simple_pull_u8(ad);
uint8_t type;

if (elen == 0 || elen > ad->len) {
break;
}
type = net_buf_simple_pull_u8(ad);
elen--;
if (type == BT_DATA_NAME_COMPLETE ||
    type == BT_DATA_NAME_SHORTENED) {
size_t n = MIN(elen, len - 1);

memcpy(out, ad->data, n);
out[n] = '\0';
return;
}
net_buf_simple_pull(ad, elen);
}
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
  uint8_t type, struct net_buf_simple *ad)
{
char name[32] = "";
char addr_str[BT_ADDR_LE_STR_LEN];
struct net_buf_simple_state st;

if (default_conn) {
return;
}
if (type != BT_GAP_ADV_TYPE_ADV_IND &&
    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
return;
}

net_buf_simple_save(ad, &st);
ad_get_name(ad, name, sizeof(name));
net_buf_simple_restore(ad, &st);
bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

#if defined(CONFIG_BT_CENTRAL_BY_NAME)
if (strcmp(name, CONFIG_BT_TARGET_NAME) != 0) {
return;
}
printk("Found: %s  %s  RSSI=%d -> connecting\n",
       name, addr_str, rssi);
bt_le_scan_stop();
bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
  BT_LE_CONN_PARAM_DEFAULT, &default_conn);

#elif defined(CONFIG_BT_CENTRAL_BY_ADDR)
if (strcmp(addr_str, CONFIG_BT_TARGET_ADDR) != 0) {
return;
}
printk("Found: %s  RSSI=%d -> connecting\n", addr_str, rssi);
bt_le_scan_stop();
bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
  BT_LE_CONN_PARAM_DEFAULT, &default_conn);

#else
/* Mode C: silent collect */
for (int i = 0; i < dev_cnt; i++) {
if (bt_addr_le_eq(&devs[i].addr, addr)) {
devs[i].rssi = rssi;
return;
}
}
if (dev_cnt >= MAX_DEVICES) {
return;
}
bt_addr_le_copy(&devs[dev_cnt].addr, addr);
devs[dev_cnt].rssi = rssi;
strncpy(devs[dev_cnt].name, name, sizeof(devs[0].name) - 1);
dev_cnt++;
#endif
}

static void connected(struct bt_conn *conn, uint8_t err)
{
char addr[BT_ADDR_LE_STR_LEN];

bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
if (err) {
printk("Connect FAILED: %s (err 0x%02x)\n", addr, err);
bt_conn_unref(default_conn);
default_conn = NULL;
return;
}
printk("=== Connected: %s ===\n", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
char addr[BT_ADDR_LE_STR_LEN];

if (conn != default_conn) {
return;
}
bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
printk("=== Disconnected: %s (0x%02x) ===\n", addr, reason);
bt_conn_unref(default_conn);
default_conn = NULL;
}

BT_CONN_CB_DEFINE(conn_cb) = {
.connected    = connected,
.disconnected = disconnected,
};

/* Mode C: interactive selection thread */
#if !defined(CONFIG_BT_CENTRAL_BY_NAME) && !defined(CONFIG_BT_CENTRAL_BY_ADDR)

static K_THREAD_STACK_DEFINE(sel_stk, 4096);
static struct k_thread sel_thr;

static char *wait_input(void)
{
#ifdef CONFIG_ARCH_POSIX
static char buf[16];
ssize_t n;

while (true) {
n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
if (n > 0) {
buf[n] = '\0';
buf[strcspn(buf, "\r\n")] = '\0';
return buf;
}
k_sleep(K_MSEC(200));
}
#else
return console_getline();
#endif
}

static void do_rescan(void)
{
dev_cnt = 0;
memset(devs, 0, sizeof(devs));
bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
k_sleep(K_SECONDS(SCAN_SECONDS));
}

static void sel_thread_fn(void *a, void *b, void *c)
{
ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

printk("Scanning %ds...\n", SCAN_SECONDS);
k_sleep(K_SECONDS(SCAN_SECONDS));

while (true) {
char addr_str[BT_ADDR_LE_STR_LEN];
char *line;
int choice, i;

if (default_conn) {
k_sleep(K_SECONDS(1));
continue;
}

bt_le_scan_stop();
k_sleep(K_MSEC(100));

if (dev_cnt == 0) {
printk("No devices found, rescanning...\n");
do_rescan();
continue;
}

printk("\n--- %d device(s) found ---\n", dev_cnt);
for (i = 0; i < dev_cnt; i++) {
bt_addr_le_to_str(&devs[i].addr,
  addr_str, sizeof(addr_str));
printk("[%2d] %-24s  %s  RSSI=%d\n",
       i,
       devs[i].name[0] ? devs[i].name : "(no name)",
       addr_str,
       devs[i].rssi);
}
printk("Enter number / r=rescan / q=quit: ");

line = wait_input();
if (!line) {
continue;
}

if (line[0] == 'r' || line[0] == 'R') {
do_rescan();
continue;
}
if (line[0] == 'q' || line[0] == 'Q') {
printk("Quit\n");
return;
}

choice = atoi(line);
if (choice < 0 || choice >= dev_cnt) {
printk("Invalid, enter 0~%d\n", dev_cnt - 1);
bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
k_sleep(K_MSEC(500));
continue;
}

bt_addr_le_to_str(&devs[choice].addr,
  addr_str, sizeof(addr_str));
printk("Connecting [%d] %s %s ...\n",
       choice,
       devs[choice].name[0] ? devs[choice].name : "(no name)",
       addr_str);

if (bt_conn_le_create(&devs[choice].addr,
      BT_CONN_LE_CREATE_CONN,
      BT_LE_CONN_PARAM_DEFAULT,
      &default_conn) != 0) {
printk("Create conn failed, rescanning\n");
do_rescan();
}
k_sleep(K_SECONDS(5));
}
}
#endif /* mode C */

int main(void)
{
int err;

err = bt_enable(NULL);
if (err) {
printk("bt_enable failed (%d)\n", err);
return 0;
}
printk("Bluetooth initialized\n");

#if defined(CONFIG_BT_CENTRAL_BY_NAME)
printk("Mode A: searching for \"%s\"\n", CONFIG_BT_TARGET_NAME);
err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
if (err) {
printk("scan_start failed (%d)\n", err);
}

#elif defined(CONFIG_BT_CENTRAL_BY_ADDR)
printk("Mode B: searching for %s\n", CONFIG_BT_TARGET_ADDR);
err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
if (err) {
printk("scan_start failed (%d)\n", err);
}

#else
#ifdef CONFIG_ARCH_POSIX
fcntl(STDIN_FILENO, F_SETFL,
      fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
#else
console_getline_init();
#endif
err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
if (err) {
printk("scan_start failed (%d)\n", err);
return 0;
}
k_thread_create(&sel_thr, sel_stk,
K_THREAD_STACK_SIZEOF(sel_stk),
sel_thread_fn, NULL, NULL, NULL,
K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
k_thread_name_set(&sel_thr, "bt_sel");
printk("Mode C: interactive\n");
#endif

return 0;
}
