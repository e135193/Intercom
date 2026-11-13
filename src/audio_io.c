/* audio_io.c - I2S0 (mikrofon RX) ve I2S1 (amfi TX).
 *
 * Mikrofon: 32-bit Philips slot, mono/LEFT. INMP441 24-bit veriyi sol
 * hizali gonderir; ust 16 bit 16-bit PCM'e cevrilir (>>16) - donusum
 * burada yapilir (§2: slot 24/32 bit olabilir, 16 bit PCM'e cevrilmelidir).
 *
 * Amfi: 16-bit Philips slot, mono/LEFT, 16 kHz. MAX98357A sinifi amfilerde
 * tam seviye icin SD pini mono veya sol-kanal moduna ayarlanmalidir
 * (stereo modda sag kanal sessiz oldugundan seviye dusebilir).
 *
 * Her iki port da ayri donanim birimi => gonderme ve alma ayni anda yapilir.
 * Ses yolunda malloc yok; tamponlar statiktir.
 */
#include "audio_io.h"
#include "pins.h"

#include <string.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s";

static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static int32_t s_raw[FRAME_SAMPLES];

bool audio_io_init(void)
{
    i2s_chan_config_t ccfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&ccfg, NULL, &s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 channel alloc failed");
        return false;
    }
    i2s_std_config_t rcfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S0_BCLK,
            .ws   = PIN_I2S0_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S0_DIN,
        },
    };
    rcfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    if (i2s_channel_init_std_mode(s_rx, &rcfg) != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 std mode init failed");
        return false;
    }
    i2s_channel_enable(s_rx);

    i2s_chan_config_t tcfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tcfg.auto_clear = true;
    if (i2s_new_channel(&tcfg, &s_tx, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 channel alloc failed");
        return false;
    }
    i2s_std_config_t wcfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S1_BCLK,
            .ws   = PIN_I2S1_WS,
            .dout = PIN_I2S1_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    wcfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    if (i2s_channel_init_std_mode(s_tx, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 std mode init failed");
        return false;
    }
    i2s_channel_enable(s_tx);

    ESP_LOGI(TAG, "I2S0 RX mic: bclk=%d ws=%d din=%d | I2S1 TX amp: bclk=%d ws=%d dout=%d (%d Hz)",
             PIN_I2S0_BCLK, PIN_I2S0_WS, PIN_I2S0_DIN,
             PIN_I2S1_BCLK, PIN_I2S1_WS, PIN_I2S1_DOUT, SAMPLE_RATE_HZ);
    return true;
}

int audio_io_read(int16_t *pcm, int samples)
{
    if (!s_rx || samples <= 0 || samples > FRAME_SAMPLES) return -1;

    int got = 0;
    while (got < samples) {
        size_t rb = 0;
        esp_err_t e = i2s_channel_read(s_rx, s_raw + got,
                                       (size_t)(samples - got) * sizeof(int32_t),
                                       &rb, pdMS_TO_TICKS(100));
        if (e != ESP_OK || rb == 0) break;
        got += (int)(rb / sizeof(int32_t));
    }

    for (int i = 0; i < got; i++) {
        pcm[i] = (int16_t)(s_raw[i] >> 16);
    }
    return got;
}

void audio_io_drain_rx(void)
{
    if (!s_rx) return;
    int32_t dump[64];
    size_t rb;
    while (i2s_channel_read(s_rx, dump, sizeof(dump), &rb, 0) == ESP_OK && rb > 0) {
    }
}

int audio_io_write(const int16_t *pcm, int samples)
{
    if (!s_tx || samples <= 0) return -1;
    size_t wb = 0;
    esp_err_t e = i2s_channel_write(s_tx, pcm, (size_t)samples * sizeof(int16_t),
                                    &wb, pdMS_TO_TICKS(100));
    if (e != ESP_OK) return -1;
    return (int)(wb / sizeof(int16_t));
}

int audio_io_write_silence(int samples)
{
    if (!s_tx || samples <= 0) return -1;
    static int16_t zeros[FRAME_SAMPLES];
    if (samples > FRAME_SAMPLES) samples = FRAME_SAMPLES;
    memset(zeros, 0, (size_t)samples * sizeof(int16_t));
    size_t wb = 0;
    esp_err_t e = i2s_channel_write(s_tx, zeros, (size_t)samples * sizeof(int16_t),
                                    &wb, pdMS_TO_TICKS(100));
    return (e == ESP_OK) ? (int)(wb / sizeof(int16_t)) : -1;
}