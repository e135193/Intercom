/* wifi_mgr.c - WiFi station yonetimi (REQUIREMENTS.md §7).
 *
 *  - SSID/sifre NVS'den (config_store) gelir; bossa WiFi baslatilmaz.
 *  - Kopunca ustel geri cekilmeli (1 sn -> 10 sn) sonsuz yeniden deneme.
 *  - Guclu tasarruf KAPALI (WIFI_PS_NONE), 802.11n + 20 MHz band.
 *  - DHCP varsayilan; `set ip ...` ile statik IP (reboot'ta uygulanir).
 *
 * Yeniden deneme esp_timer one-shot ile yapilir (event handler'da uyku yok).
 */
#include "wifi_mgr.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

#define EVT_CONNECTED BIT0

static esp_netif_t       *s_netif;
static EventGroupHandle_t s_eg;
static esp_timer_handle_t s_reconn_timer;
static char               s_ip[16];
static volatile bool      s_connected;
static bool               s_static_ip;
static int64_t            s_backoff_ms = WIFI_BACKOFF_MIN_MS;

static void reconnect_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "reconnecting now");
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        s_connected = false;
        xEventGroupClearBits(s_eg, EVT_CONNECTED);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), next retry in %lld ms",
                 d->reason, (long long)s_backoff_ms);
        esp_timer_stop(s_reconn_timer);
        esp_timer_start_once(s_reconn_timer, (uint64_t)(s_backoff_ms * 1000));
        s_backoff_ms *= 2;
        if (s_backoff_ms > WIFI_BACKOFF_MAX_MS) s_backoff_ms = WIFI_BACKOFF_MAX_MS;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_backoff_ms = WIFI_BACKOFF_MIN_MS;
        s_connected = true;
        xEventGroupSetBits(s_eg, EVT_CONNECTED);
        ESP_LOGI(TAG, "Wi-Fi connected, ip=%s", s_ip);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        if (s_static_ip) {
            s_connected = true;
            xEventGroupSetBits(s_eg, EVT_CONNECTED);
            ESP_LOGI(TAG, "Wi-Fi connected (static ip=%s)", s_ip);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        s_connected = false;
        xEventGroupClearBits(s_eg, EVT_CONNECTED);
        ESP_LOGW(TAG, "IP lost");
    }
}

void wifi_mgr_init(const cfg_t *cfg)
{
    s_eg = xEventGroupCreate();

    esp_event_handler_instance_t h1, h2;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &h1);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, &h2);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_event, NULL, &h2);

    esp_timer_create_args_t targs = {
        .callback = reconnect_cb,
        .name     = "wifi_reconn",
    };
    esp_timer_create(&targs, &s_reconn_timer);

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t winit = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&winit));

    if (!cfg->use_dhcp) {
        s_static_ip = true;
        esp_netif_dhcpc_stop(s_netif);
        esp_netif_ip_info_t ipinfo = {
            .ip.addr      = cfg->ip,
            .gw.addr      = cfg->gw,
            .netmask.addr = cfg->mask,
        };
        esp_netif_set_ip_info(s_netif, &ipinfo);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ipinfo.ip));
    }

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strlcpy((char *)wc.sta.ssid, cfg->ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, cfg->pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = cfg->pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_STA,
                          WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);

    ESP_LOGI(TAG, "STA started, ssid='%s' (retry backoff %d..%d ms)",
             cfg->ssid, WIFI_BACKOFF_MIN_MS, WIFI_BACKOFF_MAX_MS);
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}

void wifi_mgr_get_ip(char *buf, size_t len)
{
    strlcpy(buf, s_ip[0] ? s_ip : "-", len);
}