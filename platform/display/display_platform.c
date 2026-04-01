#include "display_platform.h"

#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "display_platform";

static display_info_t s_info;
static board_profile_t s_board;
static bool s_initialized;

#if CONFIG_IDF_TARGET_ESP32S3
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3_1_85c_hw_config.h"
#include "s3_1_85c_support.h"

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static bool s_backlight_ready;
static uint8_t s_backlight_percent = S3_1_85C_BACKLIGHT_DEFAULT_PCT;
static const st77916_lcd_init_cmd_t s_s3_1_85c_vendor_cmds[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0}, {0xF2, (uint8_t[]){0x28}, 1, 0}, {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0}, {0x83, (uint8_t[]){0xE0}, 1, 0}, {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0}, {0xF0, (uint8_t[]){0x00}, 1, 0}, {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0}, {0xB0, (uint8_t[]){0x56}, 1, 0}, {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0}, {0xB4, (uint8_t[]){0x87}, 1, 0}, {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0}, {0xB7, (uint8_t[]){0x40}, 1, 0}, {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0}, {0xBB, (uint8_t[]){0x08}, 1, 0}, {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0}, {0xC0, (uint8_t[]){0x80}, 1, 0}, {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0}, {0xC3, (uint8_t[]){0x80}, 1, 0}, {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0}, {0xC6, (uint8_t[]){0xA9}, 1, 0}, {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0}, {0xC9, (uint8_t[]){0xA9}, 1, 0}, {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0}, {0xD0, (uint8_t[]){0x91}, 1, 0}, {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0}, {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0}, {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0}, {0xF1, (uint8_t[]){0x10}, 1, 0}, {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0}, {0xF3, (uint8_t[]){0x10}, 1, 0}, {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0}, {0xE2, (uint8_t[]){0x00}, 1, 0}, {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0}, {0xE5, (uint8_t[]){0x06}, 1, 0}, {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0}, {0xE8, (uint8_t[]){0x05}, 1, 0}, {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0}, {0xEB, (uint8_t[]){0x00}, 1, 0}, {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0}, {0xEE, (uint8_t[]){0x00}, 1, 0}, {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0}, {0xF9, (uint8_t[]){0x00}, 1, 0}, {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0}, {0xFC, (uint8_t[]){0x00}, 1, 0}, {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xFF, (uint8_t[]){0x00}, 1, 0}, {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0}, {0x62, (uint8_t[]){0x00}, 1, 0}, {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0}, {0x65, (uint8_t[]){0x00}, 1, 0}, {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0}, {0x68, (uint8_t[]){0x00}, 1, 0}, {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0}, {0x6B, (uint8_t[]){0x00}, 1, 0}, {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0}, {0x72, (uint8_t[]){0x00}, 1, 0}, {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0}, {0x75, (uint8_t[]){0x00}, 1, 0}, {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0}, {0x78, (uint8_t[]){0x00}, 1, 0}, {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0}, {0x7B, (uint8_t[]){0x00}, 1, 0}, {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0}, {0x82, (uint8_t[]){0x06}, 1, 0}, {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0}, {0x85, (uint8_t[]){0x04}, 1, 0}, {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0}, {0x88, (uint8_t[]){0x48}, 1, 0}, {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0}, {0x8B, (uint8_t[]){0x02}, 1, 0}, {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0}, {0x8E, (uint8_t[]){0x00}, 1, 0}, {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0}, {0x91, (uint8_t[]){0x00}, 1, 0}, {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0}, {0x94, (uint8_t[]){0xDA}, 1, 0}, {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0}, {0x97, (uint8_t[]){0x00}, 1, 0}, {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0}, {0x9A, (uint8_t[]){0x0C}, 1, 0}, {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0}, {0x9D, (uint8_t[]){0x04}, 1, 0}, {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0}, {0xA0, (uint8_t[]){0x48}, 1, 0}, {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0}, {0xA3, (uint8_t[]){0x02}, 1, 0}, {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0}, {0xA6, (uint8_t[]){0x00}, 1, 0}, {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0}, {0xA9, (uint8_t[]){0x00}, 1, 0}, {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0}, {0xAC, (uint8_t[]){0xD7}, 1, 0}, {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0}, {0xAF, (uint8_t[]){0x00}, 1, 0}, {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0}, {0xB2, (uint8_t[]){0x09}, 1, 0}, {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0}, {0xB5, (uint8_t[]){0x04}, 1, 0}, {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0}, {0xB8, (uint8_t[]){0x48}, 1, 0}, {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0}, {0xBB, (uint8_t[]){0x02}, 1, 0}, {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0}, {0xBE, (uint8_t[]){0x00}, 1, 0}, {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0}, {0xC1, (uint8_t[]){0x47}, 1, 0}, {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0}, {0xC4, (uint8_t[]){0x74}, 1, 0}, {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0}, {0xC7, (uint8_t[]){0x01}, 1, 0}, {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0}, {0xD0, (uint8_t[]){0x10}, 1, 0}, {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0}, {0xD3, (uint8_t[]){0x65}, 1, 0}, {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0}, {0xD6, (uint8_t[]){0x99}, 1, 0}, {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0}, {0xD9, (uint8_t[]){0xAA}, 1, 0}, {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0}, {0x21, (uint8_t[]){0x00}, 1, 0}, {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 0},
};

static ledc_mode_t s3_1_85c_backlight_mode(void)
{
    return (ledc_mode_t)S3_1_85C_BACKLIGHT_MODE_ID;
}

static ledc_timer_t s3_1_85c_backlight_timer(void)
{
    return (ledc_timer_t)S3_1_85C_BACKLIGHT_TIMER_ID;
}

static ledc_channel_t s3_1_85c_backlight_channel(void)
{
    return (ledc_channel_t)S3_1_85C_BACKLIGHT_CHANNEL_ID;
}

static bool is_s3_1_85c_board(const board_profile_t *board)
{
    return board != NULL && board->board_id != NULL && strcmp(board->board_id, "s3_1_85c") == 0;
}

static esp_err_t s3_1_85c_backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = s3_1_85c_backlight_mode(),
        .duty_resolution = (ledc_timer_bit_t)S3_1_85C_BACKLIGHT_RESOLUTION,
        .timer_num = s3_1_85c_backlight_timer(),
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = S3_1_85C_LCD_IO_BL,
        .speed_mode = s3_1_85c_backlight_mode(),
        .channel = s3_1_85c_backlight_channel(),
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = s3_1_85c_backlight_timer(),
        .duty = 0,
        .hpoint = 0,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel failed");
    s_backlight_ready = true;
    return ESP_OK;
}

static esp_err_t s3_1_85c_backlight_set(uint8_t percent)
{
    uint32_t max_duty = (1U << S3_1_85C_BACKLIGHT_RESOLUTION) - 1U;
    uint32_t duty = ((uint32_t)percent * max_duty) / 100U;

    if (!s_backlight_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ledc_set_duty(s3_1_85c_backlight_mode(), s3_1_85c_backlight_channel(), duty), TAG, "backlight duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(s3_1_85c_backlight_mode(), s3_1_85c_backlight_channel()), TAG, "backlight update failed");
    s_backlight_percent = percent;
    return ESP_OK;
}

static esp_err_t s3_1_85c_panel_init(void)
{
    static bool spi_initialized;
    spi_bus_config_t bus_config = {
        .data0_io_num = S3_1_85C_LCD_IO_DATA0,
        .data1_io_num = S3_1_85C_LCD_IO_DATA1,
        .sclk_io_num = S3_1_85C_LCD_IO_SCK,
        .data2_io_num = S3_1_85C_LCD_IO_DATA2,
        .data3_io_num = S3_1_85C_LCD_IO_DATA3,
        .max_transfer_sz = S3_1_85C_LCD_SPI_MAX_TRANSFER,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = S3_1_85C_LCD_IO_CS,
        .dc_gpio_num = -1,
        .spi_mode = S3_1_85C_LCD_SPI_MODE,
        .pclk_hz = S3_1_85C_LCD_SPI_READ_PCLK_HZ,
        .trans_queue_depth = S3_1_85C_LCD_SPI_QUEUE_DEPTH,
        .lcd_cmd_bits = S3_1_85C_LCD_SPI_CMD_BITS,
        .lcd_param_bits = S3_1_85C_LCD_SPI_PARAM_BITS,
        .flags = {
            .quad_mode = 1,
        },
    };
    st77916_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = S3_1_85C_LCD_COLOR_BITS,
        .vendor_config = &vendor_config,
    };
    esp_err_t err;

    if (!spi_initialized) {
        err = spi_bus_initialize((spi_host_device_t)S3_1_85C_LCD_SPI_HOST_ID, &bus_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        spi_initialized = true;
    }

    if (s_panel_io == NULL) {
        uint8_t reg_data[4] = {0};
        uint32_t read_cmd = ((uint32_t)S3_1_85C_LCD_READ_OPCODE << 24) | ((uint32_t)0x04 << 8);
        bool use_vendor_cmds = false;
        esp_err_t read_err;

        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)S3_1_85C_LCD_SPI_HOST_ID,
                                                     &io_config,
                                                     &s_panel_io),
                            TAG,
                            "panel io probe failed");

        read_err = esp_lcd_panel_io_rx_param(s_panel_io, read_cmd, reg_data, sizeof(reg_data));
        if (read_err == ESP_OK) {
            ESP_LOGI(TAG, "ST77916 reg0x04: %02X %02X %02X %02X", reg_data[0], reg_data[1], reg_data[2], reg_data[3]);
            use_vendor_cmds = (reg_data[0] == 0x00 && reg_data[1] == 0x02 && reg_data[2] == 0x7F && reg_data[3] == 0x7F);
        } else {
            ESP_LOGW(TAG, "ST77916 reg0x04 read failed, using vendor init fallback");
            use_vendor_cmds = true;
        }
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;

        io_config.pclk_hz = S3_1_85C_LCD_SPI_PCLK_HZ;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)S3_1_85C_LCD_SPI_HOST_ID,
                                                     &io_config,
                                                     &s_panel_io),
                            TAG,
                            "panel io failed");
        if (use_vendor_cmds) {
            vendor_config.init_cmds = s_s3_1_85c_vendor_cmds;
            vendor_config.init_cmds_size = sizeof(s_s3_1_85c_vendor_cmds) / sizeof(s_s3_1_85c_vendor_cmds[0]);
        }
    }

    if (s_panel == NULL) {
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_panel_io, &panel_config, &s_panel), TAG, "new panel failed");
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel on failed");
    return ESP_OK;
}

static esp_err_t s3_1_85c_panel_clear_black(void)
{
    static uint16_t black_row[S3_1_85C_LCD_WIDTH];

    for (uint16_t y = 0; y < S3_1_85C_LCD_HEIGHT; ++y) {
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel,
                                                  0,
                                                  y,
                                                  S3_1_85C_LCD_WIDTH,
                                                  y + 1,
                                                  black_row);
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

static bool s3_1_85c_display_init(void)
{
    if (s3_1_85c_support_init() != ESP_OK) {
        ESP_LOGE(TAG, "syntax board support init failed");
        return false;
    }
    if (s3_1_85c_backlight_init() != ESP_OK) {
        ESP_LOGE(TAG, "syntax backlight init failed");
        return false;
    }
    if (s3_1_85c_panel_init() != ESP_OK) {
        ESP_LOGE(TAG, "syntax panel init failed");
        return false;
    }
    if (s3_1_85c_panel_clear_black() != ESP_OK) {
        ESP_LOGW(TAG, "syntax panel clear failed");
    }
    if (s3_1_85c_backlight_set(s_backlight_percent) != ESP_OK) {
        ESP_LOGW(TAG, "syntax backlight set failed");
    }
    return true;
}
#endif

bool display_platform_init(const board_profile_t *board)
{
    if (board == 0) {
        return false;
    }

    memset(&s_board, 0, sizeof(s_board));
    s_board = *board;
    s_info.width = board->layout.screen_width;
    s_info.height = board->layout.screen_height;
    s_info.tech = board->display_tech;
    s_info.shape = board->layout.display_shape;
    s_initialized = true;

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(board)) {
        return s3_1_85c_display_init();
    }
#endif

    return true;
}

bool display_platform_sleep(void)
{
    if (!s_initialized) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        if (s_panel != NULL) {
            esp_lcd_panel_disp_on_off(s_panel, false);
        }
        if (s_backlight_ready) {
            s3_1_85c_backlight_set(0);
        }
    }
#endif

    return true;
}

bool display_platform_wake(void)
{
    if (!s_initialized) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        if (s_panel != NULL) {
            esp_lcd_panel_disp_on_off(s_panel, true);
        }
        if (s_backlight_ready) {
            s3_1_85c_backlight_set(s_backlight_percent);
        }
    }
#endif

    return true;
}

display_info_t display_platform_get_info(void)
{
    return s_info;
}

bool display_platform_flush_rect(int x1, int y1, int x2, int y2, const void *color_data)
{
    if (!s_initialized || color_data == NULL) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        if (s_panel == NULL) {
            return false;
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, color_data) != ESP_OK) {
            return false;
        }
        return true;
    }
#endif

    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    return true;
}
