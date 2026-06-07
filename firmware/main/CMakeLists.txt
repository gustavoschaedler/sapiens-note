idf_component_register(
    SRCS
        "main.cpp"
        "hal/EspStorage.cpp"
        "hal/EspDisplay.cpp"
        "hal/EspInput.cpp"
        "hal/EspAudio.cpp"
    INCLUDE_DIRS
        "."
        "hal"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../simulator/src"  # shared Canvas/Font/Renderer
    REQUIRES
        sapienscore
        driver
        esp_lcd
        fatfs
        sdmmc
        esp_adc
        esp_timer
        esp_psram
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
