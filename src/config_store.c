/* config_store.c - NVS'de saklanan konfigürasyon.
 *
 * Bir "intercom" namespace'i, tek bir "cfg" blob anahtari. NVS bos ya da
 * bozuksa varsayilan degerlerle baslanir ve bu durum loglanir (§8).
 */
#include "config_store.h"
#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG     = "config";
static const char *NVS_NS  = "intercom";
static const char *NVS_KEY = "cfg";

static cfg_t           s_cfg;
static SemaphoreHandle_t s_lock;

static void cfg_defaults(cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->port      = UDP_PORT_DEFAULT;
    c->vol       = VOL_DEFAULT;
    c->mic       = MIC_GAIN_DEFAULT;
    c->use_dhcp  = true;
}

static void cfg_sanitize(cfg_t *c)
{
    c->ssid[sizeof(c->ssid) - 1] = '\0';
    c->pass[sizeof(c->pass) - 1] = '\0';
    c->peer[sizeof(c->peer) - 1] = '\0';
    if (c->port == 0)                    c->port = UDP_PORT_DEFAULT;
    if (c->vol > VOL_MAX)                c->vol  = VOL_DEFAULT;
    if (c->mic < MIC_GAIN_MIN || c->mic > MIC_GAIN_MAX) c->mic = MIC_GAIN_DEFAULT;
}

void config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    cfg_defaults(&s_cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = sizeof(s_cfg);
        err = nvs_get_blob(h, NVS_KEY, &s_cfg, &len);
        nvs_close(h);
        if (err == ESP_OK && len == sizeof(s_cfg)) {
            cfg_sanitize(&s_cfg);
            ESP_LOGI(TAG, "config loaded from NVS (ssid='%s', peer=%s, port=%u)",
                     s_cfg.ssid[0] ? s_cfg.ssid : "", s_cfg.peer[0] ? s_cfg.peer : "(none)",
                     (unsigned)s_cfg.port);
            return;
        }
    }
    cfg_defaults(&s_cfg);
    ESP_LOGW(TAG, "NVS empty/invalid: waiting for configuration on the serial console");
}

void config_get(cfg_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

void config_set(const cfg_t *in)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cfg = *in;
    cfg_sanitize(&s_cfg);
    xSemaphoreGive(s_lock);
}

bool config_save(void)
{
    cfg_t snap;
    config_get(&snap);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(h, NVS_KEY, &snap, sizeof(snap));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs write failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "config saved to NVS");
    return true;
}

bool config_factory_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    cfg_defaults(&s_cfg);
    xSemaphoreGive(s_lock);

    ESP_LOGW(TAG, "factory reset: NVS cleared");
    return err == ESP_OK;
}

bool config_is_set(void)
{
    return s_cfg.ssid[0] != '\0';
}