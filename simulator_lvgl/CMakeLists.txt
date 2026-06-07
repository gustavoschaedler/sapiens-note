# LVGL-based simulator (opt-in: -DSAPIENS_WITH_LVGL=ON). Fetches LVGL v9 and builds a
# real-widget UI driven by the shared core. Kept separate so the default build
# stays fast and dependency-light.
include(FetchContent)

# Pinned to v9.2.2: LVGL 9.5 switched to a heavier CMake build (mandatory lv_conf.h
# + a Python config-generation step) that doesn't drop into our FetchContent setup
# cleanly. Revisit as a focused migration when needed.
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.2.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

# Configure LVGL via compile definitions (no lv_conf.h needed in 9.2.x).
target_compile_definitions(lvgl PUBLIC
    LV_CONF_SKIP=1
    LV_USE_SDL=1
    LV_USE_CANVAS=1            # splash logo is drawn into an lv_canvas
    LV_COLOR_DEPTH=32
    LV_USE_LOG=0
    # Built-in bitmap fonts (stable; no runtime TTF rasterizer).
    LV_FONT_MONTSERRAT_14=1
    LV_FONT_MONTSERRAT_22=1
    LV_FONT_MONTSERRAT_28=1
    LV_FONT_MONTSERRAT_36=1
    LV_FONT_MONTSERRAT_40=1
    LV_FONT_MONTSERRAT_48=1
    LV_USE_STDLIB_MALLOC=1     # = LV_STDLIB_CLIB
    "LV_SDL_INCLUDE_PATH=<SDL2/SDL.h>"
)
if(TARGET SDL2::SDL2)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
else()
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

add_executable(sapiens-sim-lvgl main.cpp)
# reuse the simulator's SdlAudio (real capture/playback)
target_include_directories(sapiens-sim-lvgl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/src)
target_link_libraries(sapiens-sim-lvgl PRIVATE sapienscore sapiensstore lvgl)
if(TARGET SDL2::SDL2)
    target_link_libraries(sapiens-sim-lvgl PRIVATE SDL2::SDL2)
endif()
