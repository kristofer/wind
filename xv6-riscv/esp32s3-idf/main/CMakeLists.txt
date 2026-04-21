set(WIND_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

idf_component_register(
    SRCS
        app_main.c
        ${WIND_ROOT}/kernel/xtensa/main.c
        ${WIND_ROOT}/kernel/xtensa/uart.c
    INCLUDE_DIRS
        ${WIND_ROOT}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE WIND_ESP_IDF_APP=1)
