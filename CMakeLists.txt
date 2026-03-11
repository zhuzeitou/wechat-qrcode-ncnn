cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory")
endif()

project("wechat_qrcode_ncnn")

add_subdirectory(core)

option(BUILD_ANDROID_JNI "Build Android JNI bridge library" OFF)
if (BUILD_ANDROID_JNI AND ANDROID)
    add_subdirectory(android-jni)
endif ()

if (NOT ANDROID)
    option(BUILD_TESTS "Build test programs" OFF)
    if (BUILD_TESTS)
        add_subdirectory(tests)
    endif ()
endif ()
