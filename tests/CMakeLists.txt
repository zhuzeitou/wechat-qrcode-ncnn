add_executable(qrcodetest main.cpp)
target_link_libraries(qrcodetest PRIVATE zzt_qrcode)

set_target_properties(qrcodetest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:zzt_qrcode>
)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(qrcodetest PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)

    if (NOT MSVC)
        target_link_options(qrcodetest PRIVATE "-s")
    endif ()
endif ()
