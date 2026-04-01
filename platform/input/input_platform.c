#include "input_platform.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "input_platform";
static const uint8_t CST816S_AUTOSLEEP_REG = 0xFE;

typedef struct {
    board_profile_t board;
    input_event_cb_t cb;
    void *ctx;
    bool initialized;
    bool pointer_touched;
    uint16_t pointer_x;
    uint16_t pointer_y;
} input_context_t;

static input_context_t s_input;

#if CONFIG_IDF_TARGET_ESP32S3
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3_1_85c_hw_config.h"
#include "s3_1_85c_support.h"

typedef struct {
    bool touch_down;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t last_x;
    uint16_t last_y;
    TickType_t start_tick;
    TickType_t last_poll_tick;
    esp_lcd_touch_handle_t handle;
    bool boot_pressed;
    TickType_t boot_start_tick;
    bool boot_long_press_emitted;
} s3_1_85c_input_state_t;

static s3_1_85c_input_state_t s_input_state;

static bool is_s3_1_85c_board(const board_profile_t *board)
{
    return board != NULL && board->board_id != NULL && strcmp(board->board_id, "s3_1_85c") == 0;
}

static bool s3_1_85c_touch_read(uint16_t *x, uint16_t *y, bool *touched)
{
    esp_lcd_touch_point_data_t point = {0};
    uint8_t points = 0;

    if (x == NULL || y == NULL || touched == NULL) {
        return false;
    }

    if (s_input_state.handle == NULL) {
        return false;
    }

    if (esp_lcd_touch_read_data(s_input_state.handle) != ESP_OK) {
        return false;
    }

    if (esp_lcd_touch_get_data(s_input_state.handle, &point, &points, 1) != ESP_OK) {
        return false;
    }

    *touched = points > 0;
    if (*touched) {
        *x = point.x;
        *y = point.y;
    }
    return true;
}

static void emit_event(input_event_type_t type, uint16_t x, uint16_t y, bool has_coordinates)
{
    input_event_t event;

    s_input.pointer_touched = (type == INPUT_EVENT_PRESS);
    if (type == INPUT_EVENT_RELEASE || type == INPUT_EVENT_TAP ||
        type == INPUT_EVENT_GESTURE_SWIPE_DOWN || type == INPUT_EVENT_GESTURE_SWIPE_UP) {
        s_input.pointer_touched = false;
    }
    s_input.pointer_x = x;
    s_input.pointer_y = y;

    if (s_input.cb == NULL) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.x = (int16_t)x;
    event.y = (int16_t)y;
    event.has_coordinates = has_coordinates;
    s_input.cb(&event, s_input.ctx);
}

static void s3_1_85c_process_touch(void)
{
    uint16_t x = 0;
    uint16_t y = 0;
    bool touched = false;
    TickType_t now_tick = xTaskGetTickCount();

    if (s_input_state.handle == NULL) {
        return;
    }

    if ((now_tick - s_input_state.last_poll_tick) < pdMS_TO_TICKS(20)) {
        return;
    }
    s_input_state.last_poll_tick = now_tick;

    if (!s3_1_85c_touch_read(&x, &y, &touched)) {
        return;
    }

    if (touched && !s_input_state.touch_down) {
        s_input_state.touch_down = true;
        s_input_state.start_x = x;
        s_input_state.start_y = y;
        s_input_state.last_x = x;
        s_input_state.last_y = y;
        s_input_state.start_tick = now_tick;
        emit_event(INPUT_EVENT_PRESS, x, y, true);
    } else if (touched) {
        s_input_state.last_x = x;
        s_input_state.last_y = y;
    } else if (!touched && s_input_state.touch_down) {
        int dy = (int)s_input_state.last_y - (int)s_input_state.start_y;
        TickType_t duration = now_tick - s_input_state.start_tick;

        emit_event(INPUT_EVENT_RELEASE, s_input_state.last_x, s_input_state.last_y, true);
        if (dy > 35) {
            emit_event(INPUT_EVENT_GESTURE_SWIPE_DOWN, s_input_state.last_x, s_input_state.last_y, true);
        } else if (dy < -35) {
            emit_event(INPUT_EVENT_GESTURE_SWIPE_UP, s_input_state.last_x, s_input_state.last_y, true);
        } else if (duration <= pdMS_TO_TICKS(500)) {
            emit_event(INPUT_EVENT_TAP, s_input_state.last_x, s_input_state.last_y, true);
        }
        s_input_state.touch_down = false;
    } else {
        s_input.pointer_touched = false;
    }
}

static void s3_1_85c_process_button(void)
{
    bool pressed = gpio_get_level(S3_1_85C_BOOT_BUTTON_GPIO) == 0;
    TickType_t now_tick = xTaskGetTickCount();

    if (pressed && !s_input_state.boot_pressed) {
        s_input_state.boot_pressed = true;
        s_input_state.boot_start_tick = now_tick;
        s_input_state.boot_long_press_emitted = false;
    } else if (pressed && s_input_state.boot_pressed && !s_input_state.boot_long_press_emitted) {
        TickType_t duration = now_tick - s_input_state.boot_start_tick;
        if (duration >= pdMS_TO_TICKS(S3_1_85C_BOOT_BUTTON_HOLD_MS)) {
            s_input_state.boot_long_press_emitted = true;
            emit_event(INPUT_EVENT_BUTTON_LONG_PRESS, 0, 0, false);
        }
    } else if (!pressed && s_input_state.boot_pressed) {
        TickType_t duration = now_tick - s_input_state.boot_start_tick;
        if (!s_input_state.boot_long_press_emitted && duration >= pdMS_TO_TICKS(S3_1_85C_BUTTON_DEBOUNCE_MS)) {
            emit_event(INPUT_EVENT_BUTTON_SHORT_PRESS, 0, 0, false);
        }
        s_input_state.boot_pressed = false;
        s_input_state.boot_long_press_emitted = false;
    }
}

static bool s3_1_85c_input_init(void)
{
    gpio_config_t int_gpio = {
        .pin_bit_mask = 1ULL << S3_1_85C_TOUCH_INT_IO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t boot_gpio = {
        .pin_bit_mask = 1ULL << S3_1_85C_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .scl_speed_hz = 100000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .flags.disable_control_phase = 1,
    };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = S3_1_85C_LCD_WIDTH,
        .y_max = S3_1_85C_LCD_HEIGHT,
        .rst_gpio_num = -1,
        .int_gpio_num = S3_1_85C_TOUCH_INT_IO,
    };
    uint8_t autosleep_disable = 1;

    memset(&s_input_state, 0, sizeof(s_input_state));

    if (s3_1_85c_support_init() != ESP_OK) {
        ESP_LOGE(TAG, "syntax board support init failed");
        return false;
    }
    if (esp_lcd_new_panel_io_i2c(s3_1_85c_support_i2c_bus(), &tp_io_cfg, &tp_io) != ESP_OK) {
        ESP_LOGE(TAG, "syntax touch io init failed");
        return false;
    }
    if (esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_input_state.handle) != ESP_OK) {
        ESP_LOGE(TAG, "syntax touch driver init failed");
        return false;
    }
    if (esp_lcd_panel_io_tx_param(tp_io, CST816S_AUTOSLEEP_REG, &autosleep_disable, 1) != ESP_OK) {
        ESP_LOGW(TAG, "syntax touch autosleep disable failed");
    }
    if (gpio_config(&int_gpio) != ESP_OK) {
        ESP_LOGE(TAG, "syntax touch int gpio failed");
        return false;
    }
    if (gpio_config(&boot_gpio) != ESP_OK) {
        ESP_LOGE(TAG, "syntax boot button gpio failed");
        return false;
    }

    return true;
}
#endif

bool input_platform_init(const board_profile_t *board)
{
    if (board == 0) {
        return false;
    }

    memset(&s_input, 0, sizeof(s_input));
    s_input.board = *board;
    s_input.initialized = true;

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(board)) {
        return s3_1_85c_input_init();
    }
#endif

    return true;
}

void input_platform_register_callback(input_event_cb_t cb, void *ctx)
{
    s_input.cb = cb;
    s_input.ctx = ctx;
}

bool input_platform_get_pointer_state(bool *touched, uint16_t *x, uint16_t *y)
{
    if (touched == NULL || x == NULL || y == NULL) {
        return false;
    }

    *touched = s_input.pointer_touched;
    *x = s_input.pointer_x;
    *y = s_input.pointer_y;
    return true;
}

void input_platform_process(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_input.board)) {
        s3_1_85c_process_touch();
        s3_1_85c_process_button();
    }
#endif
}
