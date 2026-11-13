/* jitter_buffer.c - alici jitter buffer (§5).
 *
 *  - seq % JB_SLOTS ile hash'lenmis slotlar; wrap-around karsilastirmasi
 *    (int16_t)(a-b) ile 65535 -> 0 gecislerinde dogru siralar.
 *  - Hedef derinlik adaptif: bosluk (underrun) olursa artar, slot tasmasi
 *    (overflow) olursa azalir; JB_MIN_FRAMES..JB_MAX_FRAMES arasinda sinirli.
 *  - Tahsis baslangicta yapilir; ses yolunda malloc yoktur.
 *    (Istege bagli: slot dizisi PSRAM'e alinabilir, bkz. JB_SLOTS.)
 */
#include "jitter_buffer.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "jb";

static SemaphoreHandle_t s_lock;
static jb_frame_t       s_slots[JB_SLOTS];
static bool             s_valid[JB_SLOTS];
static int              s_count;
static int              s_target   = JB_TARGET_FRAMES;
static uint32_t         s_last_audio_ms;
static int64_t          s_last_adapt_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static int seq_cmp(uint16_t a, uint16_t b) { return (int16_t)(a - b); }

static void adapt_up_locked(void)
{
    if (s_target >= JB_MAX_FRAMES) return;
    int64_t t = now_ms();
    if (t - s_last_adapt_ms < JB_ADAPT_COOLDOWN_MS) return;
    s_target++;
    s_last_adapt_ms = t;
    ESP_LOGI(TAG, "underrun: target depth -> %d frames (%d ms)",
             s_target, s_target * FRAME_MS);
}

static void adapt_down_locked(void)
{
    if (s_target <= JB_MIN_FRAMES) return;
    int64_t t = now_ms();
    if (t - s_last_adapt_ms < JB_ADAPT_COOLDOWN_MS) return;
    s_target--;
    s_last_adapt_ms = t;
    ESP_LOGI(TAG, "overflow: target depth -> %d frames (%d ms)",
             s_target, s_target * FRAME_MS);
}

void jb_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    jb_clear();
}

void jb_clear(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_valid, 0, sizeof(s_valid));
    s_count        = 0;
    s_last_audio_ms = 0;
    xSemaphoreGive(s_lock);
}

bool jb_put(uint16_t seq, const uint8_t *data, uint16_t len)
{
    if (len > OPUS_MAX_PAYLOAD) return false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool stored = false;
    int idx = seq % JB_SLOTS;

    if (s_valid[idx]) {
        if (seq_cmp(s_slots[idx].seq, seq) < 0) {
            s_valid[idx] = false;
            s_count--;
            adapt_down_locked();
        } else {
            xSemaphoreGive(s_lock);
            return false;
        }
    }

    s_slots[idx].seq = seq;
    s_slots[idx].len = len;
    memcpy(s_slots[idx].data, data, len);
    s_valid[idx] = true;
    s_count++;
    s_last_audio_ms = (uint32_t)now_ms();
    stored = true;
    xSemaphoreGive(s_lock);
    return stored;
}

bool jb_take(uint16_t seq, jb_frame_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = seq % JB_SLOTS;
    bool ok = s_valid[idx] && s_slots[idx].seq == seq;
    if (ok) {
        *out = s_slots[idx];
        s_valid[idx] = false;
        s_count--;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

bool jb_front_seq(uint16_t *seq)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool found = false;
    uint16_t best = 0;
    for (int i = 0; i < JB_SLOTS; i++) {
        if (!s_valid[i]) continue;
        if (!found || seq_cmp(s_slots[i].seq, best) < 0) {
            best = s_slots[i].seq;
            found = true;
        }
    }
    if (found) *seq = best;
    xSemaphoreGive(s_lock);
    return found;
}

bool jb_front_newer_than(uint16_t seq, jb_frame_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool found = false;
    int best_i = -1;
    for (int i = 0; i < JB_SLOTS; i++) {
        if (!s_valid[i] || seq_cmp(s_slots[i].seq, seq) <= 0) continue;
        if (!found || seq_cmp(s_slots[i].seq, s_slots[best_i].seq) < 0) {
            best_i = i;
            found = true;
        }
    }
    if (found) *out = s_slots[best_i];
    xSemaphoreGive(s_lock);
    return found;
}

void jb_drop_older(uint16_t seq)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < JB_SLOTS; i++) {
        if (s_valid[i] && seq_cmp(s_slots[i].seq, seq) < 0) {
            s_valid[i] = false;
            s_count--;
        }
    }
    xSemaphoreGive(s_lock);
}

int jb_count(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int c = s_count;
    xSemaphoreGive(s_lock);
    return c;
}

int jb_target_depth(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int t = s_target;
    xSemaphoreGive(s_lock);
    return t;
}

void jb_note_underrun(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    adapt_up_locked();
    xSemaphoreGive(s_lock);
}

uint32_t jb_last_audio_ms(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t t = s_last_audio_ms;
    xSemaphoreGive(s_lock);
    return t;
}