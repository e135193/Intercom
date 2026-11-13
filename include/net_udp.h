/* net_udp.h - duz UDP soketi, 12 bayt'lik paket basligi, peer filtresi.
 *
 * Paket formati (little-endian):
 *   0..1  magic 0x4943 | 2 version=1 | 3 flags | 4..5 seq
 *   6..9  timestamp (ornek sayisi) | 10..11 payload_len | 12.. payload
 * flags: bit0 START, bit1 END, bit2 HEARTBEAT.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

typedef struct {
    uint8_t       flags;
    uint16_t      seq;
    uint32_t      timestamp;
    uint16_t      payload_len;
    const uint8_t *payload;   /* sadece bir sonraki net_udp_recv cagrisinda gecerli */
} pkt_t;

bool net_udp_open(uint16_t port);       /* bind + IP_TOS + kisa recv zaman asimi */
void net_udp_close(void);
void net_udp_set_peer(const char *ip);  /* canli olarak degisebilir ('set peer') */
bool net_udp_peer_configured(void);
int  net_udp_send(uint8_t flags, uint16_t seq, uint32_t timestamp,
                  const uint8_t *payload, uint16_t len);   /* >0 ok, <=0 hata */
int  net_udp_recv(pkt_t *out);          /* 1: gecerli paket, 0: zaman asimi/atlandi, -1: hata */
bool net_udp_peer_present(void);        /* son PEER_TIMEOUT_MS icinde paket geldi mi */

void net_udp_task(void *arg);           /* net_rx_task: recv -> jitter buffer */