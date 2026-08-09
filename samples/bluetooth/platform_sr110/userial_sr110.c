/****************************************************************************
**
**  Name:          userial_rt600.c
**
**  Description:   Contains platform userial operation api
**
**
**  Copyright (c) 2019-2020, Broadcom, All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
******************************************************************************/

#include <string.h>
#include "bt_target.h"
#include "gki.h"
#include "userial.h"
#include "hcidefs.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "gki_int.h"
#include "pf_trans.h"

#define READ_THREAD_PRI             (configMAX_PRIORITIES - USERIAL_TASK)
#define READ_THREAD_STACK_SIZE      (1024*2)
#define READ_THREAD_NAME            ((INT8 *) "hci_uart_task")

#define HCI_TYPE_COMMAND            1
#define HCI_TYPE_ACL_DATA           2
#define HCI_TYPE_SCO_DATA           3
#define HCI_TYPE_EVENT              4

#define OS_WAIT_FOREVER             ((unsigned) 0xffffffff)
#define OS_WAIT_ONEMSEC             ((unsigned) 0x00000002)

#define HCI_LOOPBACK_PREAM_LEN      2
#define HCI_EVENT_PREAM_LEN         3
#define HCI_ACL_PREAM_LEN           5

#define USERIAL_TRUE                1
#define USERIAL_FALSE               0

#define USERIAL_ERROR               1
#define USERIAL_SUCCESS             0

typedef bool                        userial_bool_t;
typedef int                         userial_result_t;

/******************************************************
 *                   Enumerations
 ******************************************************/
/* HCI Transport Layer Packet Type */
typedef enum
{
    HCI_UNINITIALIZED = 0x00, // Uninitialized
    HCI_COMMAND_PACKET = 0x01, // HCI Command packet from Host to Controller
    HCI_ACL_DATA_PACKET = 0x02, // Bidirectional Asynchronous Connection-Less Link (ACL) data packet
    HCI_SCO_DATA_PACKET = 0x03, // Bidirectional Synchronous Connection-Oriented (SCO) link data packet
    HCI_EVENT_PACKET = 0x04, // Events
    HCI_LOOPBACK_MODE = 0xFF,
// Loopback mode
// HCI Event packet from Controller to Host
} hci_packet_type_t;

#pragma pack(1)

typedef struct
{
    hci_packet_type_t packet_type; /* This is transport layer packet type. Not transmitted if transport bus is USB */
    uint8_t event_code;
    uint8_t content_length;
} hci_event_header_t;

typedef struct
{
    hci_packet_type_t packet_type; /* This is transport layer packet type. Not transmitted if transport bus is USB */
    uint16_t hci_handle;
    uint16_t content_length;
} hci_acl_packet_header_t;

/* USERIAL control block */
typedef struct
{
    tUSERIAL_CBACK *p_cback;
} tUSERIAL_CB;
tUSERIAL_CB userial_cb;

#pragma pack()

static UINT32 userial_baud_tbl[] = {
    300, /* USERIAL_BAUD_300          0 */
    600, /* USERIAL_BAUD_600          1 */
    1200, /* USERIAL_BAUD_1200         2 */
    2400, /* USERIAL_BAUD_2400         3 */
    9600, /* USERIAL_BAUD_9600         4 */
    19200, /* USERIAL_BAUD_19200        5 */
    57600, /* USERIAL_BAUD_57600        6 */
    115200, /* USERIAL_BAUD_115200       7 */
    230400, /* USERIAL_BAUD_230400       8 */
    460800, /* USERIAL_BAUD_460800       9 */
    921600, /* USERIAL_BAUD_921600       10 */
    1000000, /* USERIAL_BAUD_1M           11 */
    1500000, /* USERIAL_BAUD_1_5M         12 */
    2000000, /* USERIAL_BAUD_2M           13 */
    3000000, /* USERIAL_BAUD_3M           14 */
    4000000 /* USERIAL_BAUD_4M           15 */
};

TaskHandle_t    read_thread_id;

BUFFER_Q        Userial_in_q;
static BT_HDR   *pbuf_USERIAL_Read = NULL;

UINT8 g_readThreadAlive = 0;

static volatile userial_bool_t bus_initialised = USERIAL_FALSE;
static volatile userial_bool_t device_powered  = USERIAL_FALSE;

static void userial_read_thread();

/*******************************************************************************
 **
 ** Function           USERIAL_GetLineSpeed
 **
 ** Description        This function convert USERIAL baud to line speed.
 **
 ** Output Parameter   None
 **
 ** Returns            line speed
 **
 *******************************************************************************/
UINT32 USERIAL_GetLineSpeed( UINT8 baud )
{
    if ( baud <= USERIAL_BAUD_4M )
        return ( userial_baud_tbl[baud - USERIAL_BAUD_300] );
    else
        return 0;
}

/*******************************************************************************
 **
 ** Function           USERIAL_GetBaud
 **
 ** Description        This function convert line speed to USERIAL baud.
 **
 ** Output Parameter   None
 **
 ** Returns            line speed
 **
 *******************************************************************************/
UDRV_API
extern UINT8 USERIAL_GetBaud( UINT32 line_speed )
{
    UINT8 i;
    for ( i = USERIAL_BAUD_300; i <= USERIAL_BAUD_4M; i++ )
    {
        if ( userial_baud_tbl[i - USERIAL_BAUD_300] == line_speed )
            return i;
    }

    return USERIAL_BAUD_AUTO;
}

/*******************************************************************************
 **
 ** Function           USERIAL_Init
 **
 ** Description        This function initializes the  serial driver.
 **
 ** Output Parameter   None
 **
 ** Returns            Nothing
 **
 *******************************************************************************/
UDRV_API
void USERIAL_Init( void *p_cfg )
{
    memset(&userial_cb, 0, sizeof(userial_cb));
    GKI_init_q( &Userial_in_q );

    return;
}

void USERIAL_Open_ReadThread( void )
{
    if(g_readThreadAlive == 0)
    {
        g_readThreadAlive = 1;

        //since freeRTOS create task api will dynamic alloc the stack, just need input the stack size into the api
        GKI_create_task(userial_read_thread, USERIAL_TASK, READ_THREAD_NAME, NULL, READ_THREAD_STACK_SIZE);

        return;
    }
}

void USERIAL_Close_ReadThread( void )
{
    if(g_readThreadAlive == 1)
    {
        // Close our read thread
        g_readThreadAlive = 0;

        // Flush the queue
        do
        {
            pbuf_USERIAL_Read = (BT_HDR *) GKI_dequeue( &Userial_in_q );
            if ( pbuf_USERIAL_Read )
            {
                GKI_freebuf( pbuf_USERIAL_Read );
            }

        } while ( pbuf_USERIAL_Read );
    }
}

/*******************************************************************************
 **
 ** Function           USERIAL_Open
 **
 ** Description        Open the indicated serial port with the given configuration
 **
 ** Output Parameter   None
 **
 ** Returns            Nothing
 **
 *******************************************************************************/
UDRV_API
void USERIAL_Open( tUSERIAL_PORT port, tUSERIAL_OPEN_CFG *p_cfg, tUSERIAL_CBACK *p_cback )
{
    if(port == USERIAL_PORT_1)
    {// USERIAL_PORT_1 use as the hci transport
        pf_trans_init(USERIAL_GetLineSpeed(p_cfg->baud));
        if( p_cback )
        {
            userial_cb.p_cback = p_cback;
            USERIAL_Open_ReadThread();
        }
    }
}

/*******************************************************************************
 **
 ** Function           USERIAL_Read
 **
 ** Description        Read data from a serial port using byte buffers.
 **
 ** Output Parameter   None
 **
 ** Returns            Number of bytes actually read from the serial port and
 **                    copied into p_data.  This may be less than len.
 **
 *******************************************************************************/
UDRV_API UINT16 USERIAL_Read( tUSERIAL_PORT port, UINT8 *p_data, UINT16 len )
{
    UINT16 total_len = 0;
    UINT16 copy_len = 0;
    UINT8 * current_packet = NULL;

    do
    {
        if ( pbuf_USERIAL_Read != NULL )
        {
            current_packet = ( (UINT8 *) ( pbuf_USERIAL_Read + 1 ) ) + ( pbuf_USERIAL_Read->offset );

            if ( ( pbuf_USERIAL_Read->len ) <= ( len - total_len ) )
                copy_len = pbuf_USERIAL_Read->len;
            else
                copy_len = ( len - total_len );

            memcpy( ( p_data + total_len ), current_packet, copy_len );

            total_len += copy_len;

            pbuf_USERIAL_Read->offset += copy_len;
            pbuf_USERIAL_Read->len -= copy_len;

            if ( pbuf_USERIAL_Read->len == 0 )
            {
                GKI_freebuf( pbuf_USERIAL_Read );
                pbuf_USERIAL_Read = NULL;
            }
        }

        if ( pbuf_USERIAL_Read == NULL )
        {
            pbuf_USERIAL_Read = (BT_HDR *) GKI_dequeue( &Userial_in_q );
        }
    } while ( ( pbuf_USERIAL_Read != NULL ) && ( total_len < len ) );

    return total_len;
}

/*******************************************************************************
 **
 ** Function           USERIAL_Readbuf
 **
 ** Description        Read data from a serial port using GKI buffers.
 **
 ** Output Parameter   Pointer to a GKI buffer which contains the data.
 **
 ** Returns            Nothing
 **
 ** Comments           The caller of this function is responsible for freeing the
 **                    GKI buffer when it is finished with the data.  If there is
 **                    no data to be read, the value of the returned pointer is
 **                    NULL.
 **
 *******************************************************************************/
UDRV_API
void USERIAL_ReadBuf( tUSERIAL_PORT port, BT_HDR **p_buf )
{

}

/*******************************************************************************
 **
 ** Function           USERIAL_WriteBuf
 **
 ** Description        Write data to a serial port using a GKI buffer.
 **
 ** Output Parameter   None
 **
 ** Returns            TRUE  if buffer accepted for write.
 **                    FALSE if there is already a buffer being processed.
 **
 ** Comments           The buffer will be freed by the serial driver.  Therefore,
 **                    the application calling this function must not free the
 **                    buffer.
 **
 *******************************************************************************/
UDRV_API
BOOLEAN USERIAL_WriteBuf( tUSERIAL_PORT port, BT_HDR *p_buf )
{
    return FALSE;
}

/*******************************************************************************
 **
 ** Function           USERIAL_Write
 **
 ** Description        Write data to a serial port using a byte buffer.
 **
 ** Output Parameter   None
 **
 ** Returns            Number of bytes actually written to the serial port.  This
 **                    may be less than len.
 **
 *******************************************************************************/
UDRV_API
UINT16 USERIAL_Write( tUSERIAL_PORT port, UINT8 *p_data, UINT16 len )
{
    UINT16 write_len = 0;
    int32_t sent = 0;
    int retries = 100;

    if (port == USERIAL_PORT_1)
    {
        while ((pf_trans_get_status() != PF_TRANS_STATUS_READY) && retries > 0)
        {   //Since the HCI UART driver is not ready yet.
            GKI_delay(5);
            retries--;
        }

        if (retries > 0)
        {
            sent = pf_trans_send(p_data, len);
        }
        // GOOGLE: Make sure we only report written length when no error occured.
        if (sent > 0) {
            write_len = (UINT16)sent;
        }
    }

    return write_len;
}

void USERIAL_ChangeBaud(tUSERIAL_PORT port, UINT32 v)
{
    if(port == USERIAL_PORT_1)
        pf_trans_reconfig_baud(v);
}

/*******************************************************************************
 **
 ** Function           USERIAL_Ioctl
 **
 ** Description        Perform an operation on a serial port.
 **
 ** Output Parameter   The p_data parameter is either an input or output depending
 **                    on the operation.
 **
 ** Returns            Nothing
 **
 *******************************************************************************/
UDRV_API
void USERIAL_Ioctl( tUSERIAL_PORT port, tUSERIAL_OP op, tUSERIAL_IOCTL_DATA *p_data )
{

    switch ( op )
    {
        case USERIAL_OP_FLUSH:
            break;
        case USERIAL_OP_FLUSH_RX:
            break;
        case USERIAL_OP_FLUSH_TX:
            break;
        case USERIAL_OP_BAUD_RD:
            break;
        case USERIAL_OP_BAUD_WR:
                USERIAL_ChangeBaud(port, USERIAL_GetLineSpeed(p_data->baud));
            break;
        default:
            break;
    }

    return;
}

/*******************************************************************************
 **
 ** Function           USERIAL_Close
 **
 ** Description        Close a serial port
 **
 ** Output Parameter   None
 **
 ** Returns            Nothing
 **
 *******************************************************************************/
UDRV_API
void USERIAL_Close( tUSERIAL_PORT port )
{
    DRV_TRACE_DEBUG2("%s port:%d", __FUNCTION__, port);

    if(port == USERIAL_PORT_1)
    {
        pf_trans_deinit();
    }
}

/*******************************************************************************
 **
 ** Function           USERIAL_Feature
 **
 ** Description        Check whether a feature of the serial API is supported.
 **
 ** Output Parameter   None
 **
 ** Returns            TRUE  if the feature is supported
 **                    FALSE if the feature is not supported
 **
 *******************************************************************************/
UDRV_API BOOLEAN USERIAL_Feature( tUSERIAL_FEATURE feature )
{
    return FALSE;
}

/*******************************************************************************
 **
 ** Function           bt_bus_receive
 **
 ** Description        bt_bus_receive
 **
 ** Output Parameter   None
 **
 ** Returns            None
 **
 *******************************************************************************/
userial_result_t bt_bus_receive( uint8_t* data_in, uint32_t size, uint32_t timeout_ms )
{
    uint32_t remains = size;
    int32_t read_count;
    uint32_t offset = 0;
#if BT_USE_TRACES
    int32_t retryCount = 0;
#endif
    do
    {
        read_count = pf_trans_receive(data_in + offset, remains, timeout_ms);

        if(read_count <= 0)
        {
            return USERIAL_ERROR;
        }
        else
        {
            read_count = (read_count > remains) ? remains : read_count;
            remains -= read_count;
            offset += read_count;
#if BT_USE_TRACES
            if(remains && retryCount++ >= 5)
            {

            }
#endif
        }
    }while(remains > 0);

    return USERIAL_SUCCESS;
}

/*******************************************************************************
 **
 ** Function           bt_hci_trans_read_hdl
 **
 ** Description        bt_hci_trans_read_hdl
 **
 ** Output Parameter   None
 **
 ** Returns            None
 **
 *******************************************************************************/
userial_result_t bt_hci_trans_read_hdl(BT_HDR** packet)
{
    BT_HDR* read_pkt;
    hci_packet_type_t packet_type = HCI_UNINITIALIZED;
    UINT16 hci_incoming_size = 0;
    UINT8* hci_content = NULL;
    userial_result_t ret = USERIAL_SUCCESS;

    if (packet == NULL)
    {
        return USERIAL_ERROR;
    }

    // Read 1 byte:
    // packet_type
    //if (bt_bus_receive( (uint8_t*) &packet_type, 1, OS_WAIT_FOREVER) != USERIAL_SUCCESS)
    if (bt_bus_receive( (uint8_t*) &packet_type, 1, OS_WAIT_ONEMSEC) != USERIAL_SUCCESS)
    {
        DRV_TRACE_DEBUG0("bt_bus error reading pkt_type last error ");
        return USERIAL_ERROR;
    }

    switch (packet_type)
    {
        case HCI_LOOPBACK_MODE:
            // Read 1 byte:
            // content_length
            //if ( bt_bus_receive((UINT8*)&hci_incoming_size, 1, OS_WAIT_FOREVER ) != USERIAL_SUCCESS )
            if ( bt_bus_receive((UINT8*)&hci_incoming_size, 1, OS_WAIT_ONEMSEC ) != USERIAL_SUCCESS )
            {
                return USERIAL_ERROR;
            }

            DRV_TRACE_DEBUG1("HCI_LOOPBACK_MODE hci_incoming_size=0x%02x", hci_incoming_size);

            if(hci_incoming_size > 255)
            {
                DRV_TRACE_DEBUG0("Invaild len HCI_LOOPBACK_MODE");
                ret = USERIAL_ERROR;
            }
            else
            {
                read_pkt = (BT_HDR*)GKI_getbuf(hci_incoming_size + sizeof(BT_HDR) + HCI_LOOPBACK_PREAM_LEN);
                if(read_pkt == NULL)
                {
                    DRV_TRACE_DEBUG0("Cant getbuf HCI_LOOPBACK_MODE");
                    ret = USERIAL_ERROR;
                }
                else
                {
                    hci_content = (UINT8*)(read_pkt + 1);
                    *hci_content++ = packet_type;
                    *hci_content++ = (UINT8)hci_incoming_size;

                    // Read payload
                    if (bt_bus_receive(hci_content, hci_incoming_size, OS_WAIT_ONEMSEC ) != USERIAL_SUCCESS)
                    {
                        GKI_freebuf(read_pkt);
                        DRV_TRACE_DEBUG0("Error read HCI_LOOPBACK_MODE");
                        return USERIAL_ERROR;
                    }

                    read_pkt->len = hci_incoming_size + HCI_LOOPBACK_PREAM_LEN;  //packet type + len, 2 bytes
                    read_pkt->offset = 0;
                    read_pkt->layer_specific = 0;
                }
            }
        break;

        case HCI_EVENT_PACKET:
        {
            hci_event_header_t header;
            // Read 2 bytes:
            // event_code
            // content_length
            if (bt_bus_receive((uint8_t*) &header.event_code, sizeof(header) - sizeof(hci_packet_type_t), OS_WAIT_ONEMSEC ) != USERIAL_SUCCESS)
            {
                return USERIAL_ERROR;
            }

            hci_incoming_size = header.content_length;

            if(hci_incoming_size > 255)
            {
                DRV_TRACE_DEBUG0("Invaild len HCI_EVENT_PACKET");
                ret = USERIAL_ERROR;
            }
            else
            {
                read_pkt = (BT_HDR*)GKI_getbuf(hci_incoming_size + sizeof(BT_HDR) + HCI_EVENT_PREAM_LEN);
                if(read_pkt == NULL)
                {
                    DRV_TRACE_DEBUG0("Cant getbuf HCI_EVENT_PACKET");
                    ret = USERIAL_ERROR;
                }
                else
                {
                    hci_content = (UINT8*)(read_pkt + 1);
                    *hci_content++ = packet_type;
                    *hci_content++ = header.event_code;
                    *hci_content++ = header.content_length;

                    // Read payload
                    if (bt_bus_receive(hci_content, hci_incoming_size, OS_WAIT_ONEMSEC ) != USERIAL_SUCCESS)
                    {
                        GKI_freebuf(read_pkt);
                        DRV_TRACE_DEBUG0("Error read HCI_EVENT_PACKET");
                        return USERIAL_ERROR;
                    }

                    read_pkt->len = hci_incoming_size + HCI_EVENT_PREAM_LEN;  //packet type + event_code + len, 3 bytes
                    read_pkt->offset = 0;
                    read_pkt->layer_specific = 0;
                }
            }

            break;
        }

        case HCI_ACL_DATA_PACKET:
        {
            hci_acl_packet_header_t header;

            // Read 4 bytes:
            // hci_handle (2 bytes)
            // content_length (2 bytes)
            if (bt_bus_receive((uint8_t*)&header.hci_handle, HCI_DATA_PREAMBLE_SIZE, OS_WAIT_ONEMSEC) != USERIAL_SUCCESS )
            {
                DRV_TRACE_ERROR0("bt_bus error acl header");
                return USERIAL_ERROR;
            }

            hci_incoming_size = header.content_length;

            if(hci_incoming_size > (HCI_ACL_POOL_BUF_SIZE - BT_HDR_SIZE))
            {
                DRV_TRACE_DEBUG0("Invaild len HCI_ACL_DATA_PACKET");
                ret = USERIAL_ERROR;
            }
            else
            {
                read_pkt = (BT_HDR*)GKI_getbuf(hci_incoming_size + sizeof(BT_HDR) + HCI_ACL_PREAM_LEN);
                if(read_pkt == NULL)
                {
                    DRV_TRACE_DEBUG0("Cant getbuf HCI_ACL_DATA_PACKET");
                    ret = USERIAL_ERROR;
                }
                else
                {
                    hci_content = (UINT8*)(read_pkt + 1);
                    UINT8_TO_STREAM(hci_content,  packet_type);
                    UINT16_TO_STREAM(hci_content, header.hci_handle);
                    UINT16_TO_STREAM(hci_content, header.content_length);

                    // Read payload
                    if (bt_bus_receive(hci_content, hci_incoming_size, OS_WAIT_ONEMSEC ) != USERIAL_SUCCESS)
                    {
                        GKI_freebuf(read_pkt);
                        DRV_TRACE_DEBUG0("Error read HCI_ACL_DATA_PACKET");
                        return USERIAL_ERROR;
                    }

                    read_pkt->len = hci_incoming_size + HCI_ACL_PREAM_LEN;  //packet type + event_code + len, 5 bytes
                    read_pkt->offset = 0;
                    read_pkt->layer_specific = 0;
                }
            }

            break;
        }

        case HCI_COMMAND_PACKET: /* Fall-through */
        default:
            ret = USERIAL_ERROR;
    }

    if(ret == USERIAL_SUCCESS)
    {
        *packet = read_pkt;
    }

    return ret;
}

int userial_count = 0;
/*******************************************************************************
 **
 ** Function           userial_read_thread
 **
 ** Description        userial_read_thread
 **
 ** Output Parameter   None
 **
 ** Returns            None
 **
 *******************************************************************************/
void userial_read_thread( void * p )
{
    BT_HDR *p_buf;
    userial_result_t ret;

    DRV_TRACE_DEBUG0("userial_read_thread up");

    while ( g_readThreadAlive )
    {
        //DRV_TRACE_DEBUG0("userial_read_thread wait data...\n");
        // FIXME:TODO: Decide on the correct buffer size
        if(pf_trans_get_status() == PF_TRANS_STATUS_READY)
        {
            ret = bt_hci_trans_read_hdl(&p_buf);
            if ( ret == USERIAL_ERROR )
            {
                //debug_printf("userial_read_thread:%d\r\n", userial_count++);
                GKI_delay(1);
                continue;
            }

            GKI_enqueue( &Userial_in_q, p_buf );

            if (userial_cb.p_cback)
            {
                (userial_cb.p_cback)(0, USERIAL_RX_READY_EVT, NULL);
            }
        }
        else
        {
            debug_printf("userial_read_thread(NotReady):%d\r\n", userial_count++);
            GKI_delay(20);
        }
    }

    DRV_TRACE_DEBUG0("Shutdown userial_read_thread");
    GKI_exit_task(USERIAL_TASK);
}

void userial_read_thread_shutdown(void)
{
    USERIAL_Close_ReadThread();
    pf_trans_deinit();
}

#if BT_USE_TRACES == TRUE

void PrintHex(const uint8_t* buf, uint32_t bufSize)
{
    int i = 0;
    for(i = 0; i < bufSize; i++)
    {
        if(i != 0 && i % 16 == 0)
        {

        }
        else
        {

        }
    }
}

#endif
