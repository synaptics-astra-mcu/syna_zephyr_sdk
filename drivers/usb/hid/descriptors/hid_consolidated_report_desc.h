/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief Consolidated USB HID report descriptor definitions.
 *
 * @file hid_consolidated_report_desc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HID_CONSOLIDATED_REPORT_DESC_H_
/* Include guard for consolidated HID descriptor definitions. */
#define HID_CONSOLIDATED_REPORT_DESC_H_

#include "hid_hpd_v2_report_desc.h"

/* Composite descriptor macro for custom, sensor, digitizer, and HPD collections. */
#define HID_CONSOLIDATED_MULTI_PROFILE_REPORT_DESC \
	/* ========================================================== */ \
	/* Collection 1: Vendor - Custom (0x40) + CUSTOMHPD (0x03)   */ \
	/* ========================================================== */ \
	0x06, 0x00, 0xFF,        /* Usage Page (Vendor 0xFF00) */ \
	0x09, 0x01,              /* Usage (Vendor Device) */ \
	0xA1, 0x01,              /* Collection (Application) */ \
	/* Report ID 0x40 - Custom: 63-byte IN + 63-byte OUT */ \
	0x85, 0x40,              /* Report ID */ \
	0x09, 0x40,              /* Usage (Custom IN) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x26, 0xFF, 0x00,        /* Logical Maximum (255) */ \
	0x75, 0x08,              /* Report Size (8) */ \
	0x95, 0x3F,              /* Report Count (63) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0x09, 0x41,              /* Usage (Custom OUT) */ \
	0x91, 0x02,              /* Output (Data, Variable, Absolute) */ \
	/* Report ID 0x03 - CUSTOMHPD: 6-byte vendor presence */ \
	0x85, 0x03,              /* Report ID */ \
	0x09, 0x43,              /* Usage (CUSTOMHPD) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x26, 0xFF, 0x00,        /* Logical Maximum (255) */ \
	0x75, 0x08,              /* Report Size (8) */ \
	0x95, 0x06,              /* Report Count (6) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0xC0,                    /* End Collection (Vendor) */ \
	\
	/* ========================================================== */ \
	/* Collection 2: ALS Sensor -> Windows sensor driver          */ \
	/* Usage Page 0x20 (Sensors) / Usage 0x41 (Ambient Light)    */ \
	/* ========================================================== */ \
	0x05, 0x20,              /* Usage Page (Sensors) */ \
	0x09, 0x41,              /* Usage (Light: Ambient Light) */ \
	0xA1, 0x01,              /* Collection (Application) */ \
	0x85, 0x41,              /* Report ID */ \
	0x0A, 0xD1, 0x04,        /* Usage (Sensor Data: Light Illuminance 0x04D1) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x26, 0xFF, 0x7F,        /* Logical Maximum (32767) */ \
	0x75, 0x10,              /* Report Size (16) */ \
	0x95, 0x01,              /* Report Count (1) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0xC0,                    /* End Collection (ALS) */ \
	\
	/* ========================================================== */ \
	/* Collection 3: Pen Digitizer -> Windows pen/stylus driver   */ \
	/* Usage Page 0x0D (Digitizer) / Usage 0x02 (Pen)            */ \
	/* Report: [tip+inrange(1B)] [X(2B)] [Y(2B)] = 5 data bytes */ \
	/* ========================================================== */ \
	0x05, 0x0D,              /* Usage Page (Digitizer) */ \
	0x09, 0x02,              /* Usage (Pen) */ \
	0xA1, 0x01,              /* Collection (Application) */ \
	0x85, 0x43,              /* Report ID (0x43 = Pen) */ \
	0x09, 0x20,              /* Usage (Stylus) */ \
	0xA1, 0x02,              /* Collection (Logical) */ \
	0x09, 0x42,              /* Usage (Tip Switch) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x25, 0x01,              /* Logical Maximum (1) */ \
	0x75, 0x01,              /* Report Size (1) */ \
	0x95, 0x01,              /* Report Count (1) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0x09, 0x32,              /* Usage (In Range) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0x75, 0x01,              /* Report Size (1) */ \
	0x95, 0x06,              /* Report Count (6) - padding */ \
	0x81, 0x03,              /* Input (Constant) */ \
	0x05, 0x01,              /* Usage Page (Generic Desktop) */ \
	0x09, 0x30,              /* Usage (X) */ \
	0x09, 0x31,              /* Usage (Y) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x26, 0xFF, 0x0F,        /* Logical Maximum (4095) */ \
	0x75, 0x10,              /* Report Size (16) */ \
	0x95, 0x02,              /* Report Count (2) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0xC0,                    /* End Collection (Logical Stylus) */ \
	0xC0,                    /* End Collection (Pen Application) */ \
	\
	/* ========================================================== */ \
	/* Collection 4: Touch Screen -> Windows touch driver         */ \
	/* Usage Page 0x0D (Digitizer) / Usage 0x04 (Touch Screen)   */ \
	/* Report: [tip(1B)] [X(2B)] [Y(2B)] = 5 data bytes          */ \
	/* ========================================================== */ \
	0x05, 0x0D,              /* Usage Page (Digitizer) */ \
	0x09, 0x04,              /* Usage (Touch Screen) */ \
	0xA1, 0x01,              /* Collection (Application) */ \
	0x85, 0x42,              /* Report ID */ \
	0x09, 0x22,              /* Usage (Finger) */ \
	0xA1, 0x02,              /* Collection (Logical) */ \
	0x09, 0x42,              /* Usage (Tip Switch) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x25, 0x01,              /* Logical Maximum (1) */ \
	0x75, 0x01,              /* Report Size (1) */ \
	0x95, 0x01,              /* Report Count (1) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0x75, 0x01,              /* Report Size (1) */ \
	0x95, 0x07,              /* Report Count (7) - padding */ \
	0x81, 0x03,              /* Input (Constant) */ \
	0x05, 0x01,              /* Usage Page (Generic Desktop) */ \
	0x09, 0x30,              /* Usage (X) */ \
	0x09, 0x31,              /* Usage (Y) */ \
	0x15, 0x00,              /* Logical Minimum (0) */ \
	0x26, 0xFF, 0x0F,        /* Logical Maximum (4095) */ \
	0x75, 0x10,              /* Report Size (16) */ \
	0x95, 0x02,              /* Report Count (2) */ \
	0x81, 0x02,              /* Input (Data, Variable, Absolute) */ \
	0xC0,                    /* End Collection (Logical Finger) */ \
	0xC0,                    /* End Collection (Touch Screen Application) */ \
	\
	/* ========================================================== */ \
	/* Collection 5: HPD Sensor V2 -> Windows HID Sensor V2       */ \
	/* Full Windows-compatible Human Presence descriptor          */ \
	/* ========================================================== */ \
	HID_HPD_V2_REPORT_DESCRIPTOR

#endif /* HID_CONSOLIDATED_REPORT_DESC_H_ */
