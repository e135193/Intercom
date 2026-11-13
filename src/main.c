/*
 * main.c - uygulama girisi, gorev olusturma ve ses yolu orkestrasyonu.
 *
 * gorev dagilimi (§6):
 *   Core 0: wifi/lwIP (IDF), net_rx_task, ptt_task, console_task
 *   Core 1: audio_capture_task (I2S0 RX -> Opus encode -> UDP send)
 *           audio_playback_task (jitter buffer -> Opus decode -> I2S1 TX)
 *
 * NOT (README yerine bu yorum): varsayilan pin atamalari include/pins.h'de,
 * AEC kapsam disiligi ve gecikme butcesi include/config.h'de aciklanmistir.
 * Ayni anda iki taraf PTT'ye basarsa yanki olmasi beklenen bir sinirlamadir.
 *
 * Ses yolunda malloc yoktur: tum tamponlar statik, encode/decode durumlari
 * baslangicta olusturulur.
 */
#include "config.h"
#include "config_store.h"
#include "audio_io.h"
#include "opus_codec.h"
#include "net_udp.h"
#include "jitter_buffer.h"
#include "ptt.h"
#include "wifi_mgr.h"
#include "console.h"

#include <math.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

/* ------------------------------------------------------------------ */
/* Gonderim: I2S0 RX -> on-islem -> Opus encode -> UDP                */
/* ------------------------------------------------------------------ */
static void audio_capture_task(void *arg)
{
    (void)arg;
    int16_t  pcm[FRAME_SAMPLES];
    uint8_t  payload[OPUS_MAX_PAYLOAD];
    uint16_t seq = 0;
    uint32_t timestamp = 0;

    bool tx_active   = false;
    bool was_pressed = false;
    bool sent_audio  = false;
    bool first_frame = false;
    bool blocked_logged = false;
    bool peer_logged  = false;
    int64_t last_hb_us = 0;

    const float PI_F = 3.14159265358979323846f;
    const float R = expf(-2.0f * PI_F * (float)DC_HP_CUTOFF_HZ / (float)SAMPLE_RATE_HZ);
    float x1 = 0.0f, y1 = 0.0f;

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS));

        cfg_t cfg;
        config_get(&cfg);
        const bool conn    = wifi_mgr_is_connected();
        const bool pressed = ptt_pressed();
        const bool peer_ok = cfg.peer[0] != '\0';
        const int64_t now_us = esp_timer_get_time();

        bool tx_now = pressed && conn && peer_ok;
        if (tx_now && !tx_active) {
            ESP_LOGI(TAG, "TX started");
            audio_io_drain_rx();
            first_frame = true;
            sent_audio  = false;
            tx_active   = true;
            blocked_logged = false;
            peer_logged = false;
        } else if (!tx_now && tx_active) {
            tx_active = false;
            if (pressed && !conn)      ESP_LOGW(TAG, "TX blocked: no Wi-Fi connection");
            else if (pressed && !peer_ok) ESP_LOGW(TAG, "TX blocked: peer IP not configured");
            else                       ESP_LOGI(TAG, "TX stopped");
        } else if (pressed && !conn && !blocked_logged) {
            ESP_LOGW(TAG, "TX blocked: no Wi-Fi connection");
            blocked_logged = true;
        } else if (pressed && conn && !peer_ok && !peer_logged) {
            ESP_LOGW(TAG, "TX blocked: peer IP not configured (set peer <ip>)");
            peer_logged = true;
        }

        if (tx_active) {
            int n = audio_io_read(pcm, FRAME_SAMPLES);
            if (n == FRAME_SAMPLES) {
                const float g = (float)cfg.mic / 100.0f;
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    float x = (float)pcm[i];
                    float y = x - x1 + R * y1;
                    x1 = x;
                    y1 = y;
                    float v = y * g;
                    if (v >  32767.0f) v =  32767.0f;
                    if (v < -32768.0f) v = -32768.0f;
                    pcm[i] = (int16_t)v;
                }

                int plen = opus_encode_frame(pcm, payload, sizeof(payload));
                if (plen > 0) {
                    uint8_t flags = 0;
                    if (first_frame) {
                        flags |= PKT_FLAG_START;
                        first_frame = false;
                    }
                    if (net_udp_send(flags, seq, timestamp, payload, (uint16_t)plen) > 0) {
                        sent_audio = true;
                    }
                    seq++;
                    timestamp += FRAME_SAMPLES;
                }
            }
        } else {
            if (was_pressed && !pressed && sent_audio && conn && peer_ok) {
                for (int i = 0; i < END_REPEAT_COUNT; i++) {
                    net_udp_send(PKT_FLAG_END, seq++, timestamp, NULL, 0);
                }
                ESP_LOGI(TAG, "sent %d END packets", END_REPEAT_COUNT);
            }
            if (was_pressed && !pressed) sent_audio = false;

            if (!pressed && conn && peer_ok &&
                (now_us - last_hb_us) >= (int64_t)HEARTBEAT_PERIOD_MS * 1000) {
                last_hb_us = now_us;
                net_udp_send(PKT_FLAG_HEARTBEAT, seq++, timestamp, NULL, 0);
            }
        }

        was_pressed = pressed;
    }
}

/* ------------------------------------------------------------------ */
/* Alim: jitter buffer -> Opus decode (FEC/PLC) -> hacim/doyum -> I2S1 */
/* ------------------------------------------------------------------ */

typedef enum { PB_IDLE, PB_PRIMING, PB_PLAYING } pb_state_t;

static int16_t s_out[FRAME_SAMPLES];
static int16_t s_last_sample;
static int     s_fade_idx;

static void write_fade_or_silence(void)
{
    if (s_fade_idx > 0) {
        for (int i = 0; i < FRAME_SAMPLES; i++) {
            if (s_fade_idx > 0) {
                s_out[i] = (int16_t)(((int32_t)s_last_sample * s_fade_idx) / FADE_SAMPLES);
                s_fade_idx--;
            } else {
                s_out[i] = 0;
            }
        }
        audio_io_write(s_out, FRAME_SAMPLES);
    } else {
        audio_io_write_silence(FRAME_SAMPLES);
    }
}

static void audio_playback_task(void *arg)
{
    (void)arg;
    int16_t pcm[FRAME_SAMPLES];
    jb_frame_t fr;
    pb_state_t state = PB_IDLE;
    uint16_t expected = 0;
    int64_t priming_t0 = 0;
    int64_t wait_t0 = 0;

    for (;;) {
        cfg_t cfg;
        config_get(&cfg);
        const float vol = (float)cfg.vol / (float)VOL_MAX;
        const int64_t now = esp_timer_get_time() / 1000;

        if (state == PB_PLAYING) {
            jb_drop_older(expected);

            bool decoded = false;
            if (jb_take(expected, &fr)) {
                opus_decode_frame(fr.data, fr.len, pcm, false);
                expected++;
                decoded = true;
                wait_t0 = now;
            } else {
                jb_frame_t newer;
                if (jb_front_newer_than(expected, &newer)) {
                    opus_decode_frame(newer.data, newer.len, pcm, true);
                    expected++;
                    decoded = true;
                    wait_t0 = now;
                } else {
                    const uint32_t last = jb_last_audio_ms();
                    if (last == 0 || (now - (int64_t)last) > RX_FLUSH_TIMEOUT_MS) {
                        jb_clear();
                        s_fade_idx = FADE_SAMPLES;
                        state = PB_IDLE;
                        ESP_LOGI(TAG, "RX playback stopped (flush)");
                        write_fade_or_silence();
                        continue;
                    } else if ((now - wait_t0) > LATE_TOLERANCE_MS) {
                        opus_decode_frame(NULL, 0, pcm, false);
                        jb_note_underrun();
                        expected++;
                        decoded = true;
                        wait_t0 = now;
                    } else {
                        opus_decode_frame(NULL, 0, pcm, false);
                        decoded = true;
                    }
                }
            }

            if (decoded) {
                for (int i = 0; i < FRAME_SAMPLES; i++) {
                    float v = pcm[i] * vol;
                    if (v >  32767.0f) v =  32767.0f;
                    if (v < -32768.0f) v = -32768.0f;
                    s_out[i] = (int16_t)v;
                }
                s_last_sample = s_out[FRAME_SAMPLES - 1];
                audio_io_write(s_out, FRAME_SAMPLES);
            }
            continue;
        }

        if (state == PB_PRIMING) {
            int c = jb_count();
            if (c == 0) {
                state = PB_IDLE;
            } else if (c >= jb_target_depth() ||
                       (now - priming_t0) > JB_PRIMING_TIMEOUT_MS) {
                if (jb_front_seq(&expected)) {
                    wait_t0 = now;
                    state = PB_PLAYING;
                    ESP_LOGI(TAG, "RX playback started (prebuffer %d frames)", c);
                } else {
                    state = PB_IDLE;
                }
            }
            audio_io_write_silence(FRAME_SAMPLES);
            continue;
        }

        if (jb_count() > 0) {
            state = PB_PRIMING;
            priming_t0 = now;
        }
        write_fade_or_silence();
    }
}

/* ------------------------------------------------------------------ */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    config_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (!config_is_set()) {
        ESP_LOGW(TAG, "NVS empty: waiting for configuration. Use the serial console: "
                      "set ssid <..>, set pass <..>, set peer <ip>, save, reboot");
    }

    if (!audio_io_init())   ESP_LOGE(TAG, "audio I2S init failed");
    if (!opus_codec_init()) ESP_LOGE(TAG, "opus init failed");
    jb_init();

    cfg_t cfg;
    config_get(&cfg);
    if (!net_udp_open(cfg.port)) ESP_LOGE(TAG, "UDP open failed");
    net_udp_set_peer(cfg.peer);
    ptt_init();

    if (config_is_set()) {
        wifi_mgr_init(&cfg);
    } else {
        ESP_LOGW(TAG, "Wi-Fi not started (no SSID configured)");
    }

    xTaskCreatePinnedToCore(audio_capture_task,  "capture",
                            TASK_STACK_CAPTURE,  NULL, TASK_PRIO_CAPTURE,  NULL, TASK_CORE_AUDIO);
    xTaskCreatePinnedToCore(audio_playback_task, "playback",
                            TASK_STACK_PLAYBACK, NULL, TASK_PRIO_PLAYBACK, NULL, TASK_CORE_AUDIO);
    xTaskCreatePinnedToCore(net_udp_task,        "net_rx",
                            TASK_STACK_NET_RX,   NULL, TASK_PRIO_NET_RX,   NULL, TASK_CORE_SYS);
    xTaskCreatePinnedToCore(ptt_task,            "ptt",
                            TASK_STACK_PTT,      NULL, TASK_PRIO_PTT,      NULL, TASK_CORE_SYS);
    xTaskCreatePinnedToCore(console_task,        "console",
                            TASK_STACK_CONSOLE,  NULL, TASK_PRIO_CONSOLE,  NULL, TASK_CORE_SYS);

    ESP_LOGI(TAG, "intercom ready: %s:%u, peer=%s",
             cfg.ssid[0] ? cfg.ssid : "(no wifi)", (unsigned)cfg.port,
             cfg.peer[0] ? cfg.peer : "(not set)");
}