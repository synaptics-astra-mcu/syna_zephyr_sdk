/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SYNA_MAILBOX_
#define _SYNA_MAILBOX_

#include <stddef.h>
#include <stdint.h>

typedef enum {
	HOST2SYNA_SENDER_M52	= 0x1,
	HOST2SYNA_SENDER_A55	= 0x2,
	HOST2SYNA_SENDER_WLAN	= 0x5,
	HOST2SYNA_SENDER_BT		= 0x6,
}HOST2SYNA_SENDER_ID;

typedef enum {
	HOST2SYNA_CMD_SOC			= 0x0,
	HOST2SYNA_CMD_SM			= 0x1,
	HOST2SYNA_CMD_ACPU			= 0x2,
	HOST2SYNA_CMD_WIRELESS		= 0x3,
}HOST2SYNA_CMD_CATGORY;


typedef enum {
	SPI_PI_QUERY_SPK_VERSION	    = 0x0,
	SPI_PI_VERIFY_SIGNATURE 	    = 0x1,
	SPI_PI_DERIVE_KEY	            = 0x2,
	SPI_PI_QUERY_BOOT_CONTEXT       = 0x3,
	SPI_PI_UPDATE_M52_ENTRY_POINT   = 0x4,
}HOST2SYNA_SOC_CMD_ID;

typedef enum {
	SP_PI_QUERY_BOOT_CONTEXT	= 0x0,
}HOST2SYNA_SM_CMD_ID;

typedef enum {
	SP_PI_OTP_READ_WIRELESS		= 0x0,
	SP_PI_OTP_WRITE_WIRELESS	= 0x1,
}HOST2SYNA_WIRELESS_CMD_ID;


/*
 * Mailbox Command Format
 *	  [31:24]:	SENDER_ID
 *	  [23:16]:	NS 1:REE, 0: TEE
 *	  [15:8]:	CMD_CATGORY
 *	  [7:0]:	Command ID
 */

#define MAILBOX_COMMAND(sender, ns, catgory, command_id)  \
					(((sender & 0xff) << 24) | \
					((ns & 0xff) << 16) | \
					((catgory & 0xff) << 8) | \
					(command_id & 0xff))

#define MAILBOX_COMMAND_SENDER(cmd) 	(((cmd) >> 24) & 0xff)
#define MAILBOX_COMMAND_NS(cmd)		 	(((cmd) >> 16) & 0xff)
#define MAILBOX_COMMAND_CATGORY(cmd)	(((cmd) >> 8) & 0xff)
#define MAILBOX_COMMAND_ID(cmd)			(((cmd) & 0xff)

#define MAX_MAILBOX_DATA_LEN 	8
#define MAX_MAILBOX_PARAM_NUM 	(MAX_MAILBOX_DATA_LEN - 1)

struct mailbox_command {
	unsigned int command;
	unsigned int param[MAX_MAILBOX_PARAM_NUM];
	int param_num;
};

#define MAILBOX_NUM      2

#define MAILBOX_REE      1      // both secure and non-secure can access
#define MAILBOX_TEE      0      // only secure can access

#define  SHA_CMD_ALGORITHM_SHA256                             0x3
#define  SHA_CMD_ALGORITHM_SHA512                             0x5

typedef enum _image_type {
    /* ECC keys */
    IMAGE_TYPE_K0_SYNA                  = 0x00000001,
    IMAGE_TYPE_K0_OEM                   = 0x00000002,
    IMAGE_TYPE_K1_3RD                   = 0x00000003,
    IMAGE_TYPE_K1_A                     = 0x00000004,
    IMAGE_TYPE_K1_B                     = 0x00000005,
    IMAGE_TYPE_K1_C                     = 0x00000006,
    IMAGE_TYPE_K1_D                     = 0x00000007,
    IMAGE_TYPE_K1_SYNA_ADAC             = 0x00000008,
    IMAGE_TYPE_K1_OEM_ADAC              = 0x00000009,

    /* SW images */
    IMAGE_TYPE_SPK                      = 0x00000010,
    IMAGE_TYPE_BL                       = 0x00000011,
    IMAGE_TYPE_SM                       = 0x00000012,
    IMAGE_TYPE_ATF                      = 0x00020013,
    IMAGE_TYPE_OPTEE_CONTAINER_HEADER   = 0x00020014,
    IMAGE_TYPE_OPTEE                    = 0x00020015,
    IMAGE_TYPE_TEE_BOOT_PARAMETER       = 0x00020016,
    IMAGE_TYPE_UBOOT                    = 0x00020017,
    IMAGE_TYPE_LINUX                    = 0x00020018,
    IMAGE_TYPE_MODEL                    = 0x00020019,
    IMAGE_TYPE_NPU                      = 0x0002001A,

} image_type_t;

/**
 * @brief   Mailbox H2S Get Response
 *
 * @param[in]   mailbox_id    - Mailbox ID
 * @param[in]   data          - Data Pointer to hold Response
 * @param[in]   timeout       - Timeout value
 * 
 * @return
 */
int mailbox_h2s_get_response(int mailbox_id, unsigned int data[], int timeout);

/**
 * @brief   Mailbox H2S Wait Done
 *
 * @param[in]   mailbox_id    - Mailbox ID
 * @param[in]   timeout       - Timeout value
 * 
 * @return
 */
int mailbox_h2s_tx_wait_done(int mailbox_id, int timeout);

/**
 * @brief   Mailbox H2S Get Base Address
 *
 * @param[in]   mailbox_id    - Mailbox ID
 * 
 * @return
 */
int mailbox_h2s_get_base_address(int mailbox_id);

/**
 * @brief   Mailbox H2S Get Response
 *
 * @param[in]   mailbox_id    - Mailbox ID
 * @param[in]   dwords        - Data Pointer to send command
 * @param[in]   ncount        - Timeout value
 * 
 * @return
 */
int mailbox_h2s_raw_msg_send(int mailbox_id, unsigned int dwords[], int ncount);

/**
 * @brief   Mailbox H2S Get SPK Version
 *
 * @param[in]   mode : 0: polling mode, 1: interrupt mode
 * 
 * @return
 */
int mailbox_host_set_tx_mode(int mode);

/**
 * @brief   Mailbox H2S Tx Init
 *
 * @param[in]	mailbox_id - Mailbox Id
 * 
 * @return
 */
int mailbox_host_tx_init(int mailbox_id);

/**
 * @brief   Mailbox Init
 *
 * @param[in]   
 * 
 * @return
 */
int mailbox_init(void);

/**
 * @brief   Poll for a response from the secure co-processor via the mailbox.
 *
 * @param[in]   dev         - Device structure for the mailbox
 * @param[out]  data        - Buffer to store the response data
 * @param[out]  nwords      - Number of words received
 * @param[in]   timeout     - Timeout for polling
 *
 * @return  0 on success, negative error code on failure
 */
int mbox_synaprot_poll_response(const struct device *dev, uint32_t *data,
				     uint32_t *nwords, k_timeout_t timeout);

#endif // _SYNA_MAILBOX_