/**
* SPDX-License-Identifier: Apache-2.0
*
* Copyright 2025 Synaptics Incorporated
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef _DHL_H_
#define _DHL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define DHL_LIB_VERSION "0p01"          // DHL libary version string
#define DHL_VERSION     0x26100001      // DHL libary version
#define DHL_HWTAB_VER   0x0001          // DHL HW table version


// version, format: 0x26100001
unsigned int dhl_get_version();

// feature bits,
// [0]: DDR3
// [1]: DDR4
// [2]: LPDDR4
// ...
// ...
// [31]: DEBUG_EN
#define DHL_FEATRUE_DDR3            (1<<0)
#define DHL_FEATRUE_DDR4            (1<<1)
#define DHL_FEATRUE_LPDDR4          (1<<2)

#define DHL_FEATRUE_DEBUG_EN        (1<<31)

unsigned int dhl_get_feature();

int dhl_ddr_set_reg_bases(unsigned int mc_reg_base,
                          unsigned int mcwrap_reg_base,
                          unsigned int dfi_reg_base,
                          unsigned int soc_gbl_reg_base);

#define DIAG_DHL_DDR_TYPE_DDR3      0
#define DIAG_DHL_DDR_TYPE_DDR4      1
#define DIAG_DHL_DDR_TYPE_LPDDR4    2

int dhl_ddr_init(
    /* 1 */ int ddr_type,
    /* 2 */ const void *p_param_table,
    /* 3 */ unsigned int  param_table_size,
    /* 3 */ int is_warm_boot,
    /* 4 */ void *p_saved_ddr_settings,
    /* 5 */ unsigned int saved_ddr_settings_size,
    /* 6 */ unsigned int options);


//// all callbacks
//typedef struct
//{
//    void (*putc_f)(char ch);
//    void (*dbg_printf)(int logLevel, const char* szFormat, ...);
//
//    int (*ddr_init_util_callback_f)(int step_id, const char* info);
//
//} dhl_call_back_t;
//
//int dhl_set_call_back(dhl_call_back_t* pt_callback);

// individual func
int dhl_set_putc_func(void (*putc_f)(char ch));

int dhl_set_dbg_printf_func(void (*dbg_printf)(int logLevel, const char* szFormat, ...));

int dhl_set_ddr_init_callback_func(int (*ddr_init_util_callback_f)(unsigned int id));

// 0: no message, 1: error message, 2: normal message (default), 3: debug message (only available with debug lib)
int dhl_set_msg_level(int msg_level);



//int dhl_change_syspll0(unsigned int freq, unsigned int freq1, unsigned int ssc, unsigned int ssc_freq, unsigned int ssc_amp, unsigned int ssc_mode);
//int dhl_change_syspll1(unsigned int freq, unsigned int freq1, unsigned int ssc, unsigned int ssc_freq, unsigned int ssc_amp, unsigned int ssc_mode);
//int dhl_change_cpupll(unsigned int freq, unsigned int freq1, unsigned int ssc, unsigned int ssc_freq, unsigned int ssc_amp, unsigned int ssc_mode);
int dhl_change_mempll(unsigned int freq, unsigned int freq1, unsigned int ssc, unsigned int ssc_freq, unsigned int ssc_amp, unsigned int ssc_mode);


#define DIAG_DHL_ERROR_PARAM_1        -101
#define DIAG_DHL_ERROR_PARAM_2        -102
#define DIAG_DHL_ERROR_PARAM_3        -103
#define DIAG_DHL_ERROR_ADD_VIOLATE    -111
#define DIAG_DHL_ERROR_TABLE_WRONG    -112

#define DIAG_DHL_ERROR_TIMEOUT        -120

#define DIAG_DHL_ERROR_DDR_INIT_FAIL  -201

#define DIAG_DHL_ERROR_NA             -401


// mode register access
int dhl_ddr_mrw_cmd(unsigned int addr, unsigned int val);

// multi-purpose register access for ddr3/ddr4
int dhl_ddr_mpw_cmd(unsigned int page, unsigned int mpr, unsigned int value);
int dhl_ddr_mpr_dump_cmd(void);

// only available with debug lib
int dhl_debug_enable_cmd(int enable);

#ifdef __cplusplus
}
#endif

#endif //_DHL_H_