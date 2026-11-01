# ESP-IDF Project Configuration
#
# Defines the minimum required CMake version,
# imports the ESP-IDF build system, and declares
# the project name.

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(tcp_serial_bridge)
