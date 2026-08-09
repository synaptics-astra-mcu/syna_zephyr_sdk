/*
 * SPI Test Suite Header
 * Declarations for SPI test functions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPI_TESTS_H
#define SPI_TESTS_H

#include <zephyr/drivers/spi.h>

/**
 * Run the complete SPI test suite
 *
 * @param spi_master SPI master device
 * @param spi_slave SPI slave device
 * @param config_m Master SPI configuration
 * @param config_s Slave SPI configuration
 * @return 0 on success, -1 if any test failed
 */
int spi_test_suite_run(const struct device *spi_master, const struct device *spi_slave,
		       struct spi_config *config_m, struct spi_config *config_s);

#endif /* SPI_TESTS_H */
