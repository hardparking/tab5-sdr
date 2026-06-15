file(GLOB_RECURSE BSP_SRCS "*.c")
idf_component_register(
    SRCS ${BSP_SRCS}
    INCLUDE_DIRS "inc"
    PRIV_INCLUDE_DIRS "inc_private" "devices"
    # public headers: bsp_tab5.h includes esp_lcd_touch.h and esp_codec_dev.h
    REQUIRES esp_lcd_touch esp_codec_dev
    PRIV_REQUIRES driver esp_driver_i2s nvs_flash bt)

# Enable Link Time Optimization
# Disabled locally: esp-elf 14.2.0 LTO emits the `xesppie` ISA extension that
# isn't in the -march string, breaking the final link ("xesppie must be set
# with the versions"). Safe to drop — it's only an optimization.
# target_compile_options(${COMPONENT_LIB} PRIVATE -flto)
# target_link_libraries(${COMPONENT_LIB} PRIVATE -flto)
