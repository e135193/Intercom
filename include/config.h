/*
 * config.h - isimli sabitler (sihirli sayi yok).
 *
 * Gecikme butcesi (REQUIREMENTS.md §5): ucgen-uc ~100 ms hedef, 150 ms
 * sistematik ust sinir. 20 ms frame + ~2 frame jitter on-tampon (40 ms) +
 * encode/decode + ag = ~100 ms.
 *
 * NOT (README yerine bu yorum): AEC kapsam disidir; iki taraf ayni anda
 * PTT'ye basarsa hoparlorden mikrofona geri besleme (yanki) olur. Bu,
 * bilinen ve beklenen bir sinirlamadir.
 */
#pragma once

/* ---------------- Ses / Opus ---------------- */
#define SAMPLE_RATE_HZ        16000
#define AUDIO_CHANNELS        1
#define FRAME_MS              20
#define FRAME_SAMPLES         320       /* 16 kHz * 20 ms */

#define OPUS_APPLICATION      2         /* OPUS_APPLICATION_VOIP (opus.h degeri) */
#define OPUS_BITRATE_BPS      24000     /* CBR */
#define OPUS_COMPLEXITY       3         /* 2-5 araliginda ayarlanabilir */
#define OPUS_EXPECTED_LOSS_PCT 10       /* in-band FEC icin beklenen kayip % */
#define OPUS_MAX_PAYLOAD      512       /* 24 kbps CBR'de ~60 bayt; pay + kenar */

/* Mikrofon on-islemi: ~100 Hz kesimli DC offset (yuksek geciren) filtresi */
#define DC_HP_CUTOFF_HZ       100.0f
#define MIC_GAIN_MIN          1
#define MIC_GAIN_MAX          200       /* 100 = birim kazanc (x1.0) */
#define MIC_GAIN_DEFAULT      100

/* Hoparlör seviyesi 0-100 (100 = tam) + clipping'e karsi doyum */
#define VOL_MIN               0
#define VOL_MAX               100
#define VOL_DEFAULT           80

/* ---------------- UDP / paket ---------------- */
#define UDP_PORT_DEFAULT      5004
#define PKT_MAGIC             0x4943    /* "IC" */
#define PKT_VERSION           1
#define PKT_HEADER_LEN        12
#define PKT_FLAG_START        0x01      /* PTT basildi */
#define PKT_FLAG_END          0x02      /* PTT birakildi */
#define PKT_FLAG_HEARTBEAT    0x04
#define UDP_RECV_TIMEOUT_MS   50        /* kisa timeout'lu recv (gecikme sinirlama) */
#define UDP_SEND_TIMEOUT_MS   100
#define IP_TOS_DSCP_EF        0xB8

#define HEARTBEAT_PERIOD_MS   1000      /* bosken 1 sn'de bir heartbeat */
#define PEER_TIMEOUT_MS       3000      /* 3 sn ses/heartbeat yoksa "peer yok" */
#define END_REPEAT_COUNT      3         /* PTT birakilinca 3 paket art arda END */
#define RX_FLUSH_TIMEOUT_MS   200       /* 200 ms ses gelmezse buffer bosalt */

/* ---------------- Jitter buffer ---------------- */
#define JB_TARGET_FRAMES      2         /* varsayilan hedef derinlik (40 ms) */
#define JB_MIN_FRAMES         1
#define JB_MAX_FRAMES         6
#define JB_SLOTS              8         /* seq % 8 ile indekslenir (2^3 > JB_MAX) */
#define JB_PRIMING_TIMEOUT_MS 500       /* hedefe ulasilamazsa yine de basla */
#define JB_ADAPT_COOLDOWN_MS  1000      /* hedef uyarmasi icin bekleme */

/* ---------------- Zamanlama / gorevler ---------------- */
#define PTT_DEBOUNCE_MS       30
#define WIFI_BACKOFF_MIN_MS   1000
#define WIFI_BACKOFF_MAX_MS   10000     /* ustel geri cekilme 1 sn -> 10 sn */

#define TASK_CORE_SYS         0         /* wifi/lwIP, net_rx, ptt, console */
#define TASK_CORE_AUDIO       1         /* capture + playback */
#define TASK_PRIO_CAPTURE     12
#define TASK_PRIO_PLAYBACK    12
#define TASK_PRIO_NET_RX      10
#define TASK_PRIO_PTT         6
#define TASK_PRIO_CONSOLE     5

#define TASK_STACK_CAPTURE    24576     /* opus encode (alloca) icin genis */
#define TASK_STACK_PLAYBACK   24576
#define TASK_STACK_NET_RX     8192
#define TASK_STACK_PTT        4096
#define TASK_STACK_CONSOLE    8192

/* Encode/decode suresi logu: 1 acik, 0 kapali (debug secenegi) */
#define AUDIO_PERF_LOG        1
#define AUDIO_PERF_LOG_EVERY  100       /* her N karede bir ortala logla */

#define LATE_TOLERANCE_MS     40        /* beklenen kare bu kadar gec gelirse kayip sayilir */
#define FADE_SAMPLES          160       /* flush'ta 10 ms fade-out (pop/click onleme) */