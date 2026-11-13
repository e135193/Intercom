# AGENTS.md

## Repo state

- The spec is `REQUIREMENTS.md` (in Turkish) — it is authoritative. **Read it fully before writing any code**; this file only lists constraints that are easy to miss.
- The firmware source tree now exists (see "Required layout") and `pio run` compiles clean on Windows.
- No README is to be written. Guidance that would normally go in a README (default pin mapping, the no-AEC limitation when both sides press PTT) must instead be a comment at the top of the relevant source file.

## Build (verified on Windows)

- `pio` is **not** on PATH here; invoke PlatformIO as `python -m platformio run` from the project root.
- Opus comes from the ESP Component Registry: `78/esp-opus` declared in **`src/idf_component.yml`** (not the project root — the component manager only reads manifests from component directories, a root-level manifest is silently ignored). `src/CMakeLists.txt` requires it as `78__esp-opus`; the component's CMakeLists applies `-DFIXED_POINT=1 -DDISABLE_FLOAT_API` (verified in `build.ninja`).
- The app partition is 1500 K via `board_build.partitions = partitions_singleapp_large.csv` in `platformio.ini`. **PlatformIO generates `partitions.bin` itself and ignores `CONFIG_PARTITION_TABLE_*` in `sdkconfig.defaults`** — the Kconfig line alone does nothing. The default 1 M app partition was 97 % full.
- ESP-IDF 5.4+ renamed the Wi-Fi bandwidth enum: use `WIFI_BW20`, not `WIFI_BANDWIDTH_20MHZ`.
- Flash size must be stated or the build warns "Expected 8MB, found 2MB": `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in `sdkconfig.defaults`.

## Stack (non-negotiable)

- ESP32-S3, 8 MB PSRAM, PlatformIO with `framework = espidf`. **Arduino is not used.** FreeRTOS tasks only.
- Build/verify: `pio run` must compile clean (acceptance criterion 1). There is no test suite, linter, or CI yet.
- libopus must be a **fixed-point (`FIXED_POINT`)** build as an ESP-IDF component (Component Registry package or xiph/opus source) — no floating-point Opus on ESP32-S3. Whatever method you pick, verify it actually compiles.
- Two separate I2S ports: I2S0 = microphone RX, I2S1 = amplifier TX; both 16 kHz / 16-bit / mono.

## Required layout

```
platformio.ini
sdkconfig.defaults
src/    main.c audio_io.c opus_codec.c net_udp.c jitter_buffer.c ptt.c wifi_mgr.c config_store.c console.c
include/ pins.h config.h (+ headers)
```

- All pin assignments live in `include/pins.h` (PTT is GPIO, active-low, internal pull-up, software debounce). Magic numbers must be named constants in `include/config.h`.
- `sdkconfig.defaults` must at least enable PSRAM, CPU 240 MHz, low-latency Wi-Fi/lwIP buffers, FreeRTOS tick 1000 Hz.

## Protocol specifics (easy to get wrong)

- Plain UDP (lwIP BSD sockets), default port 5004, one Opus frame per packet — no fragmentation, no TCP/SIP/WebRTC/RTP.
- 12-byte little-endian header: magic `0x4943` (2), version `1` (1), flags (1; bit0 START, bit1 END, bit2 HEARTBEAT), seq uint16, timestamp uint32 in samples (20 ms = 320), payload_len (16). Invalid magic/version → drop silently; only packets from the configured peer IP are accepted.
- TX only while PTT held; on release send 3 consecutive END-flagged packets; empty-payload HEARTBEAT every 1 s when idle; no packet/heartbeat for 3 s ⇒ "peer gone".
- `IP_TOS = 0xB8` (DSCP EF), small send/recv buffers, Wi-Fi power save off (`esp_wifi_set_ps(WIFI_PS_NONE)`).
- Opus: 16 kHz mono, 20 ms frames (320 samples), 24 kbps CBR, complexity 3, in-band FEC on (10 % expected loss), DTX off, receiver PLC via `opus_decode(NULL)`.

## Runtime constraints

- Audio path must not call `malloc`; allocate static/PSRAM buffers at startup. Task/queue transfer via short-locked ring buffers or FreeRTOS queues.
- Core pinning: capture (I2S→encode→send) and playback (jitter→decode→I2S) on core 1 at high priority; Wi-Fi, `net_rx_task`, `ptt_task`, `console_task` on core 0.
- Jitter buffer: target depth 2 frames (40 ms), clamp 1–6, adaptive; drop late frames, oldest-first on overflow; flush + mute with short fade-out on END or 200 ms of silence. Target end-to-end latency ~100 ms, never systematically above 150 ms.
- Config is set via line-based UART/USB console commands (`set ssid|pass|peer|port|vol|mic|ip|dhcp`, `show`, `save`, `reboot`, `factory`, `help`), stored in NVS; mask the password in `show`; validate inputs. Empty NVS ⇒ wait for config and log it clearly.
- Reconnect with exponential backoff 1 s→10 s, infinite retries; no PTT audio sent while disconnected.

## Scope traps

Out of scope — do not build: AEC/noise suppression, encryption, OTA, display/encoder UI, web UI, internet/NAT traversal, >2 devices, PC-side software, separate test tools or test plans.
