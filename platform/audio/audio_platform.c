#include "audio_platform.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
/* driver/gpio.h moved inside S3 block */

static const char *TAG = "audio_platform";
/* Match donor semantics: slider stays 0-100, but full-scale playback is capped
 * below raw PCM 100% so the top end remains usable instead of painfully loud. */
#define SPEAKER_VOLUME_CEILING 43U

static uint8_t s_volume = 50;
static board_profile_t s_board;
static bool s_initialized;

#if CONFIG_IDF_TARGET_ESP32S3
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "s3_1_85c_hw_config.h"
#include "s3_1_85c_support.h"


typedef struct {
    i2s_chan_handle_t rx;
    i2s_chan_handle_t tx;
    audio_mic_cb_t mic_cb;
    void *mic_ctx;
    TaskHandle_t mic_task;
    TaskHandle_t spk_task;
    bool mic_enabled;
    bool channels_ready;
    uint32_t mic_reads;
    size_t mic_bytes;
    int64_t mic_log_ms;

    bool spk_finish_pending;
} s3_1_85c_audio_state_t;

static s3_1_85c_audio_state_t s_audio;

/* Speaker playback jitter buffer — 32 KB = ~682 ms at 24 kHz/16-bit mono.
 * Decouples network frame delivery timing from the I2S DMA clock so WiFi
 * jitter gaps (observed up to ~150 ms) do not cause audible stutters.
 * 300ms Pre-roll is ~14.4 KB. */
/* Speaker playback jitter buffer — 64 KB = ~1.3 seconds at 24 kHz/16-bit mono.
 * 400ms Pre-roll is ~19.2 KB (9,600 samples). */
#define SPK_RING_SIZE 65536U
static uint8_t      s_spk_ring[SPK_RING_SIZE];
static size_t       s_spk_ring_rd;
static size_t       s_spk_ring_wr;
static bool         s_spk_pre_rolled;
static portMUX_TYPE s_spk_ring_lock = portMUX_INITIALIZER_UNLOCKED;

static size_t spk_ring_available(void)
{
    size_t wr, rd;
    taskENTER_CRITICAL(&s_spk_ring_lock);
    wr = s_spk_ring_wr;
    rd = s_spk_ring_rd;
    taskEXIT_CRITICAL(&s_spk_ring_lock);
    return (wr - rd + SPK_RING_SIZE) % SPK_RING_SIZE;
}

static size_t spk_ring_push(const uint8_t *src, size_t len)
{
    size_t wr, rd, used, free_space, to_end;
    taskENTER_CRITICAL(&s_spk_ring_lock);
    wr = s_spk_ring_wr;
    rd = s_spk_ring_rd;
    taskEXIT_CRITICAL(&s_spk_ring_lock);
    used = (wr - rd + SPK_RING_SIZE) % SPK_RING_SIZE;
    free_space = SPK_RING_SIZE - 1U - used;
    if (len > free_space) {
        len = free_space;
    }
    if (len == 0) {
        return 0;
    }
    to_end = SPK_RING_SIZE - wr;
    if (len <= to_end) {
        memcpy(&s_spk_ring[wr], src, len);
    } else {
        memcpy(&s_spk_ring[wr], src, to_end);
        memcpy(&s_spk_ring[0], src + to_end, len - to_end);
    }
    taskENTER_CRITICAL(&s_spk_ring_lock);
    s_spk_ring_wr = (wr + len) % SPK_RING_SIZE;
    taskEXIT_CRITICAL(&s_spk_ring_lock);
    return len;
}

static size_t spk_ring_pop(uint8_t *dst, size_t len)
{
    size_t wr, rd, avail, to_end;
    taskENTER_CRITICAL(&s_spk_ring_lock);
    wr = s_spk_ring_wr;
    rd = s_spk_ring_rd;
    taskEXIT_CRITICAL(&s_spk_ring_lock);
    avail = (wr - rd + SPK_RING_SIZE) % SPK_RING_SIZE;
    if (len > avail) {
        len = avail;
    }
    if (len == 0) {
        return 0;
    }
    to_end = SPK_RING_SIZE - rd;
    if (len <= to_end) {
        memcpy(dst, &s_spk_ring[rd], len);
    } else {
        memcpy(dst, &s_spk_ring[rd], to_end);
        memcpy(dst + to_end, &s_spk_ring[0], len - to_end);
    }
    taskENTER_CRITICAL(&s_spk_ring_lock);
    s_spk_ring_rd = (rd + len) % SPK_RING_SIZE;
    taskEXIT_CRITICAL(&s_spk_ring_lock);
    return len;
}

static void s3_1_85c_spk_task(void *arg)
{
    (void)arg;
    int16_t mono_chunk[480];
    int16_t stereo_chunk[960]; // Duplicated for I2S Stereo slot
    const size_t mono_samples = 480U;
    const size_t mono_bytes   = mono_samples * sizeof(int16_t);

    while (true) {
        size_t avail = spk_ring_available();
        size_t got = 0;
        bool force_drain = s_audio.spk_finish_pending;

        // Startup pre-roll: Wait for enough data to buffer
        if (!s_spk_pre_rolled && !force_drain) {
            if (avail < 19200) { // 400ms
                vTaskDelay(pdMS_TO_TICKS(10));
                continue; 
            }
            s_spk_pre_rolled = true;
            ESP_LOGI(TAG, "speaker pre-roll complete, starting playback");
        }

        // Align pop to 2-byte samples.
        size_t to_pop = mono_bytes;
        if (avail < to_pop) {
            to_pop = (avail / 2U) * 2U; // Sample-aligned
        }

        if (to_pop > 0) {
            got = spk_ring_pop((uint8_t *)mono_chunk, to_pop);
        }

        if (got < mono_bytes) {
            memset((uint8_t *)((uint8_t *)mono_chunk + got), 0, mono_bytes - got);
            // If we ran dry, don't reset pre-rolled, just play silence this chunk.
            // Only 'finish' or 'clear' resets the pre-roll state officially.
            if (got == 0 && !force_drain) {
                // If it stays empty for too long, state machine will handle it.
            }
        }

        /* Volume scaling + Mono to Stereo duplication. */
        uint32_t vol = (uint32_t)s_volume;
        for (size_t i = 0; i < mono_samples; i++) {
            int32_t gain = (int32_t)((vol * SPEAKER_VOLUME_CEILING) / 100);
            int32_t s    = (int32_t)((int32_t)mono_chunk[i] * gain) / 100;
            if (s > 32767)  s = 32767;
            if (s < -32768) s = -32768;
            
            // Duplicate mono sample to both stereo channels (16-bit each)
            stereo_chunk[i * 2]     = (int16_t)s;
            stereo_chunk[i * 2 + 1] = (int16_t)s;
        }

        size_t written = 0;
        // Continuous write keeps the I2S clocks and DAC biased, preventing 'skipping'/'tapping'
        i2s_channel_write(s_audio.tx, stereo_chunk, sizeof(stereo_chunk), &written, pdMS_TO_TICKS(500));
        
        if (got == 0 && !force_drain) {
            vTaskDelay(pdMS_TO_TICKS(5)); // Slight wait if dry to avoid tight looping
        } else {
            vTaskDelay(1);
        }
    }
}



static bool is_s3_1_85c_board(const board_profile_t *board)
{
    return board != NULL && board->board_id != NULL && strcmp(board->board_id, "s3_1_85c") == 0;
}

static bool s3_1_85c_audio_init_channels(void)
{
    // V1 layout: Speaker on I2S0, Mic on I2S1. No codec initialization.
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    
    i2s_std_config_t tx_std = {
        .clk_cfg = {
            .sample_rate_hz = S3_1_85C_AUDIO_OUT_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = S3_1_85C_AUDIO_SPK_SCK,
            .ws = S3_1_85C_AUDIO_SPK_WS,
            .dout = S3_1_85C_AUDIO_SPK_SD,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };
    tx_chan_cfg.dma_desc_num = 32;
    tx_chan_cfg.dma_frame_num = 240; // 10ms per descriptor at 24kHz


    i2s_std_config_t rx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(S3_1_85C_AUDIO_IN_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = S3_1_85C_AUDIO_MIC_SCK,
            .ws = S3_1_85C_AUDIO_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = S3_1_85C_AUDIO_MIC_SD,
            .invert_flags = {0},
        },
    };
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    if (s_audio.channels_ready) {
        return true;
    }

    /* Initialize I2S0 for PCM5101 speaker. */
    if (i2s_new_channel(&tx_chan_cfg, &s_audio.tx, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "speaker channel create failed");
        return false;
    }
    if (i2s_channel_init_std_mode(s_audio.tx, &tx_std) != ESP_OK) {
        ESP_LOGE(TAG, "speaker channel init failed");
        return false;
    }
    if (i2s_channel_enable(s_audio.tx) != ESP_OK) {
        ESP_LOGE(TAG, "speaker channel enable failed");
        return false;
    }

    /* Initialize I2S1 for Microphone. */
    if (i2s_new_channel(&rx_chan_cfg, NULL, &s_audio.rx) != ESP_OK) {
        ESP_LOGE(TAG, "mic channel create failed");
        return false;
    }
    if (i2s_channel_init_std_mode(s_audio.rx, &rx_std) != ESP_OK) {
        ESP_LOGE(TAG, "mic channel init failed");
        return false;
    }
    if (i2s_channel_enable(s_audio.rx) != ESP_OK) {
        ESP_LOGE(TAG, "mic channel enable failed");
        return false;
    }

    s_audio.channels_ready = true;

    if (xTaskCreatePinnedToCore(s3_1_85c_spk_task,
                                "audio_spk",
                                4096,
                                NULL,
                                10,
                                &s_audio.spk_task,
                                1) != pdPASS) {
        ESP_LOGE(TAG, "speaker task create failed");
        return false;
    }

    return true;
}

static void s3_1_85c_mic_task(void *arg)
{
    (void)arg;
    int32_t raw_buf[256];
    int16_t pcm_buf[256];
    uint32_t mic_last_log = 0;

    while (true) {
        size_t bytes_read = 0;
        uint32_t rd_ms = 0;
        audio_mic_cb_t mic_cb = s_audio.mic_cb;
        void *mic_ctx = s_audio.mic_ctx;

        if (!s_audio.mic_enabled || mic_cb == NULL) {
            if (s_audio.mic_reads > 0) {
                ESP_LOGI(TAG,
                         "s3 mic stopping reads=%u pcm_bytes=%u",
                         (unsigned)s_audio.mic_reads,
                         (unsigned)s_audio.mic_bytes);
                s_audio.mic_reads = 0;
                s_audio.mic_bytes = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int64_t t0_ms = esp_timer_get_time() / 1000ULL;
        if (i2s_channel_read(s_audio.rx, raw_buf, sizeof(raw_buf), &bytes_read, pdMS_TO_TICKS(200)) == ESP_OK &&
            bytes_read > 0) {
            size_t samples = bytes_read / sizeof(int32_t);
            int32_t peak = 0;

            rd_ms = (uint32_t)((esp_timer_get_time() / 1000ULL) - t0_ms);

            if (samples > (sizeof(pcm_buf) / sizeof(pcm_buf[0]))) {
                samples = sizeof(pcm_buf) / sizeof(pcm_buf[0]);
            }

            for (size_t i = 0; i < samples; ++i) {
                int32_t val = raw_buf[i] >> 10;

                if (val > 32767) {
                    val = 32767;
                }
                if (val < -32768) {
                    val = -32768;
                }
                pcm_buf[i] = (int16_t)val;
                int32_t abs_val = val < 0 ? -val : val;
                if (abs_val > peak) {
                    peak = abs_val;
                }
            }
            s_audio.mic_reads++;
            s_audio.mic_bytes += samples * sizeof(int16_t);

            if (peak < 120) {
                memset(pcm_buf, 0, samples * sizeof(int16_t));
            }
            mic_cb((const uint8_t *)pcm_buf, samples * sizeof(int16_t), mic_ctx);
            if (((uint32_t)(esp_timer_get_time() / 1000ULL)) - mic_last_log >= 1000U) {
                ESP_LOGI(TAG,
                         "s3 mic reads=%u pcm_bytes=%u rd_ms=%u enabled=%d cb=%d peak=%ld",
                         (unsigned)s_audio.mic_reads,
                         (unsigned)s_audio.mic_bytes,
                         (unsigned)rd_ms,
                         s_audio.mic_enabled ? 1 : 0,
                         s_audio.mic_cb != NULL ? 1 : 0,
                         (long)peak);
                mic_last_log = (uint32_t)(esp_timer_get_time() / 1000ULL);
            }
        }

        taskYIELD();
    }
}
#endif

bool audio_platform_init(const board_profile_t *board)
{
    if (board == 0) {
        return false;
    }

    memset(&s_board, 0, sizeof(s_board));
    s_board = *board;
    s_initialized = true;

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(board)) {
        memset(&s_audio, 0, sizeof(s_audio));
        return s3_1_85c_audio_init_channels();
    }
#endif

    return true;
}

bool audio_platform_start_mic(audio_mic_cb_t cb, void *ctx)
{
    if (!s_initialized) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        if (!s_audio.channels_ready) {
            return false;
        }
        s_audio.mic_cb = cb;
        s_audio.mic_ctx = ctx;
        if (s_audio.mic_task == NULL) {
            if (xTaskCreatePinnedToCore(s3_1_85c_mic_task,
                                        "audio_mic",
                                        4096,
                                        NULL,
                                        10,
                                        &s_audio.mic_task,
                                        0) != pdPASS) {
                return false;
            }
        }
        s_audio.mic_enabled = true;
        s_audio.mic_reads = 0;
        s_audio.mic_bytes = 0;
        return true;
    }
#endif

    (void)cb;
    (void)ctx;
    return true;
}

void audio_platform_stop_mic(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        s_audio.mic_enabled = false;
    }
#endif
}

bool audio_platform_write_speaker(const uint8_t *pcm, size_t len)
{
    if (!s_initialized) {
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        if (!s_audio.channels_ready || pcm == NULL || len == 0) {
            return false;
        }
        return spk_ring_push(pcm, len) > 0;
    }
#endif

    (void)pcm;
    (void)len;
    return true;
}

bool audio_platform_set_volume(uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    s_volume = percent;
    ESP_LOGI(TAG, "speaker software volume=%u", (unsigned)s_volume);
    return true;
}

uint8_t audio_platform_get_volume(void)
{
    return s_volume;
}

bool audio_platform_speaker_idle(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        return spk_ring_available() == 0;
    }
#endif
    return true;
}

void audio_platform_clear_speaker_buffer(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        taskENTER_CRITICAL(&s_spk_ring_lock);
        s_spk_ring_rd = 0;
        s_spk_ring_wr = 0;
        s_spk_pre_rolled = false;
        s_audio.spk_finish_pending = false;
        taskEXIT_CRITICAL(&s_spk_ring_lock);
        ESP_LOGI(TAG, "speaker buffer cleared");
    }
#endif
}

void audio_platform_finish_speaker(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    if (is_s3_1_85c_board(&s_board)) {
        s_audio.spk_finish_pending = true;
        ESP_LOGI(TAG, "speaker finish signaled");
    }
#endif
}
