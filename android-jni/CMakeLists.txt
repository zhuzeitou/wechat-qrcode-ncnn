set(CMAKE_C_STANDARD 11)

add_library(zzt_qrcode_jni SHARED qrcode_jni.c)

target_link_libraries(zzt_qrcode_jni PRIVATE zzt_qrcode log)

set_target_properties(zzt_qrcode_jni PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:zzt_qrcode>
)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(zzt_qrcode_jni PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    target_link_options(zzt_qrcode_jni PRIVATE "LINKER:--exclude-libs,ALL")
    target_link_options(zzt_qrcode_jni PRIVATE "-s")
endif ()
