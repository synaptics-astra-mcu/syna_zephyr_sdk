/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SOC_H_
#define __SOC_H_

#include <cmsis_core_m_defaults.h>

#define GET_BIT_MASK(N_BIT)     ( (1<<N_BIT) - 1 )
#define GET_BIT(VALUE, BIT_POS, N_BIT)  ( (VALUE & (GET_BIT_MASK(N_BIT) << BIT_POS ) ) >> BIT_POS )
#define SET_BIT(VARIABLE, VALUE, BIT_POS, N_BIT)    ( VARIABLE = (VARIABLE & (~(GET_BIT_MASK(N_BIT) << BIT_POS) ) ) | ( (VALUE & GET_BIT_MASK(N_BIT)) << BIT_POS ) )

#define ARRAY_NUM(a)            (sizeof(a)/sizeof(a[0]))

#define SOCREG_REGION(addr)            ((addr-0xF0000000 + 0xD0000000))

#define GET_VALUE(a)                (*(volatile unsigned int*)((a)))
#define SET_VALUE(a, v)             ((*(volatile unsigned int*)((a))) = (v))
#define SET_VALUE_MASK(a, v, m)     SET_VALUE(a, (GET_VALUE(a)&(~(m))) | ((v)&(m)))
#endif /* __SOC_H */
