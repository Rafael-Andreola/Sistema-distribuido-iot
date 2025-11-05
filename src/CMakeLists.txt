cmake_minimum_required(VERSION 3.13)
project(freertos_mqtt_tago C)

set(CMAKE_C_STANDARD 11)

# Diretório do kernel já clonado no Dockerfile
set(FREERTOS_DIR ${CMAKE_SOURCE_DIR}/FreeRTOS-Kernel)

add_executable(freertos_mqtt_tago
    main.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
)

target_include_directories(freertos_mqtt_tago PRIVATE
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix
    ${CMAKE_SOURCE_DIR} # <-- ajustado para o diretório atual
)

target_link_libraries(freertos_mqtt_tago pthread mosquitto)