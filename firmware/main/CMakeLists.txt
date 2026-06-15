idf_component_register(
    SRCS
        "main.cpp"
        "hal/EspClock.cpp"
        "hal/EspNet.cpp"
        "hal/EspDashboard.cpp"
        "hal/EspPower.cpp"
        "hal/EspLed.cpp"
        "hal/DeviceSettings.cpp"
        "hal/EspStorage.cpp"
        "hal/SdNoteRepository.cpp"
        "hal/EspDisplay.cpp"
        "hal/epaper_driver_bsp.cpp"
        "hal/board_power_bsp.cpp"
        "hal/EspInput.cpp"
        "hal/EspAudio.cpp"
    INCLUDE_DIRS
        "."
        "hal"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../simulator/src"  # shared Canvas/Font/Renderer
        "${CMAKE_CURRENT_SOURCE_DIR}/../../webtool"        # sapiens-web page.h (dashboard)
    REQUIRES
        sapienscore
        audiocodec
        driver
        esp_lcd
        fatfs
        sdmmc
        esp_driver_sdmmc
        esp_adc
        esp_timer
        esp_psram
        nvs_flash
        json
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_http_client
        esp-tls
        mbedtls
        esp_hw_support
        esp_driver_usb_serial_jtag
        esp_tinyusb
        lwip
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
