/* jitter_buffer.h - seq'e gore siralayan alici buffer (REQUIREMENTS.md §5).
 *
 * Slotalar seq % JB_SLOTS ile indekslenir; seq karsilastirmalari 16-bit
 * wrap-around icin (int16_t)(a-b) seklinde yapilir. Ic kilit kisadir;
 * ses yolunda malloc yoktur (tahsis baslangicta yapilir).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

typedef struct {
    uint16_t seq;
    uint16_t len;
    uint8_t  data[OPUS_MAX_PAYLOAD];
} jb_frame_t;

void jb_init(void);
void jb_clear(void);                                   /* END veya zaman asiminda */
bool jb_put(uint16_t seq, const uint8_t *data, uint16_t len);  /* false: dup/gec */
bool jb_take(uint16_t seq, jb_frame_t *out);           /* beklenen kare varsa alir */
bool jb_front_seq(uint16_t *seq);                      /* en eski karenin seq'i */
bool jb_front_newer_than(uint16_t seq, jb_frame_t *out); /* FEC icin sonraki kare */
void jb_drop_older(uint16_t seq);                      /* beklenenden eski kareleri at */
int  jb_count(void);
int  jb_target_depth(void);                            /* adaptif hedef derinlik 1..6 */
void jb_note_underrun(void);                           /* hedefi yukseltir (cooldown'lu) */
uint32_t jb_last_audio_ms(void);                       /* 0 = END ile sifirlanmis */