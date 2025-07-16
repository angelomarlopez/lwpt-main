idf_component_register(SRCS "lwpt.cpp" "WifiManager.cpp" "HttpClient.cpp" "HttpManager.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES
                    mdns
                    nvs_flash
                    esp_netif
                    esp_event
                    esp_wifi
                    esp_timer
                    esp_driver_spi
                    esp_http_server
                    esp_http_client
                    esp_driver_gpio
                    json
                    driver)
