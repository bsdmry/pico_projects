#!/bin/sh

set -eu

usage() {
    echo "Usage: $0 PROJECT_NAME [pico|pico_w]" >&2
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 2
fi

project_name=$1
board=${2:-pico}

case "$project_name" in
    ''|[0-9]*|*[!A-Za-z0-9_]*)
        echo "Error: PROJECT_NAME must be a valid C identifier (letters, digits, and underscores; not starting with a digit)." >&2
        exit 2
        ;;
esac

case "$board" in
    pico|pico_w) ;;
    *)
        echo "Error: unsupported board '$board'. Choose 'pico' or 'pico_w'." >&2
        exit 2
        ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk_path=$(CDPATH= cd -- "$script_dir/../pico-sdk" && pwd)
project_dir=$project_name

if [ -e "$project_dir" ]; then
    echo "Error: '$project_dir' already exists; refusing to overwrite it." >&2
    exit 1
fi

mkdir "$project_dir"
cp "$script_dir/pico_sdk_import.cmake" "$project_dir/"

cat > "$project_dir/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.13)

# Preset by make_project.sh. It can still be overridden with -DPICO_SDK_PATH=...
set(PICO_SDK_PATH "$sdk_path" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_BOARD "$board" CACHE STRING "Raspberry Pi Pico board")
set(CMAKE_C_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc-13.2.1")
set(CMAKE_CXX_COMPILER "/storage/Develop/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-g++")
include(pico_sdk_import.cmake)

project($project_name C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable($project_name
    $project_name.c
)

target_link_libraries($project_name PRIVATE
    pico_stdlib
)
EOF

if [ "$board" = pico_w ]; then
    cp "$script_dir/lwipopts_examples_common.h" "$project_dir/lwipopts.h"
    cat >> "$project_dir/CMakeLists.txt" <<EOF

if(NOT DEFINED WIFI_SSID OR WIFI_SSID STREQUAL "")
    message(FATAL_ERROR "WIFI_SSID is required for a pico_w build. Configure with -DWIFI_SSID=your_network")
endif()
if(NOT DEFINED WIFI_PASSWORD OR WIFI_PASSWORD STREQUAL "")
    message(FATAL_ERROR "WIFI_PASSWORD is required for a pico_w build. Configure with -DWIFI_PASSWORD=your_password")
endif()

target_compile_definitions($project_name PRIVATE
    WIFI_SSID=\"\${WIFI_SSID}\"
    WIFI_PASSWORD=\"\${WIFI_PASSWORD}\"
)
target_include_directories($project_name PRIVATE
    \${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries($project_name PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background
)
EOF
fi

cat >> "$project_dir/CMakeLists.txt" <<EOF

pico_add_extra_outputs($project_name)
EOF

cat > "$project_dir/$project_name.h" <<EOF
#ifndef ${project_name}_H
#define ${project_name}_H

#endif /* ${project_name}_H */
EOF

cat > "$project_dir/$project_name.c" <<EOF
#include "pico/stdlib.h"
#include "$project_name.h"

int main(void)
{
    stdio_init_all();

    while (true) {
        tight_loop_contents();
    }
}
EOF

echo "Created '$project_dir' for board '$board'."
echo "Build it with: cmake -S '$project_dir' -B '$project_dir/build'"
if [ "$board" = pico_w ]; then
    echo "Wi-Fi credentials are required: -DWIFI_SSID=... -DWIFI_PASSWORD=..."
fi
