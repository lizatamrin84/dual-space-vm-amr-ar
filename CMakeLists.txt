cmake_minimum_required(VERSION 3.22.1)

project("dualspace-native")

add_library(
    native-bridge
    SHARED
    native-bridge.cpp
)

find_library(log-lib log)

target_link_libraries(
    native-bridge
    ${log-lib}
    dl
)