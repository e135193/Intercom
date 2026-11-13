/* opus_codec.c - libopus sarmalayici.
 *
 * FIXED_POINT derleme zorunludur: opus, ESP Component Registry'den gelen
 * 78/esp-opus bileseni `target_compile_options(... -DFIXED_POINT=1
 * -DDISABLE_FLOAT_API)` ile derlenir. Kayan nokta kullanilmaz (§2).
 *
 * Encode/decode sureleri AUDIO_PERF_LOG=1 iken AUDIO_PERF_LOG_EVERY karede
 * bir ortalama olarak loglanir (§6 - debug secenegi).
 */
#include "opus_codec.h"

#include <string.h>
#include "opus.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "opus";

static OpusEncoder *s_enc;
static OpusDecoder *s_dec;

#if AUDIO_PERF_LOG
static uint64_t s_enc_us;
static uint64_t s_dec_us;
static uint32_t s_frames;
#endif

bool opus_codec_init(void)
{
    int err;

    s_enc = opus_encoder_create(SAMPLE_RATE_HZ, AUDIO_CHANNELS,
                                OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !s_enc) {
        ESP_LOGE(TAG, "encoder create failed: %s", opus_strerror(err));
        return false;
    }
    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(OPUS_BITRATE_BPS));
    opus_encoder_ctl(s_enc, OPUS_SET_VBR(0));
    opus_encoder_ctl(s_enc, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));
    opus_encoder_ctl(s_enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(s_enc, OPUS_SET_PACKET_LOSS_PERC(OPUS_EXPECTED_LOSS_PCT));
    opus_encoder_ctl(s_enc, OPUS_SET_DTX(0));

    s_dec = opus_decoder_create(SAMPLE_RATE_HZ, AUDIO_CHANNELS, &err);
    if (err != OPUS_OK || !s_dec) {
        ESP_LOGE(TAG, "decoder create failed: %s", opus_strerror(err));
        return false;
    }

    ESP_LOGI(TAG, "ready: %d Hz mono, %d bps CBR, complexity %d, FEC on (loss %d%%), FIXED_POINT",
             SAMPLE_RATE_HZ, OPUS_BITRATE_BPS, OPUS_COMPLEXITY, OPUS_EXPECTED_LOSS_PCT);
    return true;
}

int opus_encode_frame(const int16_t pcm[FRAME_SAMPLES], uint8_t *out, int max_out)
{
#if AUDIO_PERF_LOG
    int64_t t0 = esp_timer_get_time();
#endif
    int n = opus_encode(s_enc, pcm, FRAME_SAMPLES, out, max_out);
#if AUDIO_PERF_LOG
    s_enc_us += (uint64_t)(esp_timer_get_time() - t0);
    if (++s_frames >= AUDIO_PERF_LOG_EVERY) {
        unsigned enc = (unsigned)(s_enc_us / s_frames / 10);
        unsigned dec = (unsigned)(s_dec_us / s_frames / 10);
        ESP_LOGI(TAG, "encode avg %u.%02u ms, decode avg %u.%02u ms (%u frames)",
                 enc / 100, enc % 100, dec / 100, dec % 100, (unsigned)s_frames);
        s_enc_us = 0;
        s_dec_us = 0;
        s_frames = 0;
    }
#endif
    if (n < 0) ESP_LOGE(TAG, "encode failed: %s", opus_strerror(n));
    return n;
}

int opus_decode_frame(const uint8_t *data, int len, int16_t pcm[FRAME_SAMPLES], bool fec)
{
#if AUDIO_PERF_LOG
    int64_t t0 = esp_timer_get_time();
#endif
    int n = -1;
    if (data && len > 0) {
        n = opus_decode(s_dec, data, len, pcm, FRAME_SAMPLES, fec ? 1 : 0);
    }
    if (n < 0) {
        n = opus_decode(s_dec, NULL, 0, pcm, FRAME_SAMPLES, 0);
    }
#if AUDIO_PERF_LOG
    s_dec_us += (uint64_t)(esp_timer_get_time() - t0);
#endif
    return n;
}