cmake_minimum_required(VERSION 3.13)

# Preset by make_project.sh. It can still be overridden with -DPICO_SDK_PATH=...
set(PICO_SDK_PATH "/storage/Develop/pico_prog/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_BOARD "pico_w" CACHE STRING "Raspberry Pi Pico board")
set(CMAKE_C_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc-13.2.1")
set(CMAKE_CXX_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-g++")
include(pico_sdk_import.cmake)

project(dm441bscpi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(dm441bscpi
    dm441bscpi.c
    acquisition.c
    display_decoder.c
    scpi_tcp_srv.c
)

target_link_libraries(dm441bscpi PRIVATE
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_timer
    pico_cyw43_arch_lwip_poll
)

pico_add_extra_outputs(dm441bscpi)

pico_enable_stdio_uart(dm441bscpi 0)
pico_enable_stdio_usb(dm441bscpi 1)
if(NOT DEFINED WIFI_SSID OR WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "Configure WIFI_SSID and WIFI_PASSWORD")
endif()
if(NOT DEFINED WIFI_PASSWORD)
    set(WIFI_PASSWORD "")
endif()
target_compile_definitions(dm441bscpi PRIVATE WIFI_SSID="${WIFI_SSID}" WIFI_PASSWORD="${WIFI_PASSWORD}")
target_include_directories(dm441bscpi PRIVATE ${CMAKE_CURRENT_LIST_DIR})
