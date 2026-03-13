cmake_minimum_required(VERSION 3.20)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(ANDROID_ARM_NEON ON CACHE BOOL "Enable NEON for armeabi-v7a" FORCE)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." "${CMAKE_CURRENT_BINARY_DIR}/wechat-qrcode-ncnn")
