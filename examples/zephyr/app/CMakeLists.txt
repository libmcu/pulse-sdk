cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pulse-sdk-example-zephyr)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE src/main.c)
