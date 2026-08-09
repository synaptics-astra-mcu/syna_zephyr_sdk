/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB CDC stack service API.
 *
 * @file usb_cdc_stack_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNA_USB_CDC_STACK_SERVICE_H_
/* Include guard for stack-owned USB CDC service API. */
#define SYNA_USB_CDC_STACK_SERVICE_H_

/**
 * \brief   Initialize stack-owned USB CDC ACM service for the active core.
 *
 * \details Configures USB device context, CDC ACM UART endpoints, ring buffers,
 *          and UART interrupt handlers. On non-USB targets this API returns
 *          -ENOTSUP.
 *
 * \return  0 on success, negative errno on failure.
 */
int usb_cdc_stack_service_init(void);

#endif /* SYNA_USB_CDC_STACK_SERVICE_H_ */
