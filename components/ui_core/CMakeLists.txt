idf_component_register(
    SRCS "ui_font.c" "ui_draw.c"
    INCLUDE_DIRS "."
    REQUIRES st7789 log heap
)
