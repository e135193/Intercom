/*
 * pins.h - tum pin atamalari tek dosyada.
 *
 * NOT (README yerine bu yorum): Asagidaki pinler makul varsayilanlardir;
 * REQUIREMENTS.md'de pin belirtilmedigi icin ESP32-S3-DevKitC-1 uzerine
 * gorelandirilmistir. Kartiniz farkliysa sadece bu dosyayi degistirin.
 *
 *   - Mikrofon (I2S0, RX): INMP441 / SPH0645 sinifi I2S MEMS.
 *     L/R pin topraga bagli => sol (LEFT) slot gecerlidir.
 *   - Amfi (I2S1, TX): MAX98357A sinifi I2S amfi.
 *   - PTT (GPIO14): dahili pull-up, active-low, yazilimsal debounce.
 *
 * Kullanilmayan GPIO'lara dikkat: ESP32-S3'te octal PSRAM GPIO33-37'yi
 * kullanir; GPIO0/3/45/46 strapping, GPIO26-32 flash'tir. Asagidaki secimler
 * bu baglantilari dikkate alir.
 */
#pragma once

/* ---- I2S0: mikrofon (RX) ---- */
#define PIN_I2S0_BCLK      5
#define PIN_I2S0_WS        4
#define PIN_I2S0_DIN       6     /* mikrofonun SD cikisi -> ESP32 girisi */

/* ---- I2S1: amfi (TX) ---- */
#define PIN_I2S1_BCLK     41
#define PIN_I2S1_WS       42
#define PIN_I2S1_DOUT     40

/* ---- PTT butonu ---- */
#define PIN_PTT           14     /* active-low, dahili pull-up */

/* ---- Durum LED'i (istege bagli) ---- */
#define PIN_STATUS_LED     2     /* aktif-yuksek; -1 yaparsaniz kapali kalir */
#define STATUS_LED_ACTIVE_LEVEL 1