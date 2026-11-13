/* opus_codec.h - libopus sarmalayici (FIXED_POINT derleme, 16 kHz mono). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

bool opus_codec_init(void);

/* PCM -> Opus; donen deger bayt sayisi, <0 hata. */
int opus_encode_frame(const int16_t pcm[FRAME_SAMPLES], uint8_t *out, int max_out);

/* Opus -> PCM.
 *  data==NULL/len<=0 : PLC (opus_decode(NULL))
 *  fec=true          : data'deki SONRAKI karenin FEC bilgisiyle onceki (kayip)
 *                      kare yeniden kurulur; basarisizsa PLC'ye dusulur.
 * Donen deger ornek sayisi, <0 hata. */
int opus_decode_frame(const uint8_t *data, int len, int16_t pcm[FRAME_SAMPLES], bool fec);