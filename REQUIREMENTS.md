# ESP32-S3 Opus Interkom Firmware: Gereksinimler

Bu belge, bir AI kodlama ajanının uygulayacağı firmware'in tam gereksinim listesidir. "ZORUNLU" olanlar tartışmasızdır. "VARSAYILAN" olanlar kullanıcı cevabı olmadığı için seçilmiş makul değerlerdir ve `config.h` içinde değiştirilebilir olmalıdır.

## 1. Amaç

İki adet ESP32-S3 kartı aynı yerel Wi-Fi ağında, birbirinin IP adresini bilerek, Opus codec ile konuşma sesini UDP üzerinden iki yönlü ve düşük gecikmeyle aktaran bir interkom oluşturur. Her iki kartta **aynı firmware** çalışır. Kullanım Bas-Konuş (PTT) şeklindedir.

## 2. Donanım ve Ortam (ZORUNLU)

- MCU: ESP32-S3, 8 MB PSRAM (octal veya quad, `platformio.ini` içinde doğru ayarlanmalı).
- Mikrofon: I2S MEMS mikrofon (örn. INMP441 / SPH0645 sınıfı). Slot genişliği 24/32 bit olabilir, 16 bit PCM'e dönüştürülmelidir.
- Hoparlör: I2S amfi (örn. MAX98357A sınıfı) + hoparlör.
- PTT: 1 adet buton (GPIO, dahili pull-up, active-low, yazılımsal debounce).
- Tüm pin atamaları tek bir `include/pins.h` dosyasında toplanmalı. Pinler belirtilmediği için ajan makul varsayılanlar yazmalı ve README yerine dosya başındaki yorumda belirtmelidir.
- Mikrofon (RX) ve amfi (TX) için **iki ayrı I2S portu** kullanılmalı (I2S0 ve I2S1), her ikisi de 16 kHz, 16 bit, mono.
- Derleme: **PlatformIO + ESP-IDF framework** (`framework = espidf`), FreeRTOS görevleriyle. Arduino kullanılmaz.
- Opus: libopus **sabit noktalı (FIXED_POINT)** derlemesi, Arduino olarak kullanılacak. Seçilen yöntem derlenip doğrulanmalı. ESP32-S3 üzerinde kayan noktalı derleme kullanılmamalı.

## 3. Ses ve Codec (ZORUNLU / VARSAYILAN)

| Parametre | Değer |
|---|---|
| Örnekleme hızı | 16 kHz, mono, 16 bit |
| Opus application | `OPUS_APPLICATION_VOIP` |
| Çerçeve süresi | 20 ms (320 örnek) |
| Bitrate | 24 kbps (VBR kapalı, CBR) |
| Complexity | 3 (ölçüme göre 2-5 arası ayarlanabilir) |
| In-band FEC | Açık (`OPUS_SET_INBAND_FEC(1)`, beklenen kayıp %10) |
| DTX | Kapalı (PTT zaten sessizlikte paket göndermez) |
| PLC | Alıcıda kayıp pakette `opus_decode(NULL)` ile uygulanır |

- Mikrofon tarafında DC offset çıkarma (yüksek geçiren filtre, ~100 Hz) ve ayarlanabilir yazılımsal mikrofon kazancı olmalı.
- Hoparlör tarafında yazılımsal ses seviyesi (0-100) olmalı ve clipping'e karşı doyum (saturation) uygulanmalı.

## 4. Ağ Protokolü (ZORUNLU)

**Taşıma: düz UDP** (lwIP BSD soket). TCP, SIP, WebRTC ve RTP yığınları kullanılmaz.

- Her cihaz tek bir UDP portunu (VARSAYILAN: 5004) dinler ve bilinen hedef IP:port'a gönderir. Port yapılandırılabilir olmalı.
- Gönderim yalnızca PTT basılıyken yapılır. Alıcı tarafında çalma, paket gelmesine göre otomatik başlar.
- Soket ayarları: `IP_TOS` = 0xB8 (DSCP EF), non-blocking veya kısa timeout'lu recv, gönderim/alım tamponları küçük tutulur (gecikmeyi büyütmemek için).
- Wi-Fi güç tasarrufu **kapalı**: `esp_wifi_set_ps(WIFI_PS_NONE)`. Wi-Fi 802.11n, mümkünse 20 MHz bant genişliği.
- Gönderilen her UDP paketi tek bir Opus çerçevesi içerir (fragmentasyon olmamalı).

### Paket formatı (little-endian, 12 bayt başlık)

| Offset | Boyut | Alan |
|---|---|---|
| 0 | 2 | magic = 0x4943 |
| 2 | 1 | version = 1 |
| 3 | 1 | flags: bit0 = START (PTT basıldı), bit1 = END (PTT bırakıldı), bit2 = HEARTBEAT |
| 4 | 2 | seq (uint16, her pakette +1) |
| 6 | 4 | timestamp (uint32, örnek sayısı cinsinden, 20 ms = 320) |
| 10 | 2 | payload_len |
| 12 | N | Opus payload |

- Geçersiz magic/version içeren paketler sessizce atılır.
- Yalnızca yapılandırılmış hedef IP'den gelen paketler kabul edilir (kaynak IP filtresi).
- PTT bırakıldığında END bayraklı 3 paket art arda gönderilir (kayıp riskine karşı).
- Bağlantı durumu için, PTT basılı değilken 1 saniyede bir boş payload'lı HEARTBEAT paketi gönderilir. 3 saniye heartbeat/ses alınmazsa "peer yok" durumuna geçilir.

## 5. Jitter Buffer ve Çalma (ZORUNLU)

- Alıcıda seq'e göre sıralayan jitter buffer (PSRAM'de ayrılabilir).
- VARSAYILAN hedef derinlik: 2 çerçeve (40 ms). Alt sınır 1, üst sınır 6 çerçeve. Ağ koşuluna göre uyarlanabilir (adaptif), basit bir algoritma yeterlidir.
- Eksik çerçeve için PLC/FEC kullanılır. Gecikmiş paketler (çalınma zamanı geçmiş) atılır.
- Buffer şişerse (birikme) en eski çerçeveler atılarak gecikme sınırlanmalıdır. Hedef: **uçtan uca ağız-kulak gecikmesi VARSAYILAN olarak ~100 ms civarı**, 150 ms'yi sistematik olarak aşmamalı.
- END bayrağı alınınca veya 200 ms paket gelmezse buffer boşaltılır ve amfi sessizleştirilir (I2S'e sıfır yazılır veya amfi mute). Pop/click oluşmaması için kısa fade-out uygulanmalıdır.
- Her iki cihaz aynı anda PTT'ye basabilir. Gönderme ve alma birbirini engellememelidir (ayrı görevler, ayrı I2S portları). Yankı giderme (AEC) kapsam dışıdır, aynı anda konuşmada geri besleme olması beklenen sınırlama olarak README yerine kod başı yorumunda belirtilmelidir.

## 6. Görev Mimarisi (ZORUNLU)

FreeRTOS görevleri ve önerilen çekirdek dağılımı:

- Wi-Fi/lwIP sistemi: Core 0.
- `audio_capture_task` (I2S RX → Opus encode → UDP send): Core 1, yüksek öncelik.
- `audio_playback_task` (jitter buffer → Opus decode → I2S TX): Core 1, yüksek öncelik.
- `net_rx_task` (UDP recv → jitter buffer): Core 0.
- `ptt_task` (buton okuma, debounce, durum makinesi).
- `console_task` (UART/USB konfigürasyon komutları).
- Görevler arası veri aktarımı kilitsiz/kısa kilitli ring buffer veya FreeRTOS queue ile yapılır. Ses yolunda `malloc` çağrısı olmamalı (başlangıçta ayrılan statik/PSRAM tamponlar).
- Encode/decode süreleri ölçülüp logda bildirilebilmelidir (debug seçeneği).

## 7. Wi-Fi Yönetimi (ZORUNLU)

- Station modu. SSID/şifre NVS'den okunur.
- Bağlantı koparsa otomatik yeniden bağlanma (üstel geri çekilme: 1 s'den 10 s'ye kadar). Sonsuz deneme.
- Bağlantı yokken PTT basılırsa ses gönderilmez, durum "bağlantı yok" olarak bildirilir.
- IP alındığında/kaybedildiğinde ve peer durumunda seri logda bilgi verilir.
- IP: VARSAYILAN DHCP. İsteğe bağlı statik IP (ip, gateway, netmask) konfigürasyonla ayarlanabilir.

## 8. Konfigürasyon (ZORUNLU)

- Bir kez yapılan konfigürasyon **UART/USB (USB Serial/JTAG veya UART0) konsolu** üzerinden, satır tabanlı komutlarla yapılır ve **NVS**'de saklanır. Cihaz her açılışta NVS'den okur. NVS boşsa konfigürasyon beklenir ve bu durum logda açıkça belirtilir.
- Komutlar (en az):
  - `set ssid <değer>`
  - `set pass <değer>`
  - `set peer <ip>`
  - `set port <port>`
  - `set vol <0-100>`
  - `set mic <kazanç>`
  - `set ip <ip> <gw> <mask>` / `set dhcp`
  - `show` (şifre gizlenerek mevcut ayarlar)
  - `save`, `reboot`, `factory` (NVS'yi siler), `help`
- Girdi doğrulaması yapılmalı (IP formatı, aralıklar). Hatalı girişte açıklayıcı mesaj yazılmalı.
- Şifre `show` çıktısında maskelenmelidir.

## 9. Durum Göstergesi

- PTT durumu ve bağlantı durumu en azından seri logda raporlanır (TX başladı/bitti, RX başladı/bitti, Wi-Fi bağlı/koptu, peer var/yok).
- İsteğe bağlı: `pins.h` içinde tanımlıysa tek bir durum LED'i (Wi-Fi yok: yavaş yanıp söner, bağlı: sürekli yanar, TX: hızlı yanıp söner).

## 10. Güvenlik

- Yerel ağda açık iletişim kabul edilmiştir, şifreleme yoktur. Yalnızca kaynak IP filtresi ve paket doğrulaması uygulanır.

## 11. Proje Yapısı ve Çıktı

Yalnızca firmware kaynak kodu istenir (ayrı test aracı, PC yazılımı, test planı kapsam dışı):

```
platformio.ini
sdkconfig.defaults
src/ (main.c, audio_io.c, opus_codec.c, net_udp.c, jitter_buffer.c, ptt.c, wifi_mgr.c, config_store.c, console.c)
include/ (pins.h, config.h, ve ilgili başlık dosyaları)
```

- Dil: C (veya C++), ESP-IDF API'leri.
- `sdkconfig.defaults` içinde en az: PSRAM etkin, CPU 240 MHz, Wi-Fi tamponları ve lwIP ayarları düşük gecikmeye göre, FreeRTOS tick 1000 Hz.
- Kod okunabilir, modüler ve yorumlu olmalı. Sihirli sayılar `config.h` içinde isimlendirilmeli.

## 12. Kabul Kriterleri

1. `pio run` hatasız derlenir.
2. İki kart konsoldan yapılandırılıp açıldığında Wi-Fi'ye bağlanır, birbirini heartbeat ile algılar.
3. A kartında PTT basılıyken konuşma B kartında anlaşılır şekilde çalar, tersi de geçerlidir.
4. PTT bırakıldığında ses 200 ms içinde kesilir, pop/click olmaz.
5. Router kapatılıp açıldığında kartlar kendiliğinden yeniden bağlanır ve peer'i tekrar bulur.
6. Normal ağ koşullarında ağız-kulak gecikmesi ~100 ms civarında, 150 ms altındadır.
7. Opus encode/decode CPU yükü makul düzeydedir (ses görevleri 20 ms içinde güvenle tamamlanır).
8. Şifre/ayarlar yeniden başlatmadan sonra korunur. `factory` komutu NVS'yi temizler.

## 13. Kapsam Dışı

AEC / gürültü bastırma, şifreleme, OTA, ekran/enkoder arayüzü, web arayüzü, internet/NAT üzerinden bağlantı, ikiden fazla cihaz, PC tarafı yazılım.
