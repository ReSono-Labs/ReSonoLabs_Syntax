#include "orb_service.h"

#include <string.h>

#include "storage_platform.h"

#define ORB_NAMESPACE "orb"
#define KEY_TUNE "tune_v2"
#define COLOR_RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

static orb_config_t s_config;
static bool s_initialized;

static void copy_params(float *dst, const float *src)
{
    memcpy(dst, src, sizeof(float) * ORB_PARAM_COUNT);
}

static void set_state_defaults(orb_state_config_t *state,
                               float speed,
                               uint32_t primary,
                               uint32_t secondary,
                               uint32_t tertiary,
                               float intensity,
                               const float *params)
{
    state->speed = speed;
    state->primary = primary;
    state->secondary = secondary;
    state->tertiary = tertiary;
    state->intensity = intensity;
    copy_params(state->params, params);
}

static void load_default_config(orb_config_t *config)
{
    static const float idle_params[ORB_PARAM_COUNT] = {
        1.0f, -0.6f, 1.4f, 2.2f, 1.8f, 1.5f, 0.058f, 0.18f, 0.10f, 1.0f, 0.8f, 1.0f
    };
    static const float listening_params[ORB_PARAM_COUNT] = {
        1.0f, 0.77f, 1.27f, 0.55f, 0.45f, 0.35f, 0.062f, 0.22f, 0.10f, 2.0f, 0.9f, 1.0f
    };
    static const float thinking_params[ORB_PARAM_COUNT] = {
        0.22f, -2.8f, 3.6f, 0.08f, 0.12f, 0.06f, 0.055f, 0.35f, 0.08f, 3.0f, 0.7f, 1.0f
    };
    static const float talking_params[ORB_PARAM_COUNT] = {
        1.0f, 0.77f, 1.2f, 2.8f, 2.4f, 2.0f, 0.065f, 0.25f, 0.12f, 1.5f, 1.0f, 1.0f
    };
    static const float sleep_params[ORB_PARAM_COUNT] = {
        0.8f, -0.5f, 1.1f, 1.8f, 1.4f, 1.0f, 0.040f, 0.12f, 0.05f, 0.6f, 0.4f, 0.8f
    };

    memset(config, 0, sizeof(*config));
    config->theme_id = 0;
    config->global_speed = 1.0f;

    set_state_defaults(&config->states[ORB_STATE_IDLE],
                       0.030f, COLOR_RGB(0, 196, 180), COLOR_RGB(74, 111, 255), COLOR_RGB(59, 59, 138), 0.10f, idle_params);
    set_state_defaults(&config->states[ORB_STATE_CONNECTING],
                       0.040f, COLOR_RGB(0, 180, 220), COLOR_RGB(0, 130, 255), COLOR_RGB(20, 45, 110), 0.40f, thinking_params);
    set_state_defaults(&config->states[ORB_STATE_LISTENING],
                       0.065f, COLOR_RGB(0, 240, 255), COLOR_RGB(0, 170, 255), COLOR_RGB(0, 102, 255), 0.50f, listening_params);
    set_state_defaults(&config->states[ORB_STATE_THINKING],
                       0.045f, COLOR_RGB(102, 51, 204), COLOR_RGB(170, 119, 255), COLOR_RGB(204, 170, 255), 0.80f, thinking_params);
    set_state_defaults(&config->states[ORB_STATE_TALKING],
                       0.050f, COLOR_RGB(0, 255, 212), COLOR_RGB(0, 196, 180), COLOR_RGB(0, 158, 142), 1.00f, talking_params);
    set_state_defaults(&config->states[ORB_STATE_ERROR],
                       0.035f, COLOR_RGB(255, 80, 80), COLOR_RGB(180, 25, 25), COLOR_RGB(70, 0, 0), 0.95f, thinking_params);
    set_state_defaults(&config->states[ORB_STATE_SLEEP],
                       0.020f, COLOR_RGB(25, 70, 90), COLOR_RGB(40, 100, 120), COLOR_RGB(8, 22, 28), 0.02f, sleep_params);
    set_state_defaults(&config->states[ORB_STATE_LOCKED],
                       0.025f, COLOR_RGB(90, 140, 255), COLOR_RGB(70, 95, 180), COLOR_RGB(22, 28, 56), 0.08f, idle_params);
}

bool orb_service_init(void)
{
    size_t blob_len = sizeof(s_config);

    if (s_initialized) {
        return true;
    }
    if (!storage_platform_init()) {
        return false;
    }

    load_default_config(&s_config);
    if (!storage_platform_get_blob(ORB_NAMESPACE, KEY_TUNE, &s_config, &blob_len) ||
        blob_len != sizeof(s_config)) {
        load_default_config(&s_config);
        storage_platform_set_blob(ORB_NAMESPACE, KEY_TUNE, &s_config, sizeof(s_config));
    }

    s_initialized = true;
    return true;
}

bool orb_service_get_config(orb_config_t *out_config)
{
    if (out_config == 0 || !orb_service_init()) {
        return false;
    }

    *out_config = s_config;
    return true;
}

bool orb_service_set_config(const orb_config_t *config)
{
    if (config == 0 || !orb_service_init()) {
        return false;
    }

    s_config = *config;
    return storage_platform_set_blob(ORB_NAMESPACE, KEY_TUNE, &s_config, sizeof(s_config));
}
