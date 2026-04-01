#include "security_auth.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"

#include "storage_platform.h"

#define SECURITY_NAMESPACE "security"
#define KEY_DEV_PIN "dev_pin"
#define SESSION_TIMEOUT_MS (30ULL * 60ULL * 1000ULL)
#define PIN_ROTATION_MS    (30ULL * 60ULL * 1000ULL)

static bool s_session_active;
static bool s_initialized;
static uint64_t s_last_activity_ms;
static uint64_t s_pin_generation_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static bool ensure_storage_ready(void)
{
    return storage_platform_init();
}

static void expire_session_if_needed(void)
{
    char rotated_pin[7];
    uint64_t now = now_ms();

    if (!s_initialized) {
        return;
    }

    // Check if PIN itself has expired (30 minute rotation)
    if (now - s_pin_generation_ms > PIN_ROTATION_MS) {
        security_auth_rotate_pin(rotated_pin, sizeof(rotated_pin));
        return;
    }

    if (!s_session_active) {
        return;
    }
    if (now - s_last_activity_ms <= SESSION_TIMEOUT_MS) {
        return;
    }

    security_auth_rotate_pin(rotated_pin, sizeof(rotated_pin));
}

bool security_auth_init(void)
{
    char pin[7];

    if (s_initialized) {
        return true;
    }
    if (!ensure_storage_ready()) {
        return false;
    }
    
    // Always rotate PIN once on boot as requested
    if (security_auth_rotate_pin(pin, sizeof(pin))) {
        s_initialized = true;
        return true;
    }
    return false;
}

bool security_auth_get_pin(char *buf, size_t buf_len)
{
    if (!security_auth_init()) {
        return false;
    }
    expire_session_if_needed();
    if (buf == 0 || buf_len < 7) {
        return false;
    }
    if (storage_platform_get_str(SECURITY_NAMESPACE, KEY_DEV_PIN, buf, buf_len)) {
        return true;
    }
    return security_auth_rotate_pin(buf, buf_len);
}

bool security_auth_rotate_pin(char *buf, size_t buf_len)
{
    uint32_t pin_value;
    char pin[7];

    if (buf == 0 || buf_len < 7 || !ensure_storage_ready()) {
        if (buf != 0 && buf_len > 0) {
            buf[0] = '\0';
        }
        return false;
    }

    pin_value = esp_random() % 1000000U;
    snprintf(pin, sizeof(pin), "%06" PRIu32, pin_value);
    if (!storage_platform_set_str(SECURITY_NAMESPACE, KEY_DEV_PIN, pin)) {
        if (buf != 0 && buf_len > 0) buf[0] = '\0';
        return false;
    }

    memcpy(buf, pin, sizeof(pin));
    s_session_active = false;
    s_last_activity_ms = 0;
    s_pin_generation_ms = now_ms();
    return true;
}

bool security_auth_authorize_bearer(const char *bearer_value)
{
    char pin[7];

    if (!security_auth_init()) {
        return false;
    }
    expire_session_if_needed();
    if (bearer_value == 0 || strncmp(bearer_value, "Bearer ", 7) != 0) {
        return false;
    }
    if (!security_auth_get_pin(pin, sizeof(pin))) {
        return false;
    }
    if (strcmp(bearer_value + 7, pin) != 0) {
        return false;
    }

    s_session_active = true;
    s_last_activity_ms = now_ms();
    return true;
}

void security_auth_mark_activity(void)
{
    expire_session_if_needed();
    if (s_session_active) {
        s_last_activity_ms = now_ms();
    }
}

bool security_auth_session_active(void)
{
    expire_session_if_needed();
    return s_session_active;
}
