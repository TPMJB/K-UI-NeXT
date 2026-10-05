# SPDX-License-Identifier: MIT
# K-UI Wi-Fi firmware for the Seeed XIAO ESP32-C5 (and C6). Build with
# ESP-IDF v5.5: idf.py set-target esp32c5 build
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.2")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kui-wifi)
