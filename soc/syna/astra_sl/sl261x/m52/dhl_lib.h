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

/**
 * @file	dhl_lib.h
 *
 * @brief	DDR Hardware Library (DHL) Caller Interface
 *
 * @details	This header provides the public API for DDR memory initialization
 *		and management using the DHL library. It serves as the main
 *		interface for external callers to initialize DDR memory with
 *		appropriate parameters and configuration.
 */

#ifndef DHL_LIB_H
#define DHL_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------
Include files
*---------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/*----------------------------------------------------------------------------
Definitions and Constants
*---------------------------------------------------------------------------*/

/**
 * @brief DDR Type Definitions
 */
typedef enum {
	DDR_TYPE_DDR3 = 0,	/**< DDR3 memory type */
	DDR_TYPE_DDR4 = 1,	/**< DDR4 memory type */
	DDR_TYPE_LPDDR4 = 2,	/**< LPDDR4 memory type */
	DDR_TYPE_MAX		/**< Maximum DDR type value */
} ddr_type_t;

/**
 * @brief DDR Initialization Return Codes
 */
typedef enum {
	DDR_INIT_SUCCESS = 0,		/**< DDR initialization successful */
	DDR_INIT_ERROR_INVALID_TYPE = -1,	/**< Invalid DDR type specified */
	DDR_INIT_ERROR_UNSUPPORTED = -2,	/**< DDR type not supported */
	DDR_INIT_ERROR_CALLBACK = -3,	/**< Callback setup failed */
	DDR_INIT_ERROR_REG_BASE = -4,	/**< Register base setup failed */
	DDR_INIT_ERROR_DHL_INIT = -5,	/**< DHL initialization failed */
	DDR_INIT_ERROR_PARAM = -6,	/**< Invalid parameter */
	DDR_INIT_ERROR_MEMORY = -7,	/**< Memory allocation error */
	DDR_INIT_ERROR_TIMEOUT = -8,	/**< Initialization timeout */
	DDR_INIT_ERROR_HARDWARE = -9	/**< Hardware error */
} ddr_init_result_t;

/**
 * @brief DDR Configuration Options
 */
typedef struct {
	ddr_type_t type;		/**< DDR memory type */
	bool warm_boot;			/**< Warm boot flag */
	bool enable_debug;		/**< Enable debug messages */
	uint32_t timeout_ms;		/**< Initialization timeout in milliseconds */
} ddr_config_t;

/**
 * @brief DDR Settings Buffer Size
 */
#define DDR_SETTINGS_BUFFER_SIZE	1024

/*----------------------------------------------------------------------------
Utility Macros
*---------------------------------------------------------------------------*/

/**
 * @brief Initialize DDR configuration structure with default values
 *
 * @param config DDR configuration structure to initialize
 * @param ddr_type DDR memory type (DDR_TYPE_DDR3, DDR_TYPE_DDR4, DDR_TYPE_LPDDR4)
 * @param warm_boot_flag Warm boot flag (true/false)
 */
#define DDR_INIT_CONFIG(config, ddr_type, warm_boot_flag) \
	do { \
		(config)->type = (ddr_type); \
		(config)->warm_boot = (warm_boot_flag); \
		(config)->enable_debug = false; \
		(config)->timeout_ms = 5000; \
	} while(0)

/*----------------------------------------------------------------------------
Function Prototypes
*---------------------------------------------------------------------------*/

/**
 * @brief Initialize DDR memory with advanced configuration
 *
 * @details This function provides advanced DDR initialization with custom
 *          configuration options including warm boot support and timeout settings.
 *
 * @param config Pointer to DDR configuration structure
 *
 * @return ddr_init_result_t
 *         - DDR_INIT_SUCCESS: DDR initialization successful
 *         - DDR_INIT_ERROR_PARAM: Invalid configuration parameter
 *         - Other error codes as defined in ddr_init_result_t
 */
ddr_init_result_t sys_ddr_init(const ddr_config_t *config, int real);

void setup_acpu();

#ifdef __cplusplus
}
#endif

#endif /* DHL_LIB_H */
