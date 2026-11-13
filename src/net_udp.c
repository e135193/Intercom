/* net_udp.c - duz UDP (lwIP BSD soket) + paket basligi + peer filtresi (§4).
 *
 *  - Tek soket, tek port (varsayilan 5004); ayni port hem dinlenir hem gonderilir.
 *  - IP_TOS = 0xB8 (DSCP EF); recv kisa timeout'lu (select) => gecikme kucuk kalir.
 *  - Tamponlar lwIP varsayilan (kucuk) birakilir; bilerek buyutulmez.
 *  - Sadece yapilandirilmis peer IP'sinden paket kabul edilir; gecersiz
 *    magic/version sessizce atilir.
 *
 * net_udp_task = "net_rx_task": recv -> jitter buffer + peer/RX durum loglari.
 */
#include "net_udp.h"
#include "jitter_buffer.h"

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

static int      s_fd = -1;
static uint16_t s_port;
static uint32_t s_peer_addr;
static uint8_t  s_rxbuf[PKT_HEADER_LEN + OPUS_MAX_PAYLOAD];

static int64_t s_last_rx_ms;
static bool    s_stream_active;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool net_udp_open(uint16_t port)
{
    net_udp_close();
    s_port = port;

    s_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return false;
    }

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_port        = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGE(TAG, "bind(:%u) failed: errno %d", (unsigned)port, errno);
        close(s_fd);
        s_fd = -1;
        return false;
    }

    int tos = IP_TOS_DSCP_EF;
    if (setsockopt(s_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
        ESP_LOGW(TAG, "setsockopt(IP_TOS) not accepted: errno %d", errno);
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = UDP_RECV_TIMEOUT_MS * 1000 };
    setsockopt(s_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "UDP listening on :%u", (unsigned)port);
    return true;
}

void net_udp_close(void)
{
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
}

void net_udp_set_peer(const char *ip)
{
    ip4_addr_t a;
    if (ip && ip[0] && ip4addr_aton(ip, &a)) {
        s_peer_addr = a.addr;
        ESP_LOGI(TAG, "peer set to %s", ip);
    } else {
        s_peer_addr = 0;
        ESP_LOGW(TAG, "peer cleared/invalid");
    }
}

bool net_udp_peer_configured(void)
{
    return s_peer_addr != 0;
}

int net_udp_send(uint8_t flags, uint16_t seq, uint32_t timestamp,
                 const uint8_t *payload, uint16_t len)
{
    if (s_fd < 0 || s_peer_addr == 0) return -1;
    if (len > OPUS_MAX_PAYLOAD) return -1;

    uint8_t buf[PKT_HEADER_LEN + OPUS_MAX_PAYLOAD];
    put_u16(&buf[0], PKT_MAGIC);
    buf[2] = PKT_VERSION;
    buf[3] = flags;
    put_u16(&buf[4], seq);
    put_u32(&buf[6], timestamp);
    put_u16(&buf[10], len);
    if (len && payload) memcpy(&buf[PKT_HEADER_LEN], payload, len);

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(s_port);
    dst.sin_addr.s_addr = s_peer_addr;

    int n = (int)sendto(s_fd, buf, PKT_HEADER_LEN + len, MSG_DONTWAIT,
                        (struct sockaddr *)&dst, sizeof(dst));
    if (n < 0) {
        ESP_LOGD(TAG, "sendto failed: errno %d", errno);
        return -1;
    }
    return n;
}

int net_udp_recv(pkt_t *out)
{
    if (s_fd < 0) return -1;

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s_fd, &rd);
    struct timeval tv = { .tv_sec = 0, .tv_usec = UDP_RECV_TIMEOUT_MS * 1000 };
    int r = select(s_fd + 1, &rd, NULL, NULL, &tv);
    if (r <= 0) return 0;

    struct sockaddr_in src;
    socklen_t sl = sizeof(src);
    int n = (int)recvfrom(s_fd, s_rxbuf, sizeof(s_rxbuf), MSG_DONTWAIT,
                          (struct sockaddr *)&src, &sl);
    if (n < 0) return 0;

    if (s_peer_addr == 0 || src.sin_addr.s_addr != s_peer_addr) return 0;
    if (n < PKT_HEADER_LEN) return 0;

    uint16_t magic = get_u16(&s_rxbuf[0]);
    uint8_t  ver   = s_rxbuf[2];
    if (magic != PKT_MAGIC || ver != PKT_VERSION) return 0;

    uint16_t plen = get_u16(&s_rxbuf[10]);
    if (plen != (uint16_t)(n - PKT_HEADER_LEN)) return 0;
    if (plen > OPUS_MAX_PAYLOAD) return 0;

    out->flags       = s_rxbuf[3];
    out->seq         = get_u16(&s_rxbuf[4]);
    out->timestamp   = get_u32(&s_rxbuf[6]);
    out->payload_len = plen;
    out->payload     = &s_rxbuf[PKT_HEADER_LEN];

    s_last_rx_ms = now_ms();
    return 1;
}

bool net_udp_peer_present(void)
{
    int64_t last = s_last_rx_ms;
    return last != 0 && (now_ms() - last) < PEER_TIMEOUT_MS;
}

void net_udp_task(void *arg)
{
    (void)arg;
    pkt_t pkt;
    bool peer_was_present = false;

    for (;;) {
        int r = net_udp_recv(&pkt);

        bool present = net_udp_peer_present();
        if (present != peer_was_present) {
            ESP_LOGI(TAG, "peer %s", present ? "present" : "absent");
            peer_was_present = present;
        }

        if (r == 1) {
            if (pkt.flags & PKT_FLAG_END) {
                if (s_stream_active) {
                    ESP_LOGI(TAG, "RX: stream stopped (END)");
                    s_stream_active = false;
                }
                jb_clear();
            } else if (pkt.payload_len > 0) {
                if (!s_stream_active) {
                    ESP_LOGI(TAG, "RX: stream started");
                    s_stream_active = true;
                }
                jb_put(pkt.seq, pkt.payload, pkt.payload_len);
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (s_stream_active) {
            uint32_t last = jb_last_audio_ms();
            if (last == 0 || (uint32_t)now_ms() - last > RX_FLUSH_TIMEOUT_MS) {
                ESP_LOGI(TAG, "RX: stream stopped (timeout)");
                s_stream_active = false;
                jb_clear();
            }
        }
    }
}