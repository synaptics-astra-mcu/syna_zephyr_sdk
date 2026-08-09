/*
 * Copyright (c) 2026 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SDHC_MSHC_H
#define ZEPHYR_DRIVERS_SDHC_MSHC_H

#include <stdint.h>
#include <zephyr/device.h>

/* Vendor quirks per driver instance */
struct dwc_mshc_vendor_quirks {
	/* Called at the beginning of dwc_mshc_init() */
	int (*pre_enable)(const struct device *dev);
	int (*post_enable)(const struct device *dev);
};

#include "sdhc_mshc_vendor_quirks.h"

#define SDHC_MSHC_VENDOR_QUIRK_GET(n)						\
	COND_CODE_1(DT_NODE_VENDOR_HAS_IDX(DT_DRV_INST(n), 1),			\
		    (&dwc_mshc_vendor_quirks_##n),				\
		    (NULL))

#define SDHC_MSHC_QUIRK_FUNC_DEFINE(fname)					\
static inline int dwc_mshc_quirk_##fname(const struct device *dev)		\
{										\
	const struct dwc_mshc_vendor_quirks *const quirks =			\
		COND_CODE_1(IS_EQ(DT_NUM_INST_STATUS_OKAY(snps_mshc), 1),	\
			(SDHC_MSHC_VENDOR_QUIRK_GET(0);), (config->quirks;))	\
										\
	if (quirks != NULL && quirks->fname != NULL) {				\
		return quirks->fname(dev);					\
	}									\
										\
	return 0;								\
}

SDHC_MSHC_QUIRK_FUNC_DEFINE(pre_enable)
SDHC_MSHC_QUIRK_FUNC_DEFINE(post_enable)

#endif /* ZEPHYR_DRIVERS_SDHC_MSHC_H */
