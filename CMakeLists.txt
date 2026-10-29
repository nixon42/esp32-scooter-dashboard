# Root CMakeLists.txt for ESP32 Scooter Dashboard
cmake_minimum_required(VERSION 3.16)

# Include core ESP-IDF build system macros
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project name.
# This directly controls the output binary name: esp32-scooter-dashboard.elf / esp32-scooter-dashboard.bin
project(esp32-scooter-dashboard)
