/*****************************************************************************
**
**  Name:             btapp_mfg.h
**
**  Description:     This file contains btapp mfg internal interface
**                     definition
**
**  Copyright (c) 2019-2020, Broadcom, All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
******************************************************************************/

#ifndef BTAPP_MFG_H
#define BTAPP_MFG_H

#include "stdint.h"

int8_t bt_mfg_init( int32_t argc, const char *argv[] );
int8_t bt_mfg_deinit( int32_t argc, const char *argv[] );
int8_t bt_mfg_hci_send_any( int32_t argc, const char *argv[] );
int8_t bt_mfg_send_hci( uint8_t *cmd, uint8_t len, uint8_t *res, uint8_t res_len );
int8_t bt_hci_reset( int32_t argc, const char *argv[] );
int8_t bt_le_tx_test( int32_t argc, const char* argv[] );
int8_t bt_le_test_end( int32_t argc, const char* argv[] );
int8_t bt_radio_tx_test( int32_t argc, const char* argv[] );
int8_t bt_le_enhanced_tx_test( int32_t argc, const char* argv[] );
int8_t bt_le_enhanced_rx_test( int32_t argc, const char* argv[] );
int8_t bt_radio_rx_test( int32_t argc, const char* argv[] );

#endif
