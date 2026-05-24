idf_component_register(
    SRCS "main.c" "webserver.c" "tft.c" "conversions.c"
    INCLUDE_DIRS "../include"
)

target_add_binary_data(${COMPONENT_TARGET} "index.html" TEXT)