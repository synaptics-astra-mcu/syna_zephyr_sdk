/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * @brief USB HID profile report descriptor definitions.
 *
 * @file hid_profile_report_descriptors.h
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HID_PROFILE_REPORT_DESCRIPTORS_H_
/* Include guard for per-profile HID descriptor macros. */
#define HID_PROFILE_REPORT_DESCRIPTORS_H_

/* ========================================================================
 * ALS (Ambient Light Sensor) Report Descriptor - Report ID 0x41
 * ======================================================================== */
#define HID_ALS_REPORT_DESCRIPTOR \
	0x06, 0x20, 0xFF,  /* Usage Page (Vendor-defined 0xFF20) */\
	0x09, 0x01,        /* Usage (0x0001 - Vendor device) */\
	0xA1, 0x01,        /* Collection (Application) */\
	0x85, 0x41,        /* Report ID (0x41 = 65 for ALS) */\
	0x09, 0x01,        /* Usage (0x0001) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */\
	0x75, 0x08,        /* Report Size (8) */\
	0x95, 0x01,        /* Report Count (1) */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0xC0               /* End Collection */

/* ========================================================================
 * PTP (Precision TouchPad) Report Descriptor - Report ID 0x42
 * ======================================================================== */
#define HID_PTP_REPORT_DESCRIPTOR \
	0x05, 0x0D,        /* Usage Page (Digitizers) */\
	0x09, 0x05,        /* Usage (Touch Pad) */\
	0xA1, 0x01,        /* Collection (Application) */\
	0x85, 0x42,        /* Report ID (0x42 = 66 for PTP) */\
	0x05, 0x0D,        /* Usage Page (Digitizers) */\
	0x09, 0x22,        /* Usage (Finger) */\
	0xA1, 0x02,        /* Collection (Logical) */\
	0x09, 0x47,        /* Usage (Confidence) */\
	0x09, 0x42,        /* Usage (Tip Switch) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x25, 0x01,        /* Logical Maximum (1) */\
	0x75, 0x01,        /* Report Size (1) */\
	0x95, 0x02,        /* Report Count (2) */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0x95, 0x06,        /* Report Count (6) */\
	0x81, 0x03,        /* Input (Constant, Variable, Absolute) */\
	0x05, 0x01,        /* Usage Page (Generic Desktop) */\
	0x09, 0x30,        /* Usage (X) */\
	0x09, 0x31,        /* Usage (Y) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x26, 0xFF, 0x0F,  /* Logical Maximum (4095) */\
	0x75, 0x10,        /* Report Size (16) */\
	0x95, 0x02,        /* Report Count (2) */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0xC0,              /* End Collection */\
	0xC0               /* End Collection */

/* ========================================================================
 * TSP (Touch Screen/Pen) Report Descriptor - Report ID 0x43
 * ======================================================================== */
#define HID_TSP_REPORT_DESCRIPTOR \
	0x05, 0x0D,        /* Usage Page (Digitizers) */\
	0x09, 0x04,        /* Usage (Touch Screen) */\
	0xA1, 0x01,        /* Collection (Application) */\
	0x85, 0x43,        /* Report ID (0x43 = 67 for TSP) */\
	0x05, 0x0D,        /* Usage Page (Digitizers) */\
	0x09, 0x22,        /* Usage (Finger) */\
	0xA1, 0x02,        /* Collection (Logical) */\
	0x09, 0x42,        /* Usage (Tip Switch) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x25, 0x01,        /* Logical Maximum (1) */\
	0x75, 0x01,        /* Report Size (1) */\
	0x95, 0x01,        /* Report Count (1) */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0x95, 0x07,        /* Report Count (7) */\
	0x81, 0x03,        /* Input (Constant, Variable, Absolute) */\
	0x05, 0x01,        /* Usage Page (Generic Desktop) */\
	0x09, 0x30,        /* Usage (X) */\
	0x09, 0x31,        /* Usage (Y) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x26, 0xFF, 0x0F,  /* Logical Maximum (4095) */\
	0x75, 0x10,        /* Report Size (16) */\
	0x95, 0x02,        /* Report Count (2) */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0xC0,              /* End Collection */\
	0xC0               /* End Collection */

/* ========================================================================
 * CUSTOM Device Report Descriptor - Report ID 0x40
 * ======================================================================== */
#define HID_CUSTOM_REPORT_DESCRIPTOR \
	0x06, 0x00, 0xFF,  /* Usage Page (Vendor-defined 0xFF00) */\
	0x09, 0x01,        /* Usage (0x0001 - Vendor device) */\
	0xA1, 0x01,        /* Collection (Application) */\
	0x85, 0x40,        /* Report ID (0x40 = 64 for Custom) */\
	0x09, 0x01,        /* Usage (0x0001) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */\
	0x75, 0x08,        /* Report Size (8) */\
	0x95, 0x3F,        /* Report Count (63) - allows up to 63 bytes */\
	0x81, 0x02,        /* Input (Data, Variable, Absolute) */\
	0x09, 0x02,        /* Usage (0x0002 - Output) */\
	0x15, 0x00,        /* Logical Minimum (0) */\
	0x26, 0xFF, 0x00,  /* Logical Maximum (255) */\
	0x75, 0x08,        /* Report Size (8) */\
	0x95, 0x3F,        /* Report Count (63) - output data */\
	0x91, 0x02,        /* Output (Data, Variable, Absolute) */\
	0xC0               /* End Collection */

#endif /* HID_PROFILE_REPORT_DESCRIPTORS_H_ */
