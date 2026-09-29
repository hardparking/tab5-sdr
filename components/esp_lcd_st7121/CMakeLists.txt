idf_component_register(SRCS "esp_lcd_st7121.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_lcd")

# The vendored public header already defines ESP_LCD_ST7121_VER_*.
