# These five lines must stay in this order for the ESP-IDF build to work.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only the common components, main and what main depends on.
idf_build_set_property(MINIMAL_BUILD ON)
set(PROJECT_VER "ps2")
project(okhi)
