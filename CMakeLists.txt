cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory")
endif()

project("wechat_qrcode_ncnn")

add_subdirectory(core)

option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_ANDROID_JNI "Build Android JNI bridge library" OFF)

if (BUILD_TESTS)
    if (ANDROID)
        message(WARNING "Building tests is unsupported on Android, skipping 'tests' subdirectory")
    else ()
        add_subdirectory(tests)
    endif ()
endif ()

if (BUILD_ANDROID_JNI)
    if (NOT ANDROID)
        message(WARNING "Building Android JNI bridge library requires Android platform, skipping 'android-jni' subdirectory")
    else ()
        add_subdirectory(android-jni)
    endif ()
endif ()
