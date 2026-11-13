# Uygulama bileşeni (bileşen adı: "main" değil, "src" olur; REQUIRES açıkça verilir).
idf_component_register(
    SRCS
        "main.c"
        "audio_io.c"
        "opus_codec.c"
        "net_udp.c"
        "jitter_buffer.c"
        "ptt.c"
        "wifi_mgr.c"
        "config_store.c"
        "console.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        driver
        78__esp-opus                 # ESP Component Registry: 78/esp-opus (libopus, FIXED_POINT)
)