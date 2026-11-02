cmake_minimum_required(VERSION 3.13)

# Preset by make_project.sh. It can still be overridden with -DPICO_SDK_PATH=...
set(PICO_SDK_PATH "/storage/Develop/pico_prog/pico-sdk" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_BOARD "pico" CACHE STRING "Raspberry Pi Pico board")
set(CMAKE_C_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc-13.2.1")
set(CMAKE_CXX_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-g++")
include(pico_sdk_import.cmake)

project(dm441bscpi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(dm441bscpi
    dm441bscpi.c
)

target_link_libraries(dm441bscpi PRIVATE
    pico_stdlib
)

pico_add_extra_outputs(dm441bscpi)
