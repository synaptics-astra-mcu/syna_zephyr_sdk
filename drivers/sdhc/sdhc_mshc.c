/*
 * Copyright (c) 2025 Synaptics, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_mshc

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sd_spec.h>

#define SDMASA			0x000 /* SDMA System Address Register (32-bit) */
#define BLOCKSIZE		0x004 /* Block size (16-bit) */
#define BLOCKCOUNT		0x006 /* Block count (16-bit) */
#define ARGUMENT		0x008 /* Argument Register (32-bit) */
#define XFER_MODE		0x00C /* Transfer Mode (16-bit) */
#define CMD			0x00E /* Command Registers (16-bit) */
#define RESP01			0x010 /* Response Register 0/1 (32-bit) */
#define RESP23			0x014 /* Response Register 2/3 (32-bit) */
#define RESP45			0x018 /* Response Register 4/5 (32-bit)*/
#define RESP67			0x01C /* Response Register 6/7 (32-bit)*/
#define BUF_DATA		0x020 /* Buffer Data Port Register (32-bit) */
#define PSTATE			0x024 /* Present State Register (32-bit) */
#define HOST_CTRL1		0x028 /* Host Control 1 Register (8-bit) */
#define PWR_CTRL		0x029 /* Power Control Register (8-bit) */
#define BGAP_CTRL		0x02A /* Block Gap Control Register (8-bit) */
#define WUP_CTRL		0x02B /* Wakeup Control Register (8-bit) */
#define CLK_CTRL		0x02C /* Clock Control Register (16-bit) */
#define TOUT_CTRL		0x02E /* Timeout Control Register (8-bit) */
#define SW_RST			0x02F /* Software Reset Register (8-bit) */
#define NORMAL_INT_STAT		0x030 /* Normal Interrupt Status Register (16-bit) */
#define ERROR_INT_STAT		0x032 /* Error Interrupt Status Register (16-bit) */
#define NORMAL_INT_STAT_EN	0x034 /* Normal Interrupt Status Enable Register (16-bit) */
#define ERROR_INT_STAT_EN	0x036 /* Error Interrupt Status Enable Register (16-bit) */
#define NORMAL_INT_SIGNAL_EN	0x038 /* Normal Interrupt Signal Enable Register (16-bit) */
#define ERROR_INT_SIGNAL_EN	0x03A /* Error Interrupt Signal Enable Register (16-bit) */
#define AUTO_CMD_STAT		0x03C /* Auto CMD Status Register (16-bit) */
#define HOST_CTRL2		0x03E /* Host Control 2 Register (16-bit) */
#define CAPABILITIES1		0x040 /* Capabilities 1 Register (32-bit) */
#define CAPABILITIES2		0x044 /* Capabilities 2 Register (32-bit) */
#define CURR_CAPABILITIES1	0x048 /* Current Capabilities 1 Register (32-bit) */
#define CURR_CAPABILITIES2	0x04C /* Current Capabilities 2 Register (32-bit) */
#define FORCE_AUTO_CMD_STAT	0x050 /* Force Auto CMD Status Register (16-bit) */
#define FORCE_ERROR_INT_STAT	0x052 /* Force Error Interrupt Status Register (16-bit) */
#define ADMA_ERR_STAT		0x054 /* ADMA Error Status Register (8-bit) */
#define ADMA_SA_LOW		0x058 /* ADMA System Address Register (Low, 32-bit) */

#define MSHC_SW_RST_R_SW_RST_DAT_Pos                 2
#define MSHC_SW_RST_R_SW_RST_DAT_Msk                (1UL << 2)
#define MSHC_SW_RST_R_SW_RST_CMD_Pos                 1
#define MSHC_SW_RST_R_SW_RST_CMD_Msk                (1UL << 1)
#define MSHC_SW_RST_R_SW_RST_ALL_Pos                 0
#define MSHC_SW_RST_R_SW_RST_ALL_Msk                (1UL << 0)

#define MSHC_PSTATE_REG_CARD_STABLE_Msk             (1UL << 17)
#define MSHC_PSTATE_REG_CARD_INSERTED_Msk           (1UL << 16)
#define MSHC_PSTATE_REG_BUF_RD_ENABLE_Msk           (1UL << 11)
#define MSHC_PSTATE_REG_BUF_WR_ENABLE_Msk           (1UL << 10)
#define MSHC_PSTATE_REG_DAT_LINE_ACTIVE_Msk         (1UL << 2)
#define MSHC_PSTATE_REG_CMD_INHIBIT_DAT_Msk         (1UL << 1)
#define MSHC_PSTATE_REG_CMD_INHIBIT_Msk             (1UL << 0)

#define MSHC_HOST_CTRL1_R_EXT_DAT_XFER_Msk          (1UL << 5)
#define MSHC_HOST_CTRL1_R_DMA_SEL_Pos                3
#define MSHC_HOST_CTRL1_R_DMA_SEL_Msk               (3UL << 3)
#define MSHC_HOST_CTRL1_R_HIGH_SPEED_EN_Msk         (1UL << 2)
#define MSHC_HOST_CTRL1_R_DAT_XFER_WIDTH_Msk        (1UL << 1)

#define MSHC_HOST_CTRL2_R_HOST_VER4_ENABLE_Msk      (1UL << 12)
#define MSHC_HOST_CTRL2_R_SIGNALING_EN_Pos           3
#define MSHC_HOST_CTRL2_R_SIGNALING_EN_Msk          (1UL << 3)

#define MSHC_BLOCKSIZE_R_XFER_BLOCK_SIZE_Msk        (0x0FFFUL << 0)
#define MSHC_BLOCKSIZE_R_SDMA_BUF_BDARY_Pos          12
#define MSHC_BLOCKSIZE_R_SDMA_BUF_BDARY_Msk         (0x7UL << 12)

#define MSHC_XFER_MODE_R_DMA_ENABLE_Msk             (1UL << 0)
#define MSHC_XFER_MODE_R_BLOCK_COUNT_ENABLE_Msk     (1UL << 1)
#define MSHC_XFER_MODE_R_AUTO_CMD_ENABLE_Pos         2
#define MSHC_XFER_MODE_R_AUTO_CMD_ENABLE_Msk        (3UL << 2)
#define MSHC_XFER_MODE_R_DATA_XFER_DIR_Msk          (1UL << 4)
#define MSHC_XFER_MODE_R_MULTI_BLK_SEL_Msk          (1UL << 5)

#define MSHC_CMD_R_RESP_TYPE_SELECT_Pos              0
#define MSHC_CMD_R_RESP_TYPE_SELECT_Msk             (3UL << 0)
#define MSHC_CMD_R_SUB_CMD_FLAG_Pos                  2
#define MSHC_CMD_R_SUB_CMD_FLAG_Msk                 (1UL << 2)
#define MSHC_CMD_R_CMD_CRC_CHK_ENABLE_Pos            3
#define MSHC_CMD_R_CMD_CRC_CHK_ENABLE_Msk           (1UL << 3)
#define MSHC_CMD_R_CMD_IDX_CHK_ENABLE_Pos            4
#define MSHC_CMD_R_CMD_IDX_CHK_ENABLE_Msk           (1UL << 4)
#define MSHC_CMD_R_DATA_PRESENT_SEL_Pos              5
#define MSHC_CMD_R_DATA_PRESENT_SEL_Msk             (1UL << 5)
#define MSHC_CMD_R_CMD_TYPE_Pos                      6
#define MSHC_CMD_R_CMD_TYPE_Msk                     (3UL << 6)
#define MSHC_CMD_R_CMD_INDEX_Pos                     8
#define MSHC_CMD_R_CMD_INDEX_Msk                    (0x3FUL << 8)

#define MSHC_TOUT_CTRL_R_TOUT_CNT_Msk               (0xFUL << 0)

#define MSHC_BGAP_CTRL_R_INT_AT_BGAP_Msk            (1UL << 3)

#define MSHC_CLK_CTRL_R_FREQ_SEL_Pos                 8
#define MSHC_CLK_CTRL_R_FREQ_SEL_Msk                (0xFFUL << 8)
#define MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Pos           6
#define MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Msk          (0x3UL << 6)
#define MSHC_CLK_CTRL_R_PLL_ENABLE_Msk              (1UL << 3)
#define MSHC_CLK_CTRL_R_SD_CLK_EN_Msk               (1UL << 2)
#define MSHC_CLK_CTRL_R_INTERNAL_CLK_STABLE_Msk     (1UL << 1)
#define MSHC_CLK_CTRL_R_INTERNAL_CLK_EN_Msk         (1UL << 0)

#define MSHC_CLK_RAMP_UP_TIME_MS (100UL) /**< The host power ramp up time. */

#define MSHC_CLK_RAMP_UP_TIME_US_WAKEUP (1U)

/* The CLK frequency in kHz during card initialization. */
#define MSHC_INIT_CLK_FREQUENCY_KHZ (SDMMC_CLOCK_400KHZ / 1000)
#define MSHC_3_PERIODS_US (((1000U * 3U) / MSHC_INIT_CLK_FREQUENCY_KHZ) + 1U)

/**
 * Command complete. In SD mode, this event is set
 * after detecting the end bit of a response except for Auto CMD12 and Auto
 * CMD23. This event is not generated when the Response Interrupt is disabled.
 */
#define MSHC_CMD_COMPLETE (0x0001U)

/**
 * Transfer complete. This event is set when a read/write
 * transfer and a command with the Busy Status are completed.
 */
#define MSHC_XFER_COMPLETE (0x0002U)

/**
 * DMA Interrupt. This event is set if the Host Controller
 * detects a SDMA Buffer Boundary during a transfer.
 * For ADMA, the Host controller generates this
 * interrupt by setting the Int field in the
 * descriptor table.
 * This interrupt is not generated after a Transfer
 * Complete.
 */
#define MSHC_DMA_INTERRUPT (0x0008U)

/**
 * The Buffer Write is ready. This event is set if
 * the Buffer Write Enable changes from 0 to 1.
 */
#define MSHC_BUF_WR_READY (0x0010U)

/**
 * The Buffer Read is ready. This event is set if
 * the Buffer Read Enable changes from 0 to 1.
 */
#define MSHC_BUF_RD_READY (0x0020U)

/* Timeouts. */
#define MSHC_INT_CLK_STABLE_TIMEOUT_MS (150U) /* The Internal Clock Stable timeout. */
#define MSHC_1_8_REG_STABLE_TIME_MS (30UL) /* The 1.8 voltage regulator stable time. */
#define MSHC_SUPPLY_RAMP_UP_TIME_MS (35UL) /* The host supply ramp up time. */
#define MSHC_BUS_RAMP_UP_TIME_MS (1000UL) /* The host bus voltage ramp up time. */

#define MSHC_RETRY_TIME (1000UL) /* The number loops to make the timeout in msec. */
#define MSHC_CMD_TIMEOUT_MS (3U) /* The Command complete timeout. */
#define MSHC_BUFFER_RDY_TIMEOUT_MS (150U) /* The Buffer read ready timeout. */
#define MSHC_RD_WR_ENABLE_TIMEOUT (1U) /* The valid data in the Host buffer timeout. */
#define MSHC_WRITE_TIMEOUT_MS (250U) /* The Write timeout for one block. */
#define MSHC_MAX_TIMEOUT (0x0EU)     /* The data max timeout for TOUT_CTRL_R. */
#define MSHC_NCC_MIN_CYCLES (8U) /* The period (clock cycles) between an end bit                   \
	      * of the command and a start bit of the next command.            \
	      */
#define MSHC_NCC_MIN_US ((1000U * MSHC_NCC_MIN_CYCLES) / MSHC_INIT_CLK_FREQUENCY_KHZ)

#define MSHC_ACMD_OFFSET_MASK (0x3FUL)

/* Other constants. */
#define MSHC_FREQ_SEL_MSK (0xFFUL)
#define MSHC_UPPER_FREQ_SEL_POS (8U)

#define MSHC_SDMA_BUF_BYTES_512K (0x7U) /* 512K bytes SDMA Buffer Boundary. */

/* Interrupt masks. */
#define MSHC_NORMAL_INT_MSK (0x1FFFU) /* The enabled Normal Interrupts. */
#define MSHC_ERROR_INT_MSK (0x07FFU)  /* The enabled Error Interrupts. */
#define MSHC_DMA_TXR_BLOCK_SIZE 64 // 128

/** SD command types. */
enum mshc_en_cmd_type {
	MSHC_CMD_NORMAL = 0U,  /**< Other commands */
	MSHC_CMD_ABORT = 3U    /**< CMD12, CMD52 for writing "I/O Abort" in CCCR */
};

/** SD Host reset types. */
enum mshc_en_reset {
	MSHC_RESET_DATALINE = 0U, /**< Reset the data circuit only. */
	MSHC_RESET_CMD_LINE = 1U, /**< Reset the command circuit only. */
	MSHC_RESET_ALL = 2U       /**< Reset the whole SD Host controller. */
};

/** The widths of the data bus. */
enum mshc_en_bus_width {
	MSHC_BUS_WIDTH_1_BIT = 0U, /**< The 1-bit mode data transfer width. */
	MSHC_BUS_WIDTH_4_BIT = 1U, /**< The 4-bit mode data transfer width. */
	MSHC_BUS_WIDTH_8_BIT = 2U  /**< The 8-bit mode data transfer width. */
};

/** The SD bus voltage select. */
enum mshc_en_io_voltage {
	MSHC_IO_VOLT_3_3V = 0U, /**< 3.3V.*/
	MSHC_IO_VOLT_1_8V = 1U  /**< 1.8V. */
};

/** SDHC response types. */
enum mshc_en_response_type {
	MSHC_RESPONSE_NONE = 0U,    /**< No Response. */
	MSHC_RESPONSE_LEN_136 = 1U, /**< Response Length 136. */
	MSHC_RESPONSE_LEN_48 = 2U,  /**< Response Length 48. */
	MSHC_RESPONSE_LEN_48B = 3U  /**< Response Length 48. Check Busy after response. */
};

/** The DMA type enum. */
enum mshc_en_dma_type {
	MSHC_DMA_SDMA = 0U,	  /**< SDMA mode. */
	MSHC_DMA_ADMA2 = 2U,	  /**< ADMA2 mode. */
	MSHC_DMA_ADMA2_ADMA3 = 3U /**< ADMA2-ADMA3 mode. */
};

/** SD Host command configuration structure. */
struct mshc_cmd_config {
	uint32_t command_index;	  /**< The index of the command. */
	uint32_t command_argument; /**< The argument for the command. */
	bool enable_crc_check;	  /**< Enables the CRC check on the response. */
	enum mshc_en_response_type resp_type;  /**< The response type. */
	bool enable_idx_check;	    /**< Checks the index of the response. */
	bool data_present;	    /**< true: Data is present to
				     * be transferred using the DAT line,
				     * false: Commands use the CMD line only.
				     */
	enum mshc_en_cmd_type cmd_type; /**< The command type. */
};

/** The SD Host data transfer configuration structure. */
struct mshc_data_config {
	uint32_t block_size;	/**< The size of the data block. */
	uint32_t number_of_block; /**< The number of blocks to send. */
	bool enable_dma;		/**< Enables DMA for the transaction. */
	uint32_t dma_mode;
	bool read; /**< true = Read from the card, false = Write to the card. */
	uint32_t *data;		  /**< The pointer to data to send/receive or
				   *   the pointer to the DMA descriptor.
				   */
	bool enable_int_at_block_gap; /**< Enables the interrupt generation at the
				     block gap. */
};

struct dwc_mshc_data {
	struct k_event irq_event;
	struct k_mutex mutex;
	uint32_t bus_clock;
	uint32_t dma_mode;  /* MSHC_DMA_SDMA, MSHC_DMA_ADMA2, etc. */
	enum sdhc_bus_mode bus_mode;
	enum sdhc_power power_mode;
	enum sdhc_bus_width bus_width;
	enum sdhc_timing_mode timing;
	enum sd_driver_type driver_type;
	enum sd_voltage signal_voltage;
};

struct dwc_mshc_config {
	const struct pinctrl_dev_config *pincfg;
	void (*irq_config_func)(const struct device *dev);
	struct sdhc_host_props props;
	uintptr_t base;
	const struct device *regulator_vmmc;
	const struct device *regulator_vmmcq;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	const struct reset_dt_spec reset;
};

static int mshc_set_sd_clk_div(uint32_t base, uint16_t clk_div);
static int mshc_set_host_bus_width(const struct dwc_mshc_config *config,
				   enum mshc_en_bus_width width);
static int mshc_enable_card_voltage(const struct dwc_mshc_config *config);
static int mshc_disable_card_voltage(const struct dwc_mshc_config *config);
static int mshc_get_response(const struct dwc_mshc_config *config,
			     uint32_t *responsePtr, bool large_response);
static int mshc_send_command(const struct dwc_mshc_config *config,
			     struct dwc_mshc_data *dev_data,
			     struct mshc_cmd_config const *cmd_config);
static int mshc_init_data_transfer(const struct dwc_mshc_config *config,
				   struct mshc_data_config const *data_config);
static uint32_t mshc_buffer_read(uint32_t base);
static void mshc_buffer_write(uint32_t base, uint32_t data);
static void mshc_change_io_voltage(const struct dwc_mshc_config *config,
				   enum mshc_en_io_voltage io_voltage);
static bool mshc_is_card_connected(uint32_t base);
static void mshc_software_reset(const struct dwc_mshc_config *config,
				enum mshc_en_reset reset);

static int mshc_cmd_rx_data(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data,
			    struct mshc_data_config *data_config);
static int mshc_cmd_tx_data(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data,
			    struct mshc_data_config *data_config);

static int mshc_ops_go_idle(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data);
static int mshc_card_change_clock(const struct dwc_mshc_config *config,
				  uint32_t frequency);
static int mshc_poll_buffer_read_ready(const struct dwc_mshc_config *config,
				       struct dwc_mshc_data *dev_data);
static int mshc_poll_buffer_write_ready(const struct dwc_mshc_config *config,
					struct dwc_mshc_data *dev_data);
static int mshc_poll_cmd_complete(const struct dwc_mshc_config *config,
				  struct dwc_mshc_data *dev_data);
static int mshc_poll_transfer_complete(const struct dwc_mshc_config *config,
				       struct dwc_mshc_data *dev_data);
static void mshc_normal_reset(const struct dwc_mshc_config *config);
static int mshc_poll_cmd_line_free(uint32_t base);
static int mshc_poll_data_line_no_inhibit(uint32_t base);
static int mshc_poll_data_line_free(uint32_t base);

static void mshc_enable_sd_clk(uint32_t base);
static void mshc_disable_sd_clk(uint32_t base);
static void mshc_set_normal_interrupt_enable(uint32_t base,
					     uint32_t interrupt);
static void mshc_clear_normal_interrupt_status(uint32_t base,
					       uint32_t status);
static void mshc_set_error_interrupt_enable(uint32_t base,
					    uint32_t interrupt);
static uint32_t mshc_get_normal_interrupt_status(uint32_t base);

static void mshc_isr(const struct device *dev)
{
	const struct dwc_mshc_config *config = dev->config;
	struct dwc_mshc_data *dev_data = dev->data;
	uint32_t int_stat = sys_read16(config->base + NORMAL_INT_STAT);

	if (int_stat & (1 << 15)) {
		uint32_t error_stat = sys_read16(config->base + ERROR_INT_STAT);
		sys_write16(error_stat, config->base + ERROR_INT_STAT);
	}

	/* Command complete */
	if (int_stat & MSHC_CMD_COMPLETE) {
		sys_write16(MSHC_CMD_COMPLETE, config->base + NORMAL_INT_STAT);
		k_event_post(&dev_data->irq_event, MSHC_CMD_COMPLETE);
	}

	/* Transfer complete */
	if (int_stat & MSHC_XFER_COMPLETE) {
		sys_write16(MSHC_XFER_COMPLETE, config->base + NORMAL_INT_STAT);
		k_event_post(&dev_data->irq_event, MSHC_XFER_COMPLETE);
	}

	/* Buffer read ready */
	if (int_stat & MSHC_BUF_RD_READY) {
		sys_write16(MSHC_BUF_RD_READY, config->base + NORMAL_INT_STAT);
		k_event_post(&dev_data->irq_event, MSHC_BUF_RD_READY);
	}

	/* Buffer write ready */
	if (int_stat & MSHC_BUF_WR_READY) {
		sys_write16(MSHC_BUF_WR_READY, config->base + NORMAL_INT_STAT);
		k_event_post(&dev_data->irq_event, MSHC_BUF_WR_READY);
	}
}

static int mshc_poll_cmd_line_free(uint32_t base)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	while (retry > 0UL) {
		if (!(sys_read32(base + PSTATE) & MSHC_PSTATE_REG_CMD_INHIBIT_Msk)) {
			ret = 0;
			break;
		}

		k_usleep(MSHC_CMD_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

static int mshc_poll_data_line_no_inhibit(uint32_t base)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	while (retry > 0UL) {
		if (!(sys_read32(base + PSTATE) & MSHC_PSTATE_REG_CMD_INHIBIT_DAT_Msk)) {
			ret = 0;
			break;
		}

		k_usleep(MSHC_CMD_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

static int mshc_poll_data_line_free(uint32_t base)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = 0;

	while ((sys_read32(base + PSTATE) & MSHC_PSTATE_REG_DAT_LINE_ACTIVE_Msk) &&
	       (retry > 0UL)) {
		k_usleep(MSHC_WRITE_TIMEOUT_MS);
		retry--;
	}

	if (sys_read32(base + PSTATE) & MSHC_PSTATE_REG_DAT_LINE_ACTIVE_Msk) {
		ret = -ETIMEDOUT;
	}

	return ret;
}

static int mshc_wait_for_irq(const struct dwc_mshc_config *config,
			     struct dwc_mshc_data *dev_data,
			     uint32_t mask, uint32_t timeout)
{
	uint32_t events;

	events = k_event_wait(&dev_data->irq_event, mask, false, K_MSEC(timeout));
	if (events & mask) {
		return 0;
	}

	return -ETIMEDOUT;
}

static int mshc_poll_buffer_read_ready(const struct dwc_mshc_config *config,
				       struct dwc_mshc_data *dev_data)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	if (config->irq_config_func != NULL) {
		return mshc_wait_for_irq(config, dev_data, MSHC_BUF_RD_READY,
					 MSHC_BUFFER_RDY_TIMEOUT_MS * MSHC_RETRY_TIME);
	}

	while (retry > 0UL) {
		/* Check the Buffer Read ready */
		if (sys_read16(config->base + NORMAL_INT_STAT) & MSHC_BUF_RD_READY) {
			/* Clear the interrupt flag */
			sys_write16(MSHC_BUF_RD_READY, config->base + NORMAL_INT_STAT);

			ret = 0;
			break;
		}

		k_usleep(MSHC_BUFFER_RDY_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

static int mshc_poll_buffer_write_ready(const struct dwc_mshc_config *config,
					struct dwc_mshc_data *dev_data)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	if (config->irq_config_func != NULL) {
		return mshc_wait_for_irq(config, dev_data, MSHC_BUF_WR_READY,
					 MSHC_BUFFER_RDY_TIMEOUT_MS * MSHC_RETRY_TIME);
	}

	while (retry > 0UL) {
		/* Check the Buffer Write ready */
		if (sys_read16(config->base + NORMAL_INT_STAT) & MSHC_BUF_WR_READY) {
			/* Clear the interrupt flag */
			sys_write16(MSHC_BUF_WR_READY, config->base + NORMAL_INT_STAT);

			ret = 0;
			break;
		}

		k_usleep(MSHC_BUFFER_RDY_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

static void mshc_enable_sd_clk(uint32_t base)
{
	uint16_t value = sys_read16(base + CLK_CTRL);

	sys_write16((uint16_t)(value | MSHC_CLK_CTRL_R_SD_CLK_EN_Msk |
		    MSHC_CLK_CTRL_R_PLL_ENABLE_Msk),
		    base + CLK_CTRL);
}

static void mshc_disable_sd_clk(uint32_t base)
{
	uint16_t value = sys_read16(base + CLK_CTRL);

	/* Disable SD CLK */
	value &= ~MSHC_CLK_CTRL_R_SD_CLK_EN_Msk;
	sys_write16(value, base + CLK_CTRL);

	/* Wait for at least 3 card clock periods */
	k_usleep(MSHC_3_PERIODS_US);

	/* Disable PLL */
	value &= ~MSHC_CLK_CTRL_R_PLL_ENABLE_Msk;
	sys_write16(value, base + CLK_CTRL);
}

static void mshc_set_normal_interrupt_enable(uint32_t base,
					     uint32_t interrupt)
{
	sys_write16((uint16_t)interrupt, base + NORMAL_INT_STAT_EN);
}

static void mshc_clear_normal_interrupt_status(uint32_t base,
					       uint32_t status)
{
	sys_write16((uint16_t)status, base + NORMAL_INT_STAT);
}

static void mshc_set_error_interrupt_enable(uint32_t base,
					    uint32_t interrupt)
{
	sys_write16((uint16_t)interrupt, base + ERROR_INT_STAT_EN);
}

static uint32_t mshc_get_normal_interrupt_status(uint32_t base)
{
	uint32_t ret;

	ret = (uint32_t)sys_read16(base + NORMAL_INT_STAT);

	return ret;
}

void mshc_set_normal_interrupt_mask(uint32_t base, uint32_t interrupt_mask)
{
	sys_write16((uint16_t)interrupt_mask, base + NORMAL_INT_SIGNAL_EN);
}

static void mshc_buffer_write(uint32_t base, uint32_t data)
{
	sys_write32(data, base + BUF_DATA);
}

static uint32_t mshc_buffer_read(uint32_t base)
{
	/* Return the Buffer Data Port Register value */
	return sys_read32(base + BUF_DATA);
}

static int mshc_poll_cmd_complete(const struct dwc_mshc_config *config,
				  struct dwc_mshc_data *dev_data)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	if (config->irq_config_func != NULL) {
		return mshc_wait_for_irq(config, dev_data, MSHC_CMD_COMPLETE,
					 MSHC_CMD_TIMEOUT_MS * MSHC_RETRY_TIME);
	}

	while (retry > 0UL) {
		/* Command Complete */
		if (sys_read16(config->base + NORMAL_INT_STAT) & MSHC_CMD_COMPLETE) {
			/* Clear the interrupt flag */
			sys_write16(MSHC_CMD_COMPLETE, config->base + NORMAL_INT_STAT);

			ret = 0;
			break;
		}

		k_usleep(MSHC_CMD_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

static int mshc_poll_transfer_complete(const struct dwc_mshc_config *config,
				       struct dwc_mshc_data *dev_data)
{
	uint32_t retry = MSHC_RETRY_TIME;
	int ret = -ETIMEDOUT;

	if (config->irq_config_func != NULL) {
		return mshc_wait_for_irq(config, dev_data, MSHC_XFER_COMPLETE,
					 MSHC_WRITE_TIMEOUT_MS * MSHC_RETRY_TIME);
	}

	while (retry > 0UL) {
		/* Transfer Complete */
		if (sys_read16(config->base + NORMAL_INT_STAT) & MSHC_XFER_COMPLETE) {
			/* Clear the interrupt flag */
			sys_write16(MSHC_XFER_COMPLETE, config->base + NORMAL_INT_STAT);

			ret = 0;
			break;
		}

		k_usleep(MSHC_WRITE_TIMEOUT_MS);
		retry--;
	}

	return ret;
}

/*
 * Reads the command data using a non-DMA data transfer.
 * This function is blocking (it exits after all data is read).
 */
static int mshc_cmd_rx_data(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data,
			    struct mshc_data_config *pcmd)
{
	uint32_t blk_size;
	uint32_t blk_cnt;
	uint32_t i;
	uint32_t retry;
	int ret;

	blk_cnt = pcmd->number_of_block;
	blk_size = pcmd->block_size;

	while (blk_cnt > 0UL) {
		ret = mshc_poll_buffer_read_ready(config, dev_data);
		if (0 != ret) {
			break;
		}

		for (i = blk_size >> 2UL; i != 0UL; i--) {
			/* Wait if valid data exists in the Host buffer. */
			retry = MSHC_RETRY_TIME;
			while (!(sys_read32(config->base + PSTATE) &
					MSHC_PSTATE_REG_BUF_RD_ENABLE_Msk) &&
			    (retry > 0UL)) {
				k_usleep(MSHC_RD_WR_ENABLE_TIMEOUT);
				retry--;
			}

			if (!(sys_read32(config->base + PSTATE) &
					MSHC_PSTATE_REG_BUF_RD_ENABLE_Msk)) {
				break;
			}

			/* Read data from the Host buffer. */
			*pcmd->data = mshc_buffer_read(config->base);
			pcmd->data++;
		}
		blk_cnt--;
	}

	/* Wait for the Transfer Complete. */
	ret = mshc_poll_transfer_complete(config, dev_data);
	if (ret == 0) {
		ret = mshc_poll_cmd_line_free(config->base);
	}
	if (ret == 0) {
		ret = mshc_poll_data_line_no_inhibit(config->base);
	}

	return ret;
}

/*
 * Writes the command data using a non-DMA data transfer.
 * This function is blocking (it exits after all data is written).
 */
static int mshc_cmd_tx_data(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data,
			    struct mshc_data_config *pcmd)
{
	int ret;
	uint32_t blk_size;
	uint32_t blk_cnt;
	uint32_t i;
	uint32_t retry;

	blk_cnt = pcmd->number_of_block;
	blk_size = pcmd->block_size;

	while (0UL < blk_cnt) {
		/* Wait for the Buffer Write ready. */
		ret = mshc_poll_buffer_write_ready(config, dev_data);

		if (0 != ret) {
			break;
		}

		for (i = blk_size >> 2UL; i != 0UL; i--) {
			/* Wait if space is available for writing data. */
			retry = MSHC_RETRY_TIME;
			while (!(sys_read32(config->base + PSTATE) &
					    MSHC_PSTATE_REG_BUF_WR_ENABLE_Msk) &&
				 (retry > 0UL)) {
				k_usleep(MSHC_RD_WR_ENABLE_TIMEOUT);
				retry--;
			}

			if (!(sys_read32(config->base + PSTATE) &
					MSHC_PSTATE_REG_BUF_WR_ENABLE_Msk)) {
				break;
			}

			/* Write data to the Host buffer. */
			mshc_buffer_write(config->base, *pcmd->data);
			pcmd->data++;
		}
		blk_cnt--;
	}

	ret = mshc_poll_transfer_complete(config, dev_data);

	if (0 == ret) {
		/* Check if DAT line is active. */
		ret = mshc_poll_data_line_free(config->base);
	}

	return ret;
}

/*
 * Starts sending a command on the SD bus. If the command uses the data lines
 * mshc_init_data_transfer() must be called first.
 * This function returns before the command completes.
 * To determine if the command is done, read the Normal Interrupt Status register
 * and check the CMD_COMPLETE flag. To determine if the entire transfer is done
 * check the XFER_COMPLETE flag. Also the interrupt is used and flags are set
 * on these events in an ISR.
 * It is the user's responsibility to clear the MSHC_CMD_COMPLETE flag
 * after calling this function.
 */
static int
mshc_send_command(const struct dwc_mshc_config *config,
		  struct dwc_mshc_data *dev_data,
		  struct mshc_cmd_config const *cmd_config)
{
	uint16_t cmd_reg = 0;
	int ret = 0;

	if (config->irq_config_func != NULL) {
		k_event_clear(&dev_data->irq_event,
			      MSHC_CMD_COMPLETE | MSHC_XFER_COMPLETE |
			      MSHC_BUF_RD_READY | MSHC_BUF_WR_READY);
	}

	ret = mshc_poll_cmd_line_free(config->base);
	if (0 != ret) {
		return ret;
	}

	if ((true == cmd_config->data_present) &&
	    (MSHC_CMD_ABORT != cmd_config->cmd_type)) {
		/* Check the DAT line inhibits only commands
		 * with the DAT line is used and when the
		 * command is not the ABORT type.
		 */
		ret = mshc_poll_data_line_no_inhibit(config->base);
		if (0 != ret) {
			return ret;
		}
	}

	/* Set the command_argument directly to the
	 * hardware register. */
	sys_write32(cmd_config->command_argument, config->base + ARGUMENT);

	sys_write16(0xffff, config->base + NORMAL_INT_STAT);
	sys_write16(0xffff, config->base + ERROR_INT_STAT);

	/* Update the command hardware register (the
	 * command is sent) according to the spec, this
	 * register should be written only once.
	 */

	/* Set response type field */
	cmd_reg |= (cmd_config->resp_type << MSHC_CMD_R_RESP_TYPE_SELECT_Pos) &
		   MSHC_CMD_R_RESP_TYPE_SELECT_Msk;

	/* Set command type field */
	cmd_reg |= (cmd_config->cmd_type << MSHC_CMD_R_CMD_TYPE_Pos) &
		   MSHC_CMD_R_CMD_TYPE_Msk;

	/* Set data present field */
	if (cmd_config->data_present) {
		cmd_reg |= MSHC_CMD_R_DATA_PRESENT_SEL_Msk;
	}

	/* Set index check enable */
	if (cmd_config->enable_idx_check) {
		cmd_reg |= MSHC_CMD_R_CMD_IDX_CHK_ENABLE_Msk;
	}

	/* Set CRC check enable */
	if (cmd_config->enable_crc_check) {
		cmd_reg |= MSHC_CMD_R_CMD_CRC_CHK_ENABLE_Msk;
	}

	/* Set command index */
	cmd_reg |= (cmd_config->command_index & MSHC_ACMD_OFFSET_MASK) <<
		   MSHC_CMD_R_CMD_INDEX_Pos;

	sys_write16(cmd_reg, config->base + CMD);

	return ret;
}

static int mshc_ops_go_idle(const struct dwc_mshc_config *config,
			    struct dwc_mshc_data *dev_data)
{
	struct mshc_cmd_config cmd;
	int ret;

	cmd.command_index = SD_GO_IDLE_STATE;
	cmd.command_argument = 0UL;
	cmd.enable_crc_check = false;
	cmd.resp_type = MSHC_RESPONSE_NONE;
	cmd.enable_idx_check = false;
	cmd.data_present = false;
	cmd.cmd_type = MSHC_CMD_NORMAL;

	ret = mshc_send_command(config, dev_data, &cmd);

	if (0 == ret) {
		/* Wait for the Command Complete event. */
		ret = mshc_poll_cmd_complete(config, dev_data);
	}

	k_usleep(MSHC_NCC_MIN_US);

	return ret;
}

static int mshc_card_change_clock(const struct dwc_mshc_config *config,
				  uint32_t frequency)
{
	uint32_t clk_div;
	uint32_t clock_input;
	int ret;

	ret = clock_control_get_rate(config->clock_dev, config->clock_subsys,
				     &clock_input);
	if (ret != 0)
		return ret;

	clk_div = (clock_input / frequency) >> 1UL;
	mshc_disable_sd_clk(config->base);
	ret = mshc_set_sd_clk_div(config->base, (uint16_t)clk_div);
	mshc_enable_sd_clk(config->base);

	return ret;
}

static void mshc_normal_reset(const struct dwc_mshc_config *config)
{
	uint32_t int_normal; /* The normal events mask. */

	int_normal = mshc_get_normal_interrupt_status(config->base);

	/* Check the normal event. */
	if (0UL < int_normal) {
		/* Clear the normal event. */
		mshc_clear_normal_interrupt_status(config->base, int_normal);
	}
}

/*
 * The divider is clocked from the CLK_HF clock (100 MHz). To determine
 * the SD bus speed divide the clock CLK_HF by the divider value passed
 * in this function. The divider value is 2*clk_div.
 */
static int mshc_set_sd_clk_div(uint32_t base, uint16_t clk_div)
{
	uint16_t value = sys_read16(base + CLK_CTRL);
	uint16_t clk_val;

	/* Clear the frequency divider bits */
	clk_val = value & ~(MSHC_CLK_CTRL_R_FREQ_SEL_Msk |
			   MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Msk);

	/* Set the lower 8 bits of the frequency divider */
	clk_val |= ((clk_div & MSHC_FREQ_SEL_MSK) << MSHC_CLK_CTRL_R_FREQ_SEL_Pos) &
		   MSHC_CLK_CTRL_R_FREQ_SEL_Msk;

	/* Set the upper 2 bits of the frequency divider */
	clk_val |= (((clk_div >> MSHC_UPPER_FREQ_SEL_POS) &
		     ((MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Msk >>
		       MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Pos))) <<
		    MSHC_CLK_CTRL_R_UPPER_FREQ_SEL_Pos);

	sys_write16(clk_val, base + CLK_CTRL);

	/* Wait for at least 3 card clock periods */
	k_usleep(MSHC_3_PERIODS_US);

	return 0;
}

/*
 * Only changes the bus width on the host side (not card side).
 * To change the bus width on the card, call mshc_set_bus_width().
 */
static int
mshc_set_host_bus_width(const struct dwc_mshc_config *config,
			enum mshc_en_bus_width width)
{
	uint8_t value = sys_read8(config->base + HOST_CTRL1);
	uint8_t new_value;

	/* Clear existing width bits */
	new_value = value & ~(MSHC_HOST_CTRL1_R_EXT_DAT_XFER_Msk |
			     MSHC_HOST_CTRL1_R_DAT_XFER_WIDTH_Msk);

	/* Set 8-bit width bit if width == 8-bit */
	if (width == MSHC_BUS_WIDTH_8_BIT) {
		new_value |= MSHC_HOST_CTRL1_R_EXT_DAT_XFER_Msk;
	}

	/* Set 4-bit width bit if width == 4-bit */
	if (width == MSHC_BUS_WIDTH_4_BIT) {
		new_value |= MSHC_HOST_CTRL1_R_DAT_XFER_WIDTH_Msk;
	}

	sys_write8(new_value, config->base + HOST_CTRL1);
	return 0;
}

static int mshc_enable_card_voltage(const struct dwc_mshc_config *config)
{
	return regulator_enable(config->regulator_vmmc);
}

static int mshc_disable_card_voltage(const struct dwc_mshc_config *config)
{
	return regulator_disable(config->regulator_vmmc);
}

static int
mshc_get_response(const struct dwc_mshc_config *config,
		  uint32_t *response, bool large_response)
{
	int ret = -EINVAL;
	uint8_t i;
	uint8_t response_length = (true == large_response) ?  4U : 1U;

	if (NULL != response) {
		/* Read the large_response Response registers values */
		for (i = 0U; i < response_length; i++) {
			*response = sys_read32(config->base + RESP01 + (i * 4));
			response++;
		}

		ret = 0;
	}

	return ret;
}

/*
 * Initializes the SD block for a data transfer. It does not start a transfer.
 * To start a transfer call mshc_send_command() after calling this function.
 * If DMA is not used for data transfer, the buffer needs to be filled
 * with data first if this is a write.
 */
static int
mshc_init_data_transfer(const struct dwc_mshc_config *config,
			struct mshc_data_config const *data_config)
{
	uint32_t transfer_mode;
	uint32_t value;
	uint32_t data_timeout = MSHC_MAX_TIMEOUT;

	sys_write16(0xffff, config->base + ERROR_INT_STAT);

	value = sys_read32(config->base + PSTATE);
	/* Check if DAT or CMD line is active */
	if ((value & MSHC_PSTATE_REG_DAT_LINE_ACTIVE_Msk) ||
	    (value & MSHC_PSTATE_REG_CMD_INHIBIT_Msk) ||
	    (value & MSHC_PSTATE_REG_CMD_INHIBIT_DAT_Msk)) {
		return -EIO;
	}

	sys_write16(0U, config->base + BLOCKSIZE);
	sys_write16(0U, config->base + XFER_MODE);

	if (data_config->enable_dma) {
		if ((uint32_t)MSHC_DMA_SDMA == data_config->dma_mode) {
			/* Set 512K bytes SDMA Buffer Boundary. */
			value = sys_read16(config->base + BLOCKSIZE);
			/* Clear SDMA buffer boundary bits and set new value */
			value &= ~MSHC_BLOCKSIZE_R_SDMA_BUF_BDARY_Msk;
			value |= MSHC_SDMA_BUF_BYTES_512K <<
				 MSHC_BLOCKSIZE_R_SDMA_BUF_BDARY_Pos;
			sys_write16(value, config->base + BLOCKSIZE);

			value = sys_read16(config->base + HOST_CTRL2);
			if (value & MSHC_HOST_CTRL2_R_HOST_VER4_ENABLE_Msk) {
				/* The data address. */
				sys_write32((uint32_t)data_config->data,
					    config->base + ADMA_SA_LOW);

				/* Set the block count. */
				sys_write32(data_config->number_of_block,
					    config->base + SDMASA);
			} else {
				/* The data address. */
				sys_write32((uint32_t)data_config->data,
					    config->base + SDMASA);
			}
		}
		/* ADMA2 mode not yet supported. */
	} else {
		/* Set the block count. */
		sys_write32(data_config->number_of_block, config->base + SDMASA);
	}

	/* Set the block size. */
	value = sys_read16(config->base + BLOCKSIZE);
	value &= ~MSHC_BLOCKSIZE_R_XFER_BLOCK_SIZE_Msk;
	value |= data_config->block_size;
	sys_write16(value, config->base + BLOCKSIZE);

	/* Set the block count. */
	sys_write16((uint16_t)data_config->number_of_block, config->base + BLOCKCOUNT);

	/* Set a multi- or single-block transfer.*/
	transfer_mode = (1U < data_config->number_of_block) ?
		       MSHC_XFER_MODE_R_MULTI_BLK_SEL_Msk : 0;

	/* Set the data transfer direction. */
	if (data_config->read) {
		transfer_mode |= MSHC_XFER_MODE_R_DATA_XFER_DIR_Msk;
	}

	/* Set the block count enable. */
	transfer_mode |= MSHC_XFER_MODE_R_BLOCK_COUNT_ENABLE_Msk;

	/* Enable the DMA or not. */
	if (data_config->enable_dma) {
		transfer_mode |= MSHC_XFER_MODE_R_DMA_ENABLE_Msk;
	}

	/* Set an interrupt at the block gap. */
	value = sys_read8(config->base + BGAP_CTRL);
	if (data_config->enable_int_at_block_gap)
		value |= MSHC_BGAP_CTRL_R_INT_AT_BGAP_Msk;
	else
		value &= ~MSHC_BGAP_CTRL_R_INT_AT_BGAP_Msk;
	sys_write8(value, config->base + BGAP_CTRL);

	/* Set the data timeout (Base clock*2^27). */
	value = sys_read8(config->base + TOUT_CTRL);
	value = (value & ~MSHC_TOUT_CTRL_R_TOUT_CNT_Msk) |
		(data_timeout & MSHC_TOUT_CTRL_R_TOUT_CNT_Msk);
	sys_write8(value, config->base + TOUT_CTRL);

	/* The auto-command setting. */
	/* Clear auto-cmd bits first */
	transfer_mode &= ~MSHC_XFER_MODE_R_AUTO_CMD_ENABLE_Msk;

	sys_write16((uint16_t)transfer_mode, config->base + XFER_MODE);

	return 0;
}

/*
 * Changes the logic level on the sd_io_volt_sel line. It assumes that
 * this line is used to control a regulator connected to the VDDIO of the PSoC.
 * This regulator allows for switching between the 3.3V and 1.8V signaling.
 */
static void mshc_change_io_voltage(const struct dwc_mshc_config *config,
				   enum mshc_en_io_voltage io_voltage)
{
	int ret;
	uint16_t value;

	/* Set the 1.8V signaling enable. */
	value = sys_read16(config->base + HOST_CTRL2);
	value = (value & ~MSHC_HOST_CTRL2_R_SIGNALING_EN_Msk);

	if (io_voltage == MSHC_IO_VOLT_1_8V) {
		value |= MSHC_HOST_CTRL2_R_SIGNALING_EN_Msk;
	}

	sys_write16(value, config->base + HOST_CTRL2);

	if (io_voltage == MSHC_IO_VOLT_3_3V) {
		ret = regulator_set_voltage(config->regulator_vmmcq, 3300000,
					    3300000);
	} else if (io_voltage == MSHC_IO_VOLT_1_8V) {
		ret = regulator_set_voltage(config->regulator_vmmcq, 1800000,
					    1800000);
	}
}

static bool mshc_is_card_connected(uint32_t base)
{
	while (!(sys_read32(base + PSTATE) & MSHC_PSTATE_REG_CARD_STABLE_Msk)) {
		/* Wait until the card is stable. */
	}

	return !!(sys_read32(base + PSTATE) & MSHC_PSTATE_REG_CARD_INSERTED_Msk);
}

static void mshc_software_reset(const struct dwc_mshc_config *config,
				enum mshc_en_reset reset)
{
	uint8_t sw_rst_val;
	uint16_t clk_ctrl_val;

	switch (reset) {
	case MSHC_RESET_DATALINE:
		sys_write8(MSHC_SW_RST_R_SW_RST_DAT_Msk, config->base + SW_RST);

		/* Wait for at least 3 card clock periods */
		k_usleep(MSHC_3_PERIODS_US);

		while ((sw_rst_val = sys_read8(config->base + SW_RST)) &
		       MSHC_SW_RST_R_SW_RST_DAT_Msk) {
			/* Wait until the reset completes. */
		}

		break;
	case MSHC_RESET_CMD_LINE:
		sys_write8(MSHC_SW_RST_R_SW_RST_CMD_Msk, config->base + SW_RST);

		/* Wait for at least 3 card clock periods */
		k_usleep(MSHC_3_PERIODS_US);

		while ((sw_rst_val = sys_read8(config->base + SW_RST)) &
		       MSHC_SW_RST_R_SW_RST_CMD_Msk) {
			/* Wait until the reset completes. */
		}

		break;
	case MSHC_RESET_ALL:
		sys_write16(0U, config->base + CLK_CTRL);

		/* Wait for at least 3 card clock periods */
		k_usleep(MSHC_3_PERIODS_US);

		sys_write8(MSHC_SW_RST_R_SW_RST_ALL_Msk, config->base + SW_RST);

		while ((sw_rst_val = sys_read8(config->base + SW_RST)) &
		       MSHC_SW_RST_R_SW_RST_ALL_Msk) {
			/* Wait until the reset completes. */
		}

		/* Enable the Internal clock. */
		sys_write16(MSHC_CLK_CTRL_R_INTERNAL_CLK_EN_Msk,
			    config->base + CLK_CTRL);

		while (!((clk_ctrl_val = sys_read16(config->base + CLK_CTRL)) &
			 MSHC_CLK_CTRL_R_INTERNAL_CLK_STABLE_Msk)) {
			/* Wait for the stable Internal Clock. */
		}

		break;
	default:
		/* Unknown Reset selection*/
		break;
	}
}

static int dwc_mshc_request(const struct device *dev, struct sdhc_command *cmd,
			    struct sdhc_data *sd_data)
{
	const struct dwc_mshc_config *config = dev->config;
	struct dwc_mshc_data *dev_data = dev->data;
	struct mshc_cmd_config dwc_cmd = { 0 };
	struct mshc_data_config dwc_data = { 0 };
	int retries = (int)(cmd->retries + 1); /* first try plus retries */
	int ret = 0;

	ret = k_mutex_lock(&dev_data->mutex, K_MSEC(cmd->timeout_ms));
	if (ret != 0) {
		return -EBUSY;
	}

	dwc_cmd.command_index = cmd->opcode;
	dwc_cmd.command_argument = cmd->arg;
	dwc_cmd.data_present = false;
	dwc_cmd.resp_type = MSHC_RESPONSE_LEN_48;
	dwc_cmd.enable_crc_check = true;
	dwc_cmd.enable_idx_check = true;
	dwc_cmd.cmd_type = MSHC_CMD_NORMAL;

	if (sd_data != NULL) {
		dwc_cmd.data_present = true;

		dwc_data.block_size = sd_data->block_size;
		dwc_data.number_of_block = sd_data->blocks;
		if (sd_data->block_size >= MSHC_DMA_TXR_BLOCK_SIZE) {
			dwc_data.enable_dma = true;
		} else {
			dwc_data.enable_dma = false;
		}
		dwc_data.dma_mode = dev_data->dma_mode;
		dwc_data.read = true;
		dwc_data.data = (uint32_t *)sd_data->data;
		dwc_data.enable_int_at_block_gap = false;
		if (1UL < sd_data->blocks) {
			dwc_data.enable_int_at_block_gap = true;
		}
	}

	switch (cmd->opcode) {
	case /*  0 */ SD_GO_IDLE_STATE:
		mshc_ops_go_idle(config, dev_data);
		mshc_software_reset(config, MSHC_RESET_CMD_LINE);
		goto out;
	case /*  2 */ SD_ALL_SEND_CID:
	case /*  9 */ SD_SEND_CSD:
		dwc_cmd.resp_type = MSHC_RESPONSE_LEN_136;
		dwc_cmd.enable_idx_check = false;
		break;
	case /*  7 */ SD_SELECT_CARD:
		if (cmd->arg > 0) {
			dwc_cmd.resp_type = MSHC_RESPONSE_LEN_48B;
		} else {
			dwc_cmd.resp_type = MSHC_RESPONSE_NONE;
		}
		dwc_cmd.enable_crc_check = false;
		dwc_cmd.enable_idx_check = false;
		break;
	case /*  1 */ MMC_SEND_OP_COND:
	case /*  5 */ SDIO_SEND_OP_COND:
	case /* 41 */ SD_APP_SEND_OP_COND:
		dwc_cmd.enable_crc_check = false;
		dwc_cmd.enable_idx_check = false;
		break;
	case /* 12 */ SD_STOP_TRANSMISSION:
		dwc_cmd.cmd_type = MSHC_CMD_ABORT;
	case /*  3 */ SD_SEND_RELATIVE_ADDR:
	case /*  6 */ SD_SWITCH:
	case /*  8 */ SD_SEND_IF_COND:
	case /* 13 */ SD_SEND_STATUS:
	case /* 11 */ SD_VOL_SWITCH:
	case /* 52 */ SDIO_RW_DIRECT:
	case /* 55 */ SD_APP_CMD:
		break;
	case /* 51 */ SD_APP_SEND_SCR:
		dwc_cmd.resp_type = MSHC_RESPONSE_LEN_48B;
		break;
	case /* 17 */ SD_READ_SINGLE_BLOCK:
	case /* 18 */ SD_READ_MULTIPLE_BLOCK:
	case /* 24 */ SD_WRITE_SINGLE_BLOCK:
	case /* 25 */ SD_WRITE_MULTIPLE_BLOCK:
	case /* 53 */ SDIO_RW_EXTENDED:
		if (cmd->opcode == SD_WRITE_SINGLE_BLOCK ||
		    cmd->opcode == SD_WRITE_MULTIPLE_BLOCK ||
		    (cmd->arg & BIT(SDIO_CMD_ARG_RW_SHIFT))) {
			dwc_data.read = false;
		}
		break;
	default:
		ret = -ENOTSUP;
		goto out;
	}

	if (sd_data) {
		/* TODO: clear the cache before passing the ADMA Descriptor Table */
		ret = mshc_init_data_transfer(config, &dwc_data);
		if (ret != 0) {
			goto out;
		}
	}

	while (retries > 0) {
		ret = mshc_send_command(config, dev_data, &dwc_cmd);
		if (0 == ret && (!sd_data || !dwc_data.enable_dma)) {
			/* Wait for the Command Complete event. */
			ret = mshc_poll_cmd_complete(config, dev_data);
		}
		if (ret) {
			retries--; /* error, retry */
		} else {
			break;
		}
	}

	if (ret == 0 && sd_data) {
		if (!dwc_data.enable_dma) {
			/* Wait for the response on the DAT lines. */
			if (dwc_data.read) {
				ret = mshc_cmd_rx_data(config, dev_data, &dwc_data);
			} else {
				ret = mshc_cmd_tx_data(config, dev_data, &dwc_data);
			}
		}
		if (ret == 0) {
			ret = mshc_poll_transfer_complete(config, dev_data);
		}
		if (ret == 0) {
			sd_data->bytes_xfered = sd_data->blocks * sd_data->block_size;
		}
	}

	k_usleep(MSHC_NCC_MIN_US);

	if (dwc_cmd.resp_type == MSHC_RESPONSE_LEN_136) {
		ret =
		    mshc_get_response(config, (uint32_t *)&cmd->response, true);
	} else if (dwc_cmd.resp_type == MSHC_RESPONSE_LEN_48 ||
		   dwc_cmd.resp_type == MSHC_RESPONSE_LEN_48B) {
		ret = mshc_get_response(config, (uint32_t *)&cmd->response, false);
	}

out:
	(void)k_mutex_unlock(&dev_data->mutex);

	return ret;
}

static int dwc_mshc_card_busy(const struct device *dev)
{
	const struct dwc_mshc_config *config = dev->config;
	uint32_t pstate;

	pstate = sys_read32(config->base + PSTATE);
	if (pstate & MSHC_PSTATE_REG_CMD_INHIBIT_Msk) {
		return true;
	}

	return false;
}

static int dwc_mshc_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct dwc_mshc_config *config = dev->config;
	struct dwc_mshc_data *dev_data = dev->data;
	uint8_t value;
	int ret = 0;

	if (ios->bus_width > 0 && (dev_data->bus_width != ios->bus_width)) {
		if (ios->bus_width == SDHC_BUS_WIDTH1BIT) {
			ret = mshc_set_host_bus_width(config,
						      MSHC_BUS_WIDTH_1_BIT);
		} else if (ios->bus_width == SDHC_BUS_WIDTH4BIT) {
			ret = mshc_set_host_bus_width(config,
						      MSHC_BUS_WIDTH_4_BIT);
		} else {
			return -ENOTSUP;
		}

		if (ret == 0) {
			dev_data->bus_width = ios->bus_width;
		}
	}

	if (ios->clock) {
		ret = mshc_card_change_clock(config, ios->clock);
		if (ret == 0) {
			dev_data->bus_clock = (uint32_t)ios->clock;
		}
	}

	if (ios->signal_voltage != dev_data->signal_voltage) {
		switch (ios->signal_voltage) {
		case SD_VOL_3_3_V:
			mshc_change_io_voltage(config, MSHC_IO_VOLT_3_3V);
			break;
		case SD_VOL_1_8_V:
			mshc_change_io_voltage(config, MSHC_IO_VOLT_1_8V);
			break;
		default:
			return -ENOTSUP;
		}
		dev_data->signal_voltage = ios->signal_voltage;
	}

	if (dev_data->power_mode != ios->power_mode) {
		value = sys_read8(config->base + PWR_CTRL);
		if (ios->power_mode == SDHC_POWER_OFF) {
			ret = mshc_disable_card_voltage(config);
			if (ret != 0) {
				return ret;
			}
			value &= ~0x1;
		} else if (ios->power_mode == SDHC_POWER_ON) {
			ret = mshc_enable_card_voltage(config);
			if (ret != 0) {
				return ret;
			}
			value |= 0x1;
		}
		sys_write8(value, config->base + PWR_CTRL);
		dev_data->power_mode = ios->power_mode;
	}

	if (ios->timing > 0) {
		value = sys_read8(config->base + HOST_CTRL1);
		switch (ios->timing) {
		case SDHC_TIMING_LEGACY:
			value &= ~MSHC_HOST_CTRL1_R_HIGH_SPEED_EN_Msk;
			break;
		case SDHC_TIMING_HS:
		case SDHC_TIMING_SDR12:
		case SDHC_TIMING_SDR25:
		case SDHC_TIMING_SDR50:
			value |= MSHC_HOST_CTRL1_R_HIGH_SPEED_EN_Msk;
			break;
		default:
			return -ENOTSUP;
		}
		sys_write8(value, config->base + HOST_CTRL1);
		dev_data->timing = ios->timing;
	}

	return ret;
}

static int dwc_mshc_get_card_present(const struct device *dev)
{
	const struct dwc_mshc_config *config = dev->config;

	return mshc_is_card_connected(config->base);
}

static int dwc_mshc_get_host_props(const struct device *dev,
				   struct sdhc_host_props *props)
{
	const struct dwc_mshc_config *config = dev->config;

	memcpy(props, &config->props, sizeof(struct sdhc_host_props));

	return 0;
}

static int dwc_mshc_reset(const struct device *dev)
{
	const struct dwc_mshc_config *config = dev->config;

	mshc_software_reset(config, MSHC_RESET_ALL);

	return 0;
}

static const struct sdhc_driver_api dwc_mshc_api = {
	.reset = dwc_mshc_reset,
	.request = dwc_mshc_request,
	.set_io = dwc_mshc_set_io,
	.get_card_present = dwc_mshc_get_card_present,
	.card_busy = dwc_mshc_card_busy,
	.get_host_props = dwc_mshc_get_host_props,
};

static int dwc_mshc_init(const struct device *dev)
{
	const struct dwc_mshc_config *config = dev->config;
	struct dwc_mshc_data *dev_data = dev->data;
	uint32_t value;
	int ret;

	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}

	if (config->irq_config_func != NULL) {
		k_event_init(&dev_data->irq_event);
		config->irq_config_func(dev);
	}

	if (config->reset.dev != NULL) {
		if (!device_is_ready(config->reset.dev)) {
			//LOG_ERR("Reset device not found");
			return -ENODEV;
		}

		ret = reset_line_toggle(config->reset.dev,
					config->reset.id);
		if (ret != 0) {
			//LOG_ERR("Reset failed");
			return ret;
		}
	}

	if (!device_is_ready(config->clock_dev)) {
		//LOG_ERR("Clock control device not found");
		return -EINVAL;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0 && ret != -EALREADY && ret != -ENOSYS) {
		//LOG_ERR("Clock enable failed");
		return ret;
	}

	k_mutex_init(&dev_data->mutex);

	mshc_software_reset(config, MSHC_RESET_ALL);

	sys_write16(0U, config->base + XFER_MODE);

	/* Select ADMA or not. */
	value = sys_read8(config->base + HOST_CTRL1);
	value &= ~MSHC_HOST_CTRL1_R_DMA_SEL_Msk;
	value |= dev_data->dma_mode;
	sys_write8(value, config->base + HOST_CTRL1);

	/* Set the data timeout to the max. */
	value = sys_read8(config->base + TOUT_CTRL);
	value &= ~MSHC_TOUT_CTRL_R_TOUT_CNT_Msk;
	value |= MSHC_MAX_TIMEOUT;
	sys_write8(value, config->base + TOUT_CTRL);

	/* Reset normal events. */
	mshc_normal_reset(config);

	/* Enable all statuses. */
	mshc_set_normal_interrupt_enable(config->base, MSHC_NORMAL_INT_MSK);
	mshc_set_error_interrupt_enable(config->base, MSHC_ERROR_INT_MSK);

	if (config->irq_config_func != NULL) {
		mshc_set_normal_interrupt_mask(config->base, MSHC_NORMAL_INT_MSK);
	}

	/* Enable Host Version 4. */
	value = sys_read16(config->base + HOST_CTRL2);
	value |= MSHC_HOST_CTRL2_R_HOST_VER4_ENABLE_Msk;
	sys_write16(value, config->base + HOST_CTRL2);

	/* Wait for the Host stable voltage. */
	k_usleep(MSHC_SUPPLY_RAMP_UP_TIME_MS); // TODO: ms vs us?

	dev_data->bus_width = SDHC_BUS_WIDTH1BIT;

	return 0;
}

#define DWC_SDHC_INTR_CONFIG(n)                                                  \
	static void mshc_##n##_irq_config_func(const struct device *dev)         \
	{                                                                        \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), mshc_isr, \
			    DEVICE_DT_INST_GET(n), 0);                           \
		irq_enable(DT_INST_IRQN(n));                                     \
	}

#define DWC_SDHC_INTR_FUNC_REG(n) .irq_config_func = mshc_##n##_irq_config_func,

#define DWC_SDHC_INTR_CONFIG_NULL
#define DWC_SDHC_INTR_FUNC_REG_NULL .irq_config_func = NULL,

#define DWC_SDHC_INTR_CONFIG_API(n)                                              \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, interrupts),                        \
		    (DWC_SDHC_INTR_CONFIG(n)), (DWC_SDHC_INTR_CONFIG_NULL))

#define DWC_SDHC_INTR_FUNC_REG_API(n)                                            \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, interrupts),                        \
		    (DWC_SDHC_INTR_FUNC_REG(n)), (DWC_SDHC_INTR_FUNC_REG_NULL))

#define DWC_MSHC_INIT(n)                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                               \
	DWC_SDHC_INTR_CONFIG_API(n)                                              \
	static const struct dwc_mshc_config dwc_mshc_##n##_config = {            \
		.base = (uintptr_t)DT_INST_REG_ADDR(n),                          \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                     \
		DWC_SDHC_INTR_FUNC_REG_API(n)                                    \
		.regulator_vmmc =                                                \
			DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(n), vmmc_supply)),  \
		.regulator_vmmcq =                                               \
			DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(n), vmmcq_supply)), \
		.reset = RESET_DT_SPEC_INST_GET(n),                              \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),              \
		.clock_subsys =                                                  \
			(clock_control_subsys_t)DT_INST_PHA(n, clocks, clkid),   \
		.props = {                                                       \
			.is_spi = false,                                         \
			.f_max = DT_INST_PROP(n, max_bus_freq),                  \
			.f_min = DT_INST_PROP(n, min_bus_freq),                  \
			.host_caps = {                                           \
				.vol_180_support = true,                         \
				.vol_300_support = false,                        \
				.vol_330_support = true,                         \
				.suspend_res_support = false,                    \
				.sdma_support = true,                            \
				.high_spd_support = true,                        \
				.adma_2_support = false,                         \
				.max_blk_len = 0,                                \
				.ddr50_support = false,                          \
				.sdr104_support = false,                         \
				.sdr50_support = false,                          \
				.bus_8_bit_support = false,                      \
			},                                                       \
		},                                                               \
	};                                                                       \
	static struct dwc_mshc_data dwc_mshc_##n##_data = {                      \
		.bus_width = SDHC_BUS_WIDTH1BIT,                                 \
		.bus_clock = 400000,                                             \
		.power_mode = SDHC_POWER_ON,                                     \
		.timing = SDHC_TIMING_LEGACY,                                    \
		.driver_type = SD_DRIVER_TYPE_B,                                 \
		.bus_mode = SDHC_BUSMODE_PUSHPULL,                               \
		.signal_voltage = SD_VOL_3_3_V,                                  \
		.dma_mode = MSHC_DMA_SDMA,                                       \
	};                                                                       \
	DEVICE_DT_INST_DEFINE(n, &dwc_mshc_init, NULL, &dwc_mshc_##n##_data,     \
			      &dwc_mshc_##n##_config, POST_KERNEL,               \
			      CONFIG_SDHC_INIT_PRIORITY, &dwc_mshc_api);

DT_INST_FOREACH_STATUS_OKAY(DWC_MSHC_INIT)
