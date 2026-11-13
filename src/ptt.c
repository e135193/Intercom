/* ptt.c - PTT butonu (dahili pull-up, active-low, yazilimsal debounce)
 * ve istege bagli durum LED'i (§9).
 *
 * LED desenleri:
 *   Wi-Fi yok / konfigurasyon yok -> yavas yanip sone (500 ms)
 *   Bagli, PTT kapali            -> surekli yanar
 *   TX aktif (PTT + bagli)       -> hizli yanip sone (100 ms)
 *
 * NOT (README yerine bu yorum): AEC kapsam disidir; ayni anda basilan iki
 * PTT tarafinda geri besleme (yanki) beklenen bir sinirlamadir (§5).
 */
#include "ptt.h"
#include "pins.h"
#include "config.h"
#include "wifi_mgr.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ptt";

static volatile bool s_pressed;

void ptt_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_PTT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

#if PIN_STATUS_LED >= 0
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << PIN_STATUS_LED,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(PIN_STATUS_LED, 0);
#endif
    ESP_LOGI(TAG, "PTT on GPIO%d (active-low, pull-up, %d ms debounce)",
             PIN_PTT, PTT_DEBOUNCE_MS);
}

bool ptt_pressed(void)
{
    return s_pressed;
}

void ptt_task(void *arg)
{
    (void)arg;
    bool     stable = false;
    bool     last_raw = false;
    int64_t  last_change_us = 0;

    int64_t led_next_us = 0;
    bool    led_on = false;

    for (;;) {
        int64_t now_us = esp_timer_get_time();
        bool raw = (gpio_get_level(PIN_PTT) == 0);

        if (raw != last_raw) {
            last_raw = raw;
            last_change_us = now_us;
        }
        if (raw != stable &&
            (now_us - last_change_us) >= (int64_t)PTT_DEBOUNCE_MS * 1000) {
            stable = raw;
            if (stable != s_pressed) {
                s_pressed = stable;
                ESP_LOGI(TAG, "PTT %s", stable ? "pressed" : "released");
            }
        }

#if PIN_STATUS_LED >= 0
        bool     conn = wifi_mgr_is_connected();
        bool     tx   = s_pressed && conn;
        int      period_ms = !conn ? 1000 : (tx ? 100 : 0);
        if (period_ms == 0) {
            led_on = true;
            led_next_us = now_us;
        } else if (now_us >= led_next_us) {
            led_on = !led_on;
            led_next_us = now_us + (int64_t)period_ms * 1000 / 2;
        }
        gpio_set_level(PIN_STATUS_LED, led_on ? STATUS_LED_ACTIVE_LEVEL
                                              : !STATUS_LED_ACTIVE_LEVEL);
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}