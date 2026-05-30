/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

/*****Macro for 7Inch Display*****/
#define FB_W_7Inch 480
#define FB_H_7Inch 240
#define BYTES_PP_7Inch 2
#define FB_SIZE_7Inch (FB_W_7Inch * FB_H_7Inch * BYTES_PP_7Inch)

#define FB_ADDR 0x000000UL
#define RAM_DL 0x300000UL

#define CHIP_ID 0x0C0000UL
#define REG_ID 0x302000UL

#define REG_HCYCLE 0x30202CUL
#define REG_HOFFSET 0x302030UL
#define REG_HSIZE 0x302034UL
#define REG_HSYNC0 0x302038UL
#define REG_HSYNC1 0x30203CUL

#define REG_VCYCLE 0x302040UL
#define REG_VOFFSET 0x302044UL
#define REG_VSIZE 0x302048UL
#define REG_VSYNC0 0x30204CUL
#define REG_VSYNC1 0x302050UL

#define REG_SWIZZLE 0x302064UL
#define REG_PCLK_POL 0x30206CUL
#define REG_CSPREAD 0x302068UL
#define REG_DITHER 0x302060UL

#define REG_GPIO_DIR 0x302090UL
#define REG_GPIO 0x302094UL

#define REG_PWM_HZ 0x3020D0UL
#define REG_PWM_DUTY 0x3020D4UL

#define REG_PCLK 0x302070UL
#define REG_DLSWAP 0x302054UL

#define DLSWAP_FRAME 0x02

#define CLEAR_COLOR_RGB(r, g, b) (0x02000000UL | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (b))
#define CLEAR(c, s, t) (0x26000000UL | ((c) ? 1 : 0) | ((s) ? 2 : 0) | ((t) ? 4 : 0))
#define DISPLAY() (0x00000000UL)
#define VERTEX2II(x, y, handle, cell) (0x80000000UL | ((uint32_t)(x) << 21) | ((uint32_t)(y) << 12) | ((uint32_t)(handle) << 7) | (uint32_t)(cell))
#define BEGIN(prim) (0x1F000000UL | ((uint32_t)(prim)&0x0F))
#define END() (0x21000000UL)
#define COLOR_RGB(r, g, b) (0x04000000UL | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (b))
#define POINT_SIZE(size) ((0x0DUL << 24) | ((size)&0x1FFF))
#define VERTEX2F(x, y) ((1UL << 30) | (((x)&0x7FFF) << 15) | ((y)&0x7FFF))
#define VERTEX_FORMAT(frac) (0x27000000UL | ((frac)&0x07))
#define BITMAP_HANDLE(h) (0x05000000UL | ((uint32_t)(h)&0x1F))
#define BITMAP_SOURCE(addr) (0x01000000UL | ((uint32_t)(addr)&0x00FFFFFFUL))
#define BITMAP_LAYOUT(format, stride, height) (0x07000000UL | (((uint32_t)(format)&0x1F) << 19) | (((uint32_t)(stride)&0x3FF) << 9) | ((uint32_t)(height)&0x1FF))
#define BITMAP_LAYOUT_H(stride, height) (0x28000000UL | ((((uint32_t)(stride) >> 10) & 0x03) << 2) | (((uint32_t)(height) >> 9) & 0x03))
#define BITMAP_SIZE(filter, wrapx, wrapy, width, height) (0x08000000UL | (((uint32_t)(filter)&0x01) << 20) | (((uint32_t)(wrapx)&0x01) << 19) | (((uint32_t)(wrapy)&0x01) << 18) | (((uint32_t)(width)&0x1FF) << 9) | ((uint32_t)(height)&0x1FF))
#define BITMAP_SIZE_H(width, height) (0x29000000UL | ((((uint32_t)(width) >> 9) & 0x03) << 2) | (((uint32_t)(height) >> 9) & 0x03))

/*******************************************************************************
*                           Display Line Data Variable
*******************************************************************************/

//uint8_t display_line_data[1603];

/*******************************************************************************
*                           SPI Configuration
*******************************************************************************/
#define SPI_NODE DT_NODELABEL(spim)
const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);

struct spi_config spi_cfg = {
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .frequency = 10000000,
        .slave = 3,
    };
    
    struct spi_config spi_cfg2 = {
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .frequency = 10000000,
        .slave = 3,
    };
/*******************************************************************************
*                           Static Function Definitions
*******************************************************************************/
static uint8_t FT_Read8(uint32_t addr)
{
    uint8_t tx[5];
    uint8_t rx[5];

    tx[0] = (addr >> 16) & 0x3F;   // read
    tx[1] = (addr >> 8) & 0xFF;
    tx[2] = addr & 0xFF;
    tx[3] = 0x00;                // dummy
    tx[4] = 0x00;

    struct spi_buf tx_buf = {
        .buf = tx,
        .len = sizeof(tx),
    };

    struct spi_buf rx_buf = {
        .buf = rx,
        .len = sizeof(rx),
    };

    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1,
    };

    struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);

    return rx[4];   // FT81x returns data after dummy
}

static int FT_Write8(uint32_t addr, uint8_t val)
{
    uint8_t tx[4];

    tx[0] = ((addr >> 16) & 0x3F) | 0x80;
    tx[1] = (addr >> 8) & 0xFF;
    tx[2] = addr & 0xFF;
    tx[3] = val;

    struct spi_buf tx_buf = {
        .buf = tx,
        .len = sizeof(tx),
    };

    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1,
    };

    return spi_write(spi_dev, &spi_cfg, &tx_set);
}

static int FT_Write16(uint32_t addr, uint16_t data)
{
    FT_Write8(addr + 0, (uint8_t)(data & 0xFF));
    FT_Write8(addr + 1, (uint8_t)(data >> 8));
    return 0;
}

static int FT_Write32(uint32_t addr, uint32_t data)
{
    FT_Write8(addr + 0, (uint8_t)(data & 0xFF));
    FT_Write8(addr + 1, (uint8_t)((data >> 8) & 0xFF));
    FT_Write8(addr + 2, (uint8_t)((data >> 16) & 0xFF));
    FT_Write8(addr + 3, (uint8_t)((data >> 24) & 0xFF));
    return 0;
}

static int Display_Active()
{
    uint8_t tx[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t rx[6];

    struct spi_buf tx_buf = {
        .buf = tx,
        .len = sizeof(tx),
    };

    struct spi_buf rx_buf = {
        .buf = rx,
        .len = sizeof(rx),
    };

    struct spi_buf_set tx_set = {
        .buffers = &tx_buf,
        .count = 1,
    };

    struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    k_msleep(100);
    return 0;
}

static int Read_ChipID()
{
    uint32_t addr= CHIP_ID;
    uint32_t read_id[4];
    for(int i=0;i<4;i++)
    {
        read_id[i]= FT_Read8(addr + i);
    }
    return (read_id[0]<<24)|(read_id[1]<<16)|(read_id[2]<<8)|read_id[3];
}

static int Read_RegID()
{
    uint32_t addr= REG_ID;
    uint8_t read_id;
    read_id= FT_Read8(addr);
    return read_id;
}

static int SetTiming_800x480()
{
    // Disable pixel clock first
    FT_Write8(REG_PCLK, 0);

    // Standard 800x480 timing
    FT_Write16(REG_HSIZE, 800);
    FT_Write16(REG_VSIZE, 480);

    FT_Write16(REG_HCYCLE, 928);
    FT_Write16(REG_HOFFSET, 88);
    FT_Write16(REG_HSYNC0, 0);
    FT_Write16(REG_HSYNC1, 48);

    FT_Write16(REG_VCYCLE, 525);
    FT_Write16(REG_VOFFSET, 32);
    FT_Write16(REG_VSYNC0, 0);
    FT_Write16(REG_VSYNC1, 3);

    FT_Write8(REG_SWIZZLE, 0);
    FT_Write8(REG_PCLK_POL, 1);
    FT_Write8(REG_CSPREAD, 1);
    FT_Write8(REG_DITHER, 1);

    // Enable DISP using FT81x_GPIO7
    FT_Write8(REG_GPIO_DIR, 0x80);
    FT_Write8(REG_GPIO, 0x80);

    // Backlight PWM ON
    FT_Write16(REG_PWM_HZ, 1000);
    FT_Write8(REG_PWM_DUTY, 128);

    // Enable pixel clock
    FT_Write8(REG_PCLK, 2);

    return 0;
}

static int Bitmap_Settings()
{
    uint32_t addr;
    addr = RAM_DL;
    //CLEAR_COLOR_RGB: Set the display background color
    //Setting Black Color
    FT_Write32(addr, CLEAR_COLOR_RGB(0, 0, 0));
    //Clear Screen
    addr = addr + 4;
    FT_Write32(addr, CLEAR(1, 1, 1));
    //BEGIN_BITMAPS
    addr = addr + 4;
    FT_Write32(addr, BEGIN(1));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_HANDLE(0));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_SOURCE(FB_ADDR));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_LAYOUT(7, 1600, 480));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_LAYOUT_H(1600, 480));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_SIZE(0, 0, 0, 800, 480));
    addr = addr + 4;
    FT_Write32(addr, BITMAP_SIZE_H(800, 480));
    addr = addr + 4;
    FT_Write32(addr, VERTEX2II(0, 0, 0, 0));
    addr = addr + 4;
    FT_Write32(addr, END());
    addr = addr + 4;
    FT_Write32(addr, DISPLAY());
    // Swap display list at next frame
    FT_Write8(REG_DLSWAP, DLSWAP_FRAME);

    return 0;
}
void display_init()
{
    //Display Active
    Display_Active();

    //Read CHIP_ID to confirm communication
    int chip_id;
    chip_id=Read_ChipID();
    printk("CHIP_ID: 0x%x\n", chip_id);

    //Read Device REG_ID=0x7C to confirm display is responding
    int reg_id;
    reg_id=Read_RegID();
    printk("REG_ID: 0x%x\n", reg_id);

    //Set display timing for 7 inch 800x480
    SetTiming_800x480();
    //Set Bitmap settings for 7 inch 800x480
    Bitmap_Settings();
}

int horizontal_smpte_generator_rgb565(int width, int height, void *pattern_buff_addr)
{
    int row,col;
    uint32_t i;
    int equal_width_counter,color_index;
    unsigned short int smpte_color[8]={0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};

    unsigned char *data=(unsigned char *)pattern_buff_addr;
    color_index=0;
    equal_width_counter=0;
    i=0; // it counts data pixel number
    for(row=0;row<height;row++)
    {
        for(col=0;col<width;col++)
        {
            data[i++]=smpte_color[color_index]&0xFF;
            data[i++]=smpte_color[color_index]>>8;
        }
        equal_width_counter++;
        if(equal_width_counter==(height/8))
        {
            equal_width_counter=0;
            color_index++;
        }
    }

    return i;
}

int vertical_smpte_generator_rgb565(int width, int height, void *pattern_buff_addr)

{
    int row, col;
    uint32_t i;
    unsigned short int smpte_color[8]={0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
 
    unsigned char *data = (unsigned char *)pattern_buff_addr;
    i = 0;
    for (row = 0; row < height; row++)
    {
        for (col = 0; col < width; col++)
        {
            int color_index = col / (width / 8);
            if (color_index > 7) color_index = 7;
            data[i++] = smpte_color[color_index] & 0xFF;
            data[i++] = smpte_color[color_index] >> 8;
        }
    }
 
    return i;
}

/**
 * Generate pattern for 1 quarter of the display (400x240)
 * Generates 8-color SMPTE bars (50 pixels each) - will be replicated to fill 800x480
 */
int quarter_smpte_generator_rgb565(int width, int height, void *pattern_buff_addr)
{
    int row, col;
    uint32_t i;
    unsigned short int smpte_color[8]={0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
 
    unsigned char *data = (unsigned char *)pattern_buff_addr;
    i = 0;
    for (row = 0; row < height; row++)
    {
        for (col = 0; col < width; col++)
        {
            /* Divide 400-pixel width into 8 sections (50 pixels each) */
            int color_index = col / (width / 8);  /* 400 / 8 = 50 pixels per color */
            if (color_index > 7) color_index = 7;
            
            data[i++] = smpte_color[color_index] & 0xFF;
            data[i++] = smpte_color[color_index] >> 8;
        }
    }
 
    return i;
}

/**
 * Display full 800x480 using 1/4 memory buffer
 * Each display line is sent in 2 SPI transfers:
 *   Transfer 1: Header (3 bytes) + 400 pixels data (800 bytes)
 *   Transfer 2: Header (3 bytes) + 400 pixels data (800 bytes)
 * This repeats for all 480 display lines
 */
uint8_t line_half_buffer[800];  /* 400 pixels * 2 bytes = 800 bytes */
int display_Write_full_800x480(int quarter_width, int quarter_height, const uint8_t *quarter_pattern)
{
    uint32_t addr;
    const uint8_t *line_data;
    uint8_t header[3];
    struct spi_buf tx_buf[2];
    
    /* Send data for all 480 display lines */
    for (int y = 0; y < 480; y++)
    {
        /* Calculate which quarter row to read from (tiles vertically every 240 lines) */
        int quarter_row = y % quarter_height;
        
        /* Get pointer to this row in the quarter pattern */
        line_data = quarter_pattern + quarter_row * quarter_width * 2;
        
        /* FIRST TRANSFER: Send first 400 pixels (x=0) */
        addr = 0x800000 + (1600 * y);
        header[0] = ((addr >> 16) & 0x3F) | 0x80;
        header[1] = (addr >> 8) & 0xFF;
        header[2] = addr & 0xFF;
        
        tx_buf[0].buf = header;
        tx_buf[0].len = 3;
        tx_buf[1].buf = (uint8_t *)line_data;
        tx_buf[1].len = 800;
        
        struct spi_buf_set tx_set = {
            .buffers = tx_buf,
            .count = 2,
        };
        spi_write(spi_dev, &spi_cfg2, &tx_set);
        
        /* SECOND TRANSFER: Send second 400 pixels (x=400) */
        addr = 0x800000 + (1600 * y) + 800;
        header[0] = ((addr >> 16) & 0x3F) | 0x80;
        header[1] = (addr >> 8) & 0xFF;
        header[2] = addr & 0xFF;
        
        tx_buf[0].buf = header;
        tx_buf[0].len = 3;
        tx_buf[1].buf = (uint8_t *)line_data;
        tx_buf[1].len = 800;
        
        struct spi_buf_set tx_set2 = {
            .buffers = tx_buf,
            .count = 2,
        };
        spi_write(spi_dev, &spi_cfg2, &tx_set2);
    }
    
    return 0;
}

//uint8_t data_buff[768000 * 2];
/* Buffer for 1 quarter: 400x240 @ RGB565 (2 bytes per pixel) = 192000 bytes */
uint8_t data_buff[192000] = {1};

int main(void)
{
    int quarter_width = 400;
    int quarter_height = 240;
    int data_len;

    if (!device_is_ready(spi_dev)) {
        printk("SPI device not ready\n");
        return 0;
    }
    
    display_init();

    while (1) {
        /* Generate pattern for 1 quarter (400x240) */
        data_len = quarter_smpte_generator_rgb565(quarter_width, quarter_height, data_buff);
        
        /* Display full 800x480 by replicating quarter pattern */
        display_Write_full_800x480(quarter_width, quarter_height, data_buff);
        printk("Vertical SMPTE pattern displayed on full 800x480\n");
        k_msleep(10);
        
        /* Clear screen */
        //memset(data_buff, 0, sizeof(data_buff));
        //display_Write_full_800x480(quarter_width, quarter_height, data_buff);
        //printk("Screen Cleared\n");
        k_msleep(10);
    }

    return 0;
}
