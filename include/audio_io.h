/* audio_io.h - I2S ses yoluna erisim.
 *
 * I2S0 = mikrofon RX (32-bit slot, 24-bit veri sol hizali -> ust 16 bit PCM)
 * I2S1 = amfi TX (16-bit slot, mono/LEFT; MAX98357A'nin mono veya sol-kanal
 *        modunda tam seviye vermesi icin SD pini ayarina bakin, bkz. pins.h).
 * Her ikisi de 16 kHz, 16 bit, mono icerik. Ayri portlar => ayni anda
 * gonderme/alma engellenmez.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"

bool audio_io_init(void);

/* Mikrofonlarin 32-bit slotundan `samples` PCM ornek okur (bloklar).
 * Donus: okunan ornek sayisi (FRAME_SAMPLES'den az olabilir). */
int  audio_io_read(int16_t *pcm, int samples);

/* DMA'da birikmis eski ornekleri at (PTT basiminda tazelik icin). */
void audio_io_drain_rx(void);

/* Amfiye `samples` PCM ornek yazar; DMA dolu oldugunda blokler => kare hizlar.
 * Donus: yazilan ornek sayisi, <0 hata. */
int  audio_io_write(const int16_t *pcm, int samples);

/* Amfiye sessizlik yazar (ayni bicimde hizlanir). */
int  audio_io_write_silence(int samples);