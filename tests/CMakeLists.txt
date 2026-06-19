add_executable(qrcodetest main.cpp)
target_link_libraries(qrcodetest PRIVATE zzt_qrcode)

add_executable(qrcode_validation_test validation.cpp)
target_link_libraries(qrcode_validation_test PRIVATE zzt_qrcode)

set_target_properties(qrcodetest qrcode_validation_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:zzt_qrcode>
    MACOSX_BUNDLE OFF
)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(qrcodetest qrcode_validation_test PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)

    if (APPLE)
        target_link_options(qrcodetest PRIVATE "LINKER:-x")
        target_link_options(qrcode_validation_test PRIVATE "LINKER:-x")
    endif ()

    if (UNIX AND NOT APPLE)
        target_link_options(qrcodetest PRIVATE "LINKER:--exclude-libs,ALL")
        target_link_options(qrcodetest PRIVATE "-s")
        target_link_options(qrcode_validation_test PRIVATE "LINKER:--exclude-libs,ALL")
        target_link_options(qrcode_validation_test PRIVATE "-s")
    endif ()

    if (WIN32 AND NOT MSVC)
        target_link_options(qrcodetest PRIVATE "LINKER:--exclude-all-symbols")
        target_link_options(qrcodetest PRIVATE "-s")
        target_link_options(qrcode_validation_test PRIVATE "LINKER:--exclude-all-symbols")
        target_link_options(qrcode_validation_test PRIVATE "-s")
    endif ()
endif ()

# --- Installation Configuration ---
include(GNUInstallDirs)

install(TARGETS qrcodetest qrcode_validation_test
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (APPLE)
    set_target_properties(qrcodetest qrcode_validation_test PROPERTIES INSTALL_RPATH "@executable_path/../lib")
elseif (UNIX AND NOT ANDROID)
    set_target_properties(qrcodetest qrcode_validation_test PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib")
endif ()
