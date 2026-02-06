cmake_minimum_required(VERSION 3.20)
project(zzt_qrcode)

set(CMAKE_CXX_STANDARD 17)

if (WIN32)
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif ()

if (NOT TARGET ncnn)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ncnn/CMakeLists.txt")
        set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
        set(NCNN_STDIO OFF CACHE BOOL "" FORCE)
        set(NCNN_STRING OFF CACHE BOOL "" FORCE)
        set(NCNN_C_API OFF CACHE BOOL "" FORCE)
        set(NCNN_PLATFORM_API OFF CACHE BOOL "" FORCE)
        set(NCNN_DISABLE_RTTI OFF CACHE BOOL "" FORCE)
        set(NCNN_DISABLE_EXCEPTION OFF CACHE BOOL "" FORCE)
        set(NCNN_SIMPLEOCV ON CACHE BOOL "" FORCE)
        set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
        
        set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
        
        add_subdirectory(third_party/ncnn)
    else()
        message(FATAL_ERROR "NCNN submodule not found! Please run: git submodule update --init --recursive")
    endif()
endif()

file(GLOB_RECURSE wechat_qrcode_srcs src/wechat_qrcode/src/*.cpp)

add_library(zzt_qrcode SHARED
        src/qrcode.cpp
        src/qrcode_result.cpp
        ${wechat_qrcode_srcs}
)

target_include_directories(zzt_qrcode PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(zzt_qrcode PRIVATE 
    src/wechat_qrcode/include
    src
)

target_compile_definitions(zzt_qrcode PRIVATE ZZT_QRCODE_EXPORT)
target_compile_definitions(zzt_qrcode PRIVATE DETECT_USE_OPT_MODEL)
target_compile_definitions(zzt_qrcode PRIVATE SR_USE_OPT_MODEL)

target_link_libraries(zzt_qrcode PRIVATE ncnn)

if (WIN32 AND NOT MSVC)
    target_link_libraries(zzt_qrcode PRIVATE iconv)
endif ()

if (ANDROID)
    target_link_libraries(zzt_qrcode PRIVATE log)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(zzt_qrcode PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)

    if (UNIX AND NOT APPLE)
        target_link_options(zzt_qrcode PRIVATE "LINKER:--exclude-libs,ALL")
        target_link_options(zzt_qrcode PRIVATE "-s")
    endif ()

    if (WIN32 AND NOT MSVC)
        target_link_options(zzt_qrcode PRIVATE "LINKER:--exclude-all-symbols")
        target_link_options(zzt_qrcode PRIVATE "-s")
    endif ()

    if (NOT ANDROID OR CMAKE_ANDROID_NDK_VERSION)
        set_property(TARGET zzt_qrcode PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
endif ()

