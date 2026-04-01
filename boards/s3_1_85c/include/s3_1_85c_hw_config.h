#ifndef S3_1_85C_HW_CONFIG_H
#define S3_1_85C_HW_CONFIG_H

#include <stdint.h>

/*
 * Deskbot round-display board hardware facts.
 *
 * Sources aligned:
 * - ambitious/desk_bot/main/user/config.h
 * - ESP32-S3-Touch-LCD-1.85C example project
 *
 * This header is board-only. Shared core and services must not include it.
 */

#define S3_1_85C_LCD_WIDTH            360U
#define S3_1_85C_LCD_HEIGHT           360U
#define S3_1_85C_LCD_COLOR_BITS       16U
#define S3_1_85C_LCD_SPI_HOST_ID      2
#define S3_1_85C_LCD_SPI_MODE         0U
#define S3_1_85C_LCD_SPI_PCLK_HZ      (80U * 1000U * 1000U)
#define S3_1_85C_LCD_SPI_READ_PCLK_HZ (3U * 1000U * 1000U)
#define S3_1_85C_LCD_SPI_QUEUE_DEPTH  1U
#define S3_1_85C_LCD_SPI_CMD_BITS     32U
#define S3_1_85C_LCD_SPI_PARAM_BITS   8U
#define S3_1_85C_LCD_SPI_MAX_TRANSFER (S3_1_85C_LCD_WIDTH * 40U * sizeof(uint16_t))
#define S3_1_85C_LCD_READ_OPCODE      0x0BU

#define S3_1_85C_LCD_IO_DATA0         46
#define S3_1_85C_LCD_IO_DATA1         45
#define S3_1_85C_LCD_IO_DATA2         42
#define S3_1_85C_LCD_IO_DATA3         41
#define S3_1_85C_LCD_IO_SCK           40
#define S3_1_85C_LCD_IO_CS            21
#define S3_1_85C_LCD_IO_TE            18
#define S3_1_85C_LCD_IO_BL            5

#define S3_1_85C_TOUCH_I2C_PORT       0
#define S3_1_85C_TOUCH_I2C_SDA        11
#define S3_1_85C_TOUCH_I2C_SCL        10
#define S3_1_85C_TOUCH_INT_IO         4
#define S3_1_85C_TOUCH_I2C_ADDR       0x15
#define S3_1_85C_TOUCH_I2C_FREQ_HZ    400000U

#define S3_1_85C_EXIO_ADDR            0x20
#define S3_1_85C_EXIO_OUTPUT_REG      0x01
#define S3_1_85C_EXIO_CONFIG_REG      0x03
#define S3_1_85C_EXIO_TOUCH_RST_BIT   (1U << 0)
#define S3_1_85C_EXIO_LCD_RST_BIT     (1U << 1)
#define S3_1_85C_EXIO_SD_CS_BIT       (1U << 2)

#define S3_1_85C_AUDIO_MIC_SD         39
#define S3_1_85C_AUDIO_MIC_WS         2
#define S3_1_85C_AUDIO_MIC_SCK        15
#define S3_1_85C_AUDIO_SPK_SD         47
#define S3_1_85C_AUDIO_SPK_WS         38
#define S3_1_85C_AUDIO_SPK_SCK        48

#define S3_1_85C_AUDIO_IN_RATE        16000U
#define S3_1_85C_AUDIO_OUT_RATE       24000U

#define S3_1_85C_BOOT_BUTTON_GPIO     0
#define S3_1_85C_BOOT_BUTTON_HOLD_SECONDS 5U
#define S3_1_85C_BOOT_BUTTON_HOLD_MS  (S3_1_85C_BOOT_BUTTON_HOLD_SECONDS * 1000U)
#define S3_1_85C_BUTTON_DEBOUNCE_MS   50U

#define S3_1_85C_BACKLIGHT_TIMER_ID      0
#define S3_1_85C_BACKLIGHT_MODE_ID       0
#define S3_1_85C_BACKLIGHT_CHANNEL_ID    0
#define S3_1_85C_BACKLIGHT_RESOLUTION    13U
#define S3_1_85C_BACKLIGHT_DEFAULT_PCT   55U

#endif
