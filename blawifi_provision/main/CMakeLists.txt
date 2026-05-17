file(GLOB_RECURSE srcs "*.c")
file(GLOB_RECURSE provision_www "wifi_provision_www/*")
idf_component_register(SRCS "${srcs}"
                       PRIV_REQUIRES esp_wifi wpa_supplicant nvs_flash esp_http_server esp_netif esp_event lwip
                       INCLUDE_DIRS "."
                       EMBED_FILES "${provision_www}")
