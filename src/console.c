/* console.c - satir tabanli UART/USB konfigurasyon komutlari (§8).
 *
 * Komutlar RAM'de config_store kopyasini degistirir; `save` NVS'ye yazar.
 * Aga/SSID/IP'ye etki eden degisiklikler `reboot` ile uygulanir; vol/mic/peer
 * aninda uygulanir (gorevler her karede kopyayi okur).
 *
 * Girdi dogrulamasi yapilir, hatali giriste aciklayici mesaj yazilir ve
 * `show` ciktisinda sifre maskelenir (§8).
 */
#include "console.h"
#include "config.h"
#include "config_store.h"
#include "net_udp.h"
#include "wifi_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_ip_addr.h"

static const char *HELP_TEXT =
    "Commands:\n"
    "  set ssid <value>         Wi-Fi SSID (1-32 chars)\n"
    "  set pass <value>         Wi-Fi password (8-63 chars, empty = open)\n"
    "  set peer <ip>            peer IP address\n"
    "  set port <port>          UDP port 1-65535 (default 5004, after reboot)\n"
    "  set vol <0-100>          speaker volume (immediate)\n"
    "  set mic <1-200>          mic gain, 100 = unity (immediate)\n"
    "  set ip <ip> <gw> <mask>  static IP (after reboot)\n"
    "  set dhcp                 use DHCP (default, after reboot)\n"
    "  show                     show settings (password masked)\n"
    "  save                     persist settings to NVS\n"
    "  reboot                   restart the device\n"
    "  factory                  erase NVS and restart\n"
    "  help                     this text\n";

static bool parse_ip(const char *s, uint32_t *out)
{
    ip4_addr_t a;
    if (!s || !ip4addr_aton(s, &a)) return false;
    *out = a.addr;
    return true;
}

static void cmd_show(void)
{
    cfg_t c;
    char  ip[16];
    config_get(&c);
    wifi_mgr_get_ip(ip, sizeof(ip));

    printf("ssid      : %s\n", c.ssid[0] ? c.ssid : "(not set)");
    printf("pass      : %s\n", c.pass[0] ? "********" : "(empty/open)");
    printf("peer      : %s\n", c.peer[0] ? c.peer : "(not set)");
    printf("port      : %u\n", (unsigned)c.port);
    printf("vol       : %u\n", (unsigned)c.vol);
    printf("mic       : %u  (100 = unity)\n", (unsigned)c.mic);
    if (c.use_dhcp) {
        printf("ip        : DHCP\n");
    } else {
        printf("ip        : static (after reboot)\n");
        printf("  ip/gw/mask: " IPSTR " / " IPSTR " / " IPSTR "\n",
               IP2STR((esp_ip4_addr_t *)&c.ip),
               IP2STR((esp_ip4_addr_t *)&c.gw),
               IP2STR((esp_ip4_addr_t *)&c.mask));
    }
    printf("wifi      : %s (ip=%s)\n",
           wifi_mgr_is_connected() ? "connected" : "disconnected", ip);
    printf("peer seen : %s\n", net_udp_peer_present() ? "present" : "absent");
    printf("configured: %s\n", config_is_set() ? "yes" : "NO - waiting for config");
}

static bool check_len(const char *what, const char *v, int min, int max)
{
    size_t n = strlen(v);
    if (n < (size_t)min || n > (size_t)max) {
        printf("error: %s must be %d-%d characters (got %u)\n",
               what, min, max, (unsigned)n);
        return false;
    }
    return true;
}

static bool check_range(const char *what, long v, long min, long max)
{
    if (v < min || v > max) {
        printf("error: %s must be %ld-%ld (got %ld)\n", what, min, max, v);
        return false;
    }
    return true;
}

static bool parse_long(const char *s, long *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!s[0] || (end && *end) != '\0') {
        printf("error: '%s' is not a number\n", s);
        return false;
    }
    *out = v;
    return true;
}

static void apply_live_changes(const cfg_t *c)
{
    net_udp_set_peer(c->peer);
}

static void handle_set(char *args)
{
    cfg_t c;
    char *save = NULL;
    char *key = strtok_r(args, " \t", &save);
    if (!key) {
        printf("error: missing setting name; try 'help'\n");
        return;
    }
    config_get(&c);

    if (strcmp(key, "ssid") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        if (!v || !check_len("ssid", v, 1, 32)) { printf("usage: set ssid <value>\n"); return; }
        strlcpy(c.ssid, v, sizeof(c.ssid));
    } else if (strcmp(key, "pass") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        if (!v) { printf("usage: set pass <value>  (empty value = open network)\n"); return; }
        if (v[0] && !check_len("pass", v, 8, 63)) return;
        strlcpy(c.pass, v, sizeof(c.pass));
    } else if (strcmp(key, "peer") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        uint32_t ip;
        if (!v || !parse_ip(v, &ip)) { printf("usage: set peer <ip>  (e.g. 192.168.1.23)\n"); return; }
        strlcpy(c.peer, v, sizeof(c.peer));
    } else if (strcmp(key, "port") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        long p;
        if (!v || !parse_long(v, &p) || !check_range("port", p, 1, 65535)) {
            printf("usage: set port <1-65535>\n"); return;
        }
        c.port = (uint16_t)p;
    } else if (strcmp(key, "vol") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        long n;
        if (!v || !parse_long(v, &n) || !check_range("vol", n, VOL_MIN, VOL_MAX)) {
            printf("usage: set vol <0-100>\n"); return;
        }
        c.vol = (uint8_t)n;
    } else if (strcmp(key, "mic") == 0) {
        char *v = strtok_r(NULL, " \t", &save);
        long n;
        if (!v || !parse_long(v, &n) || !check_range("mic", n, MIC_GAIN_MIN, MIC_GAIN_MAX)) {
            printf("usage: set mic <1-200>\n"); return;
        }
        c.mic = (uint8_t)n;
    } else if (strcmp(key, "ip") == 0) {
        char *a = strtok_r(NULL, " \t", &save);
        char *g = strtok_r(NULL, " \t", &save);
        char *m = strtok_r(NULL, " \t", &save);
        uint32_t ip, gw, mask;
        if (!a || !g || !m || !parse_ip(a, &ip) || !parse_ip(g, &gw) || !parse_ip(m, &mask)) {
            printf("usage: set ip <ip> <gateway> <netmask>\n"); return;
        }
        c.ip = ip; c.gw = gw; c.mask = mask;
        c.use_dhcp = false;
    } else if (strcmp(key, "dhcp") == 0) {
        c.use_dhcp = true;
    } else {
        printf("error: unknown setting '%s'; try 'help'\n", key);
        return;
    }

    config_set(&c);
    apply_live_changes(&c);
    printf("ok: %s set (use 'save' to persist; network changes apply after 'reboot')\n", key);
}

static void handle_line(char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;

    if (strcmp(cmd, "help") == 0) {
        printf("%s", HELP_TEXT);
    } else if (strcmp(cmd, "show") == 0) {
        cmd_show();
    } else if (strcmp(cmd, "set") == 0) {
        handle_set(save ? save : "");
    } else if (strcmp(cmd, "save") == 0) {
        if (config_save()) printf("saved to NVS\n");
        else printf("error: NVS write failed (see log)\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        printf("rebooting...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (strcmp(cmd, "factory") == 0) {
        if (config_factory_erase()) {
            printf("NVS erased; rebooting...\n");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else {
            printf("error: factory reset failed (see log)\n");
        }
    } else {
        printf("error: unknown command '%s'; try 'help'\n", cmd);
    }
}

void console_task(void *arg)
{
    (void)arg;
    static char line[160];
    int pos = 0;
    bool skip_lf = false;

    printf("\nintercom> ");
    fflush(stdout);

    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (ch == '\r') {
            skip_lf = true;
            goto eol;
        }
        if (ch == '\n') {
            if (skip_lf) { skip_lf = false; continue; }
            goto eol;
        }
        skip_lf = false;

        if (ch == '\b' || ch == 0x7f) {
            if (pos > 0) { pos--; printf("\b \b"); }
        } else if (ch >= 32 && ch < 127 && pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)ch;
            putchar(ch);
        }
        fflush(stdout);
        continue;

    eol:
        printf("\n");
        line[pos] = '\0';
        if (pos > 0) handle_line(line);
        pos = 0;
        printf("intercom> ");
        fflush(stdout);
    }
}