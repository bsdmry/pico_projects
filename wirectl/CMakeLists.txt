cmake_minimum_required(VERSION 3.13)

# Preset by make_project.sh. It can still be overridden with -DPICO_SDK_PATH=...
set(PICO_SDK_PATH "/storage/Develop/pico_prog/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_BOARD "pico_w" CACHE STRING "Raspberry Pi Pico board")
set(CMAKE_C_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc-13.2.1")
set(CMAKE_CXX_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-g++")
include(pico_sdk_import.cmake)

project(wirectl C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(wirectl
    wirectl.c
    scpi_tcp_srv.c
    led14seg.c
)

target_link_libraries(wirectl PRIVATE
    pico_stdlib
    pico_multicore
    hardware_irq
    hardware_spi
    hardware_uart
)

# UART0 and UART1 are dedicated to the two control interfaces.  Keep debug
# stdio on USB so Core0 logging cannot corrupt controller UART traffic.
pico_enable_stdio_uart(wirectl 0)
pico_enable_stdio_usb(wirectl 1)

if(NOT DEFINED WIFI_SSID OR WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "WIFI_SSID is required for a pico_w build. Configure with -DWIFI_SSID=your_network")
endif()
if(NOT DEFINED WIFI_PASSWORD OR WIFI_PASSWORD STREQUAL "")
    message(FATAL_ERROR "WIFI_PASSWORD is required for a pico_w build. Configure with -DWIFI_PASSWORD=your_password")
endif()

target_compile_definitions(wirectl PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
)
target_include_directories(wirectl PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(wirectl PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_add_extra_outputs(wirectl)
