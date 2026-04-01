#include "s3_1_85c_support.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "s3_1_85c_hw_config.h"

static const char *TAG = "s3_1_85c_support";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_tca9554;
static bool s_initialized;

static esp_err_t s3_1_85c_tca_write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_tca9554, buf, sizeof(buf), -1);
}

static esp_err_t s3_1_85c_reset_lines(void)
{
    uint8_t exio = 0xFF;

    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_CONFIG_REG, 0x00), TAG, "exio config failed");
    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_OUTPUT_REG, exio), TAG, "exio init failed");

    exio &= (uint8_t)~S3_1_85C_EXIO_LCD_RST_BIT;
    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_OUTPUT_REG, exio), TAG, "lcd reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    exio |= S3_1_85C_EXIO_LCD_RST_BIT;
    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_OUTPUT_REG, exio), TAG, "lcd reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    exio &= (uint8_t)~S3_1_85C_EXIO_TOUCH_RST_BIT;
    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_OUTPUT_REG, exio), TAG, "touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    exio |= S3_1_85C_EXIO_TOUCH_RST_BIT;
    ESP_RETURN_ON_ERROR(s3_1_85c_tca_write_reg(S3_1_85C_EXIO_OUTPUT_REG, exio), TAG, "touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

esp_err_t s3_1_85c_support_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = S3_1_85C_TOUCH_I2C_PORT,
        .sda_io_num = S3_1_85C_TOUCH_I2C_SDA,
        .scl_io_num = S3_1_85C_TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const i2c_device_config_t tca_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = S3_1_85C_EXIO_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus init failed");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &tca_cfg, &s_tca9554), TAG, "tca9554 add failed");
    ESP_RETURN_ON_ERROR(s3_1_85c_reset_lines(), TAG, "reset lines failed");

    s_initialized = true;
    return ESP_OK;
}

i2c_master_bus_handle_t s3_1_85c_support_i2c_bus(void)
{
    return s_i2c_bus;
}
