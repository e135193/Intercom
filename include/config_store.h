/* config_store.h - NVS tabanli yapilandirma (REQUIREMENTS.md §8). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char     ssid[33];
    char     pass[65];
    char     peer[16];      /* nokta-dortlu hedef IP, "" = atanmadi */
    uint16_t port;          /* yerel + hedef UDP portu */
    uint8_t  vol;           /* hoparlör seviyesi 0-100 */
    uint8_t  mic;           /* mikrofon kazanci 1-200, 100 = x1.0 */
    bool     use_dhcp;
    uint32_t ip, gw, mask;  /* network byte order; sadece !use_dhcp */
} cfg_t;

void config_init(void);                 /* NVS'den okur; bos/butuk ise varsayilan */
void config_get(cfg_t *out);            /* anlik kopya (kilitli) */
void config_set(const cfg_t *in);       /* RAM kopyasini degistir (konsol 'set') */
bool config_save(void);                 /* NVS'ye yazar */
bool config_factory_erase(void);        /* NVS anahtarini siler ('factory') */
bool config_is_set(void);               /* ssid tanimli mi */