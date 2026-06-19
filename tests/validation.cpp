#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include "zzt_qrcode/qrcode.h"

#define EXPECT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (File: " << __FILE__ << ", Line: " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)

#define EXPECT_EQ(val1, val2, msg) \
    do { \
        if ((val1) != (val2)) { \
            std::cerr << "FAIL: " << msg << " (Expected: " << (val2) << ", Got: " << (val1) << ") (File: " << __FILE__ << ", Line: " << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)

std::vector<std::pair<zzt_qrcode_log_level_t, std::string>> g_captured_logs;

void test_log_callback(zzt_qrcode_log_level_t level, const char* message) {
    if (message) {
        g_captured_logs.emplace_back(level, std::string(message));
    }
}

bool has_substring(const std::string& str, const std::string& sub) {
    return str.find(sub) != std::string::npos;
}

int main() {
    std::cout << "Starting log validation tests..." << std::endl;

    // Install callback
    zzt_qrcode_error_t err = zzt_qrcode_set_log_callback(test_log_callback);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Install log callback failed");

    // Create detector
    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    EXPECT_TRUE(detector != nullptr, "Create detector returned null");

    // 1. Default WARN suppresses VERBOSE timing.
    // By default, level is WARN.
    g_captured_logs.clear();
    std::vector<std::pair<zzt_qrcode_log_level_t, std::string>> privacy_logs;
    std::vector<unsigned char> pixels(100, 128); // 10x10 gray image
    zzt_qrcode_result_h result = nullptr;
    err = zzt_qrcode_detect_and_decode_pixels(detector, pixels.data(), ZZT_QRCODE_PIXEL_GRAY, 10, 10, 10, &result);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "detect_and_decode_pixels failed");
    if (result) {
        zzt_qrcode_release_result(result);
        result = nullptr;
    }
    // Verify no VERBOSE timing logs captured under default WARN
    for (const auto& log : g_captured_logs) {
        EXPECT_TRUE(log.first != ZZT_QRCODE_LOG_LEVEL_VERBOSE, "Captured VERBOSE log under default WARN");
        EXPECT_TRUE(log.first != ZZT_QRCODE_LOG_LEVEL_INFO, "Captured INFO log under default WARN");
    }
    std::cout << "Test 1 passed (default WARN suppresses VERBOSE timing)" << std::endl;

    // 2. Setting VERBOSE enables timing logs using synthetic non-sensitive input.
    err = zzt_qrcode_set_log_level(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Set log level VERBOSE failed");

    g_captured_logs.clear();
    err = zzt_qrcode_detect_and_decode_pixels(detector, pixels.data(), ZZT_QRCODE_PIXEL_GRAY, 10, 10, 10, &result);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "detect_and_decode_pixels failed under VERBOSE");
    if (result) {
        zzt_qrcode_release_result(result);
        result = nullptr;
    }

    // Verify that we captured VERBOSE timings
    bool found_prep_timing = false;
    bool found_detect_timing = false;
    bool found_total_timing = false;
    for (const auto& log : g_captured_logs) {
        if (log.first == ZZT_QRCODE_LOG_LEVEL_VERBOSE) {
            if (has_substring(log.second, "pixel conversion/prep duration")) {
                found_prep_timing = true;
            }
            if (has_substring(log.second, "internal detectAndDecode duration")) {
                found_detect_timing = true;
            }
            if (has_substring(log.second, "zzt_qrcode_detect_and_decode_pixels: total duration")) {
                found_total_timing = true;
            }
        }
    }
    EXPECT_TRUE(found_prep_timing, "Did not find pixel conversion/prep timing log");
    EXPECT_TRUE(found_detect_timing, "Did not find internal detectAndDecode timing log");
    EXPECT_TRUE(found_total_timing, "Did not find total duration timing log");
    std::cout << "Test 2 passed (VERBOSE enables timing logs)" << std::endl;

    // 3. Invalid level returns INVALID_ARGUMENT.
    // Setting invalid level (e.g. 5 or -1) should fail.
    // Also warn only if the current gate allows WARN.
    
    // Set to WARN first to check warning behavior
    err = zzt_qrcode_set_log_level(ZZT_QRCODE_LOG_LEVEL_WARN);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Set log level WARN failed");
    
    g_captured_logs.clear();
    err = zzt_qrcode_set_log_level(static_cast<zzt_qrcode_log_level_t>(5));
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "Expected INVALID_ARGUMENT for level 5");
    bool found_invalid_warning = false;
    for (const auto& log : g_captured_logs) {
        if (log.first == ZZT_QRCODE_LOG_LEVEL_WARN && has_substring(log.second, "invalid log level")) {
            found_invalid_warning = true;
        }
    }
    EXPECT_TRUE(found_invalid_warning, "Did not find invalid log level warning under WARN gate");

    err = zzt_qrcode_set_log_level(static_cast<zzt_qrcode_log_level_t>(-1));
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "Expected INVALID_ARGUMENT for level -1");

    // Set to ERROR, setting invalid level should NOT produce warning because WARN is suppressed.
    err = zzt_qrcode_set_log_level(ZZT_QRCODE_LOG_LEVEL_ERROR);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Set log level ERROR failed");
    g_captured_logs.clear();
    err = zzt_qrcode_set_log_level(static_cast<zzt_qrcode_log_level_t>(5));
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "Expected INVALID_ARGUMENT for level 5");
    for (const auto& log : g_captured_logs) {
        EXPECT_TRUE(!has_substring(log.second, "invalid log level"), "Warning log emitted under ERROR gate");
    }

    std::cout << "Test 3 passed (invalid level returns INVALID_ARGUMENT and respects warning gate)" << std::endl;

    // 4. WARN still emits at default WARN.
    err = zzt_qrcode_set_log_level(ZZT_QRCODE_LOG_LEVEL_WARN);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Set log level WARN failed");

    g_captured_logs.clear();
    // Trigger a warning with null pixels pointer
    err = zzt_qrcode_detect_and_decode_pixels(detector, nullptr, ZZT_QRCODE_PIXEL_GRAY, 10, 10, 10, &result);
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "Expected INVALID_ARGUMENT for null pixels");
    bool found_warn = false;
    for (const auto& log : g_captured_logs) {
        if (log.first == ZZT_QRCODE_LOG_LEVEL_WARN) {
            found_warn = true;
        }
    }
    EXPECT_TRUE(found_warn, "Warning was not emitted at default WARN level");
    std::cout << "Test 4 passed (WARN still emits at default WARN)" << std::endl;

    // 5. Privacy sentinels absent from captured logs, and total-duration logs are present on failure.
    err = zzt_qrcode_set_log_level(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Set log level VERBOSE failed");

    g_captured_logs.clear();

    // 5.1 Test u8 path with sentinel
    const char* path_sentinel = "SENTINEL_PATH_xyz_123_SECRET";
    err = zzt_qrcode_detect_and_decode_path_u8(detector, reinterpret_cast<const char8_t*>(path_sentinel), &result);
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_DECODE_FAILED, "Expected DECODE_FAILED for invalid path");

    // Check that we logged the total duration for the failed path call
    bool found_failed_path_total_duration = false;
    for (const auto& log : g_captured_logs) {
        if (log.first == ZZT_QRCODE_LOG_LEVEL_VERBOSE &&
            has_substring(log.second, "zzt_qrcode_detect_and_decode_path_u8: total duration")) {
            found_failed_path_total_duration = true;
        }
    }
    EXPECT_TRUE(found_failed_path_total_duration, "Did not find total duration log for failed path call");
    privacy_logs.insert(privacy_logs.end(), g_captured_logs.begin(), g_captured_logs.end());

    // 5.2 Test data with sentinel
    g_captured_logs.clear();
    const char* data_sentinel = "SENTINEL_BYTES_abc_789_TOP_SECRET";
    int data_len = static_cast<int>(std::strlen(data_sentinel));
    err = zzt_qrcode_detect_and_decode_data(detector, reinterpret_cast<const unsigned char*>(data_sentinel), data_len, &result);
    EXPECT_EQ(err, ZZT_QRCODE_ERROR_DECODE_FAILED, "Expected DECODE_FAILED for invalid data");

    // Check that we logged the total duration for the failed data call
    bool found_failed_data_total_duration = false;
    for (const auto& log : g_captured_logs) {
        if (log.first == ZZT_QRCODE_LOG_LEVEL_VERBOSE &&
            has_substring(log.second, "zzt_qrcode_detect_and_decode_data: total duration")) {
            found_failed_data_total_duration = true;
        }
    }
    EXPECT_TRUE(found_failed_data_total_duration, "Did not find total duration log for failed data call");
    privacy_logs.insert(privacy_logs.end(), g_captured_logs.begin(), g_captured_logs.end());

    // 5.3 Test get_result_text invalid index warning
    g_captured_logs.clear();
    err = zzt_qrcode_detect_and_decode_pixels(detector, pixels.data(), ZZT_QRCODE_PIXEL_GRAY, 10, 10, 10, &result);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "detect_and_decode_pixels failed");
    EXPECT_TRUE(result != nullptr, "Expected non-null result handle");
    
    // Preserve original sentinel string before passing mutable buffer size into API
    const std::string original_buffer_size_sentinel_str = "999999";
    int buffer_size_sentinel = 999999;
    zzt_qrcode_error_t text_err = zzt_qrcode_get_result_text(result, 10, nullptr, &buffer_size_sentinel);
    EXPECT_EQ(text_err, ZZT_QRCODE_ERROR_INVALID_INDEX, "Expected INVALID_INDEX for out of bounds index");
    
    // Clean up result
    zzt_qrcode_release_result(result);
    privacy_logs.insert(privacy_logs.end(), g_captured_logs.begin(), g_captured_logs.end());

    // Assert absence of sentinels across path, data, pixel, and result-index logs.
    for (const auto& log : privacy_logs) {
        EXPECT_TRUE(!has_substring(log.second, path_sentinel), "Log leaked path sentinel!");
        EXPECT_TRUE(!has_substring(log.second, data_sentinel), "Log leaked data sentinel!");
        EXPECT_TRUE(!has_substring(log.second, original_buffer_size_sentinel_str), "Log leaked buffer size sentinel!");
    }

    std::cout << "Test 5 passed (privacy sentinels absent from captured logs and failed duration logs present)" << std::endl;

    // Cleanup
    err = zzt_qrcode_release_detector(detector);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Release detector failed");

    err = zzt_qrcode_set_log_callback(nullptr);
    EXPECT_EQ(err, ZZT_QRCODE_OK, "Clear log callback failed");

    std::cout << "All validation tests passed successfully!" << std::endl;
    return 0;
}
