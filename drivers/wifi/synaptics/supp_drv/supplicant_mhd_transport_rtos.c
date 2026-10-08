/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Synaptics Incorporated
 */

#ifndef ZEPHYR_OS

#include "includes.h"
#include "supp_main.h"
#include "sup_adap_msg.h"
#include "supplicant_mhd_event.h"
#include "supplicant_mhd_transport.h"

static osEventFlagsId_t sup_adap_evt_id;
static osMessageQueueId_t sup_adap_queue;
static osThreadId_t sup_adap_thread_id = NULL;

static void init_sup_adap_queue(void)
{
    sup_adap_queue = osMessageQueueNew(MQUEUE_OBJS, sizeof(sup_adap_msg_t), NULL);
    if (sup_adap_queue == NULL) {
        wpa_printf(MSG_ERROR, "Adaptor : Failed to init mhd msg queue!");
    } else {
        wpa_printf(MSG_DEBUG, "Adaptor : queue init success(%d).", MQUEUE_OBJS);
    }
}

static void init_sup_adap_event(void)
{
    sup_adap_evt_id = osEventFlagsNew(NULL);
    if (sup_adap_evt_id == NULL)
        wpa_printf(MSG_ERROR, "Adaptor: Failed to init Event grp!");
    else
        wpa_printf(MSG_DEBUG, "Adaptor: event created success.");
}

int send_msg_to_sup_adap(sup_adap_msg_t *msg)
{
    osStatus_t status;

    if (sup_adap_queue) {
        status = osMessageQueuePut(sup_adap_queue, msg, 0, 0);
        if (status == osOK) {
            osEventFlagsSet(sup_adap_evt_id, SUP_ADAP_SUPP_MASK);
            return 0;
        }

        wpa_printf(MSG_ERROR, "send_msg_to_sup_adap : Failed to send the msg!");
        return -1;
    }

    wpa_printf(MSG_ERROR, "send_msg_to_sup_adap : supp_queue_rx is not initied!!!!");
    return -1;
}

int sup_adap_register_ap_event_handler(void)
{
    size_t event_count = 0;
    const uint32_t *event_list = sup_adap_transport_ap_events(&event_count);
    mhd_wifi_event_handler_params_t eh_ap = {
        .event_list = event_list,
        .event_count = event_count,
        .event_cb = mhd_wifi_common_events_handler,
        .user_ctx = sup_adap_transport_state(),
        .interface = MHD_AP_INTERFACE,
    };

    if (mhd_wifi_api_set_event_handler(&eh_ap) != 0)
        return -1;

    return 0;
}

int sup_adap_send_supp_msg(void *msg)
{
    osStatus_t status;

    if (supp_queue) {
        status = osMessageQueuePut(supp_queue, msg, 0, 0);
        if (status == osOK) {
            osEventFlagsSet(supp_evt_id, SUPP_SUP_ADAP_MASK);
            return 0;
        }

        wpa_printf(MSG_ERROR, "%s : Failed to send the msg!", __func__);
        return -1;
    }

    wpa_printf(MSG_ERROR, "%s : supp_queue_rx is not initied!!!!", __func__);
    return -1;
}

void *sup_adap_send_and_recv(sup_adap_msg_t *req,
                             drv_syna_message_type_t ret_msg_type)
{
    u32 flag;
    osStatus_t status;
    drv_syna_msg_t resp;

    if (!sup_adap_queue) {
        wpa_printf(MSG_ERROR, "Supplicant : mhd_queue is not initied!");
        return NULL;
    }

    status = osMessageQueuePut(sup_adap_queue, req, 0, 0);
    if (status != osOK) {
        wpa_printf(MSG_ERROR, "Supplicant : Failed to send and recv msg!");
        return NULL;
    }

    osEventFlagsSet(sup_adap_evt_id, SUP_ADAP_SUPP_MASK);

    while (1) {
        flag = osEventFlagsWait(supp_evt_id, SUPP_SUP_ADAP_MASK,
                                osFlagsWaitAny, osWaitForever);
        if ((int)flag < 0) {
            wpa_printf(MSG_ERROR, "%s: Errorcode flag=0x%08lX",
                       __func__, (unsigned long)flag);
            return NULL;
        }

        status = osMessageQueueGet(supp_queue, &resp, NULL, 0);
        if (status != osOK) {
            wpa_printf(MSG_ERROR, "Supplicant : Failed to get msg from queue");
            return NULL;
        }

        if (ret_msg_type == resp.type) {
            wpa_printf(MSG_DEBUG, "MHD Message received : %d", resp.type);
            return resp.data;
        }
    }
}

static void sup_adap_get_data(uint32_t flag)
{
    sup_adap_msg_t msg;
    osStatus_t status;

    if ((flag & SUP_ADAP_SUPP_MASK) != SUP_ADAP_SUPP_MASK)
        return;

    while (1) {
        status = osMessageQueueGet(sup_adap_queue, &msg, NULL, osWaitForever);
        if (status == osOK) {
            sup_adap_process_msg(&msg);
        } else {
            wpa_printf(MSG_ERROR,
                       "Adaptor: Failed to receive cmd in supplicant_mhd task.");
        }

        if (osMessageQueueGetCount(sup_adap_queue) == 0)
            return;
    }
}

static void sup_adap_task(void)
{
    uint32_t flag;

    wpa_printf(MSG_INFO, "sup_adap_task : task started!");

    while (1) {
        flag = osEventFlagsWait(sup_adap_evt_id, SUP_ADAP_SUPP_MASK,
                                osFlagsWaitAny, osWaitForever);
        if (flag)
            sup_adap_get_data(flag);
    }
}

void init_sup_adap_task(void)
{
    init_sup_adap_queue();
    init_sup_adap_event();

    dl_list_init(&drv_scan_results_list);

    const osThreadAttr_t thread2_attr = {
        .name = "Supp_Adap_task",
        .priority = osPriorityNormal,
        .stack_size = STACK_SIZE * 3,
    };

    sup_adap_thread_id = osThreadNew((osThreadFunc_t)sup_adap_task, NULL,
                                     &thread2_attr);
    if (sup_adap_thread_id == NULL)
        wpa_printf(MSG_ERROR, "init_sup_adap_task Thread Creation failed");

    wpa_printf(MSG_DEBUG, "Adaptor: initialized (scan-only)");
}

void deinit_sup_adap_task(void)
{
    if (sup_adap_thread_id) {
        osThreadTerminate(sup_adap_thread_id);
        sup_adap_thread_id = NULL;
    }

    if (sup_adap_evt_id) {
        osEventFlagsDelete(sup_adap_evt_id);
        sup_adap_evt_id = NULL;
    }

    if (sup_adap_queue) {
        osMessageQueueDelete(sup_adap_queue);
        sup_adap_queue = NULL;
    }
}

#endif
