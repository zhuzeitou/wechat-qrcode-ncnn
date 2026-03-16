cmake_minimum_required(VERSION 3.20)

project(zzt_qrcode_android)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(ANDROID_ARM_NEON ON CACHE BOOL "Enable NEON for armeabi-v7a" FORCE)
endif()

set(ZZT_BUILD_ANDROID_JNI ON CACHE BOOL "Build Android JNI bridge" FORCE)

option(ZZT_USE_PREBUILT "Use prebuilt libraries if available" ON)

set(PREBUILT_ABI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${ANDROID_ABI}")
set(CORE_SO "${PREBUILT_ABI_DIR}/lib/libzzt_qrcode.so")
set(JNI_SO "${PREBUILT_ABI_DIR}/lib/libzzt_qrcode_jni.so")
set(PREBUILT_INCLUDE "${PREBUILT_ABI_DIR}/include")

set(USE_PREBUILT_CORE OFF)
set(USE_PREBUILT_JNI OFF)

if(ZZT_USE_PREBUILT)
    if(EXISTS "${CORE_SO}")
        set(USE_PREBUILT_CORE ON)
        message(STATUS "[ZZT] Found prebuilt core library: ${CORE_SO}")
        
        if(EXISTS "${JNI_SO}")
            set(USE_PREBUILT_JNI ON)
            message(STATUS "[ZZT] Found prebuilt JNI library: ${JNI_SO}")
        endif()
    else()
        message(STATUS "[ZZT] ZZT_USE_PREBUILT is ON but core library not found at ${CORE_SO}. Falling back to source build.")
    endif()
endif()

if(USE_PREBUILT_CORE)
    add_library(zzt_qrcode SHARED IMPORTED)
    set_target_properties(zzt_qrcode PROPERTIES
        IMPORTED_LOCATION "${CORE_SO}"
    )
    
    if(EXISTS "${PREBUILT_INCLUDE}")
        set_target_properties(zzt_qrcode PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PREBUILT_INCLUDE}"
        )
        message(STATUS "[ZZT] Using prebuilt headers: ${PREBUILT_INCLUDE}")
    else()
        set_target_properties(zzt_qrcode PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../core/include"
        )
        message(STATUS "[ZZT] Prebuilt include not found, using source headers: ../../core/include")
    endif()
else()
    message(STATUS "[ZZT] Building core library from source...")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core" "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

if(USE_PREBUILT_JNI)
    add_library(zzt_qrcode_jni SHARED IMPORTED)
    set_target_properties(zzt_qrcode_jni PROPERTIES
        IMPORTED_LOCATION "${JNI_SO}"
    )
    set_target_properties(zzt_qrcode_jni PROPERTIES
        INTERFACE_LINK_LIBRARIES zzt_qrcode
    )
    message(STATUS "[ZZT] Using prebuilt JNI library.")
else()
    message(STATUS "[ZZT] Building JNI bridge from source...")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../android-jni" "${CMAKE_CURRENT_BINARY_DIR}/android-jni")
endif()
