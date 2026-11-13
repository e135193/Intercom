/* wifi_mgr.h - WiFi station yonetimi: baglanma, ustel geri cekilme, statik IP. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "config_store.h"

/* cfg->ssid bos ise WiFi baslatilmaz. cfg->ssid bosa doluysa NVS'den okunmus
 * sayilir; statik IP ayari varsa DHCP durdurulup IP sabitlenir. */
void wifi_mgr_init(const cfg_t *cfg);
bool wifi_mgr_is_connected(void);
void wifi_mgr_get_ip(char *buf, size_t len);