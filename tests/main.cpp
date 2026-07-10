#include <atomic>
#include <chrono>
#include <codecvt>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "zzt_qrcode/qrcode.h"

namespace {

std::atomic<int> g_log_callback_count{0};
std::atomic<int> g_log_callback_invalid_event{0};

const char *log_level_name(int32_t level) {
    switch (static_cast<zzt_qrcode_log_level_t>(level)) {
        case ZZT_QRCODE_LOG_LEVEL_VERBOSE: return "VERBOSE";
        case ZZT_QRCODE_LOG_LEVEL_DEBUG: return "DEBUG";
        case ZZT_QRCODE_LOG_LEVEL_INFO: return "INFO";
        case ZZT_QRCODE_LOG_LEVEL_WARN: return "WARN";
        case ZZT_QRCODE_LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void ZZT_QRCODE_CALLBACK on_qrcode_log(const zzt_qrcode_log_event_t *event, void *) {
    g_log_callback_count.fetch_add(1, std::memory_order_relaxed);
    if (event == nullptr || event->message == nullptr || event->operation == nullptr) {
        g_log_callback_invalid_event.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::cerr << "[zzt_qrcode][" << log_level_name(event->level) << "] "
              << std::string(event->message, event->message_len) << std::endl;
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path> [image_path...]" << std::endl;
        return EXIT_FAILURE;
    }

    zzt_qrcode_log_sink_options_t log_options = ZZT_QRCODE_LOG_SINK_OPTIONS_INIT;
    log_options.callback = on_qrcode_log;
    log_options.min_level = ZZT_QRCODE_LOG_LEVEL_WARN;
    zzt_qrcode_log_sink_id_t log_sink = 0;
    zzt_qrcode_error_t ret_log = zzt_qrcode_add_runtime_log_sink(&log_options, &log_sink);
    if (ret_log != ZZT_QRCODE_OK) {
        std::cerr << "add runtime log sink failed with error: " << ret_log << std::endl;
        return EXIT_FAILURE;
    }

    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
#ifdef _WIN32
        int u16_size = MultiByteToWideChar(CP_ACP, 0, LPSTR(path), -1, nullptr, 0);
        std::vector<char16_t> path_u16(u16_size + 1, 0);
        MultiByteToWideChar(CP_ACP, 0, LPSTR(path), -1, LPWSTR(path_u16.data()), u16_size);
        int u8_size = WideCharToMultiByte(CP_UTF8, 0, LPWSTR(path_u16.data()), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char8_t> path_u8(u8_size + 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, LPWSTR(path_u16.data()), -1, LPSTR(path_u8.data()), u8_size, nullptr, nullptr);
#else
        int u8_size = strlen(path);
        std::vector<char8_t> path_u8(u8_size + 1, 0);
        std::copy(path, path + u8_size, path_u8.data());
#endif

        std::cout << "raw:" << path << std::endl;
        std::cout << "u8:" << reinterpret_cast<const char *>(path_u8.data()) << std::endl;

        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        zzt_qrcode_result_h result = nullptr;
        zzt_qrcode_error_t ret_detect = zzt_qrcode_detect_and_decode_path_u8(detector, path_u8.data(), &result);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << elapsed_seconds.count() << " seconds" << std::endl;

        if (ret_detect != ZZT_QRCODE_OK) {
            std::cerr << "detectAndDecode failed with error: " << ret_detect << std::endl;
            continue;
        }

        int result_size = 0;
        zzt_qrcode_error_t ret_size = zzt_qrcode_get_result_size(result, &result_size);
        std::cout << "result length: " << result_size << std::endl;
        if (ret_size == ZZT_QRCODE_OK && result_size > 0) {
            for (int j = 0; j < result_size; j++)
            {
                {
                    std::cout << "result text: ";
                    int text_buf_size = 0;
                    zzt_qrcode_error_t ret = zzt_qrcode_get_result_text(result, j, nullptr, &text_buf_size);
                    if (ret == ZZT_QRCODE_OK && text_buf_size > 0) {
                        std::vector<char> result_text(text_buf_size, 0);
                        ret = zzt_qrcode_get_result_text(result, j, result_text.data(), &text_buf_size);
                        if (ret == ZZT_QRCODE_OK && text_buf_size > 0) {
                            std::cout << result_text.data();
                        }
                    }
                    std::cout << std::endl;
                }

                {
                    std::cout << "result points: [";
                    int point_len = 0;
                    zzt_qrcode_error_t ret = zzt_qrcode_get_result_points(result, j, nullptr, &point_len);
                    if (ret == ZZT_QRCODE_OK && point_len > 0) {
                        std::vector<float> result_point(point_len, 0);
                        ret = zzt_qrcode_get_result_points(result, j, result_point.data(), &point_len);
                        if (ret == ZZT_QRCODE_OK && point_len > 0) {
                            for (int j = 0; j < point_len / 2; j++) {
                                if (j != 0) std::cout << ", ";
                                std::cout << "(" << result_point[j * 2] << ", " << result_point[j * 2 + 1] << ")";
                            }
                        }
                    }
                    std::cout << "]" << std::endl;
                }
            }
        }
        zzt_qrcode_error_t ret_release = zzt_qrcode_release_result(result);
        if (ret_release != ZZT_QRCODE_OK) {
            std::cerr << "release result failed with error: " << ret_release << std::endl;
        }
        std::cout << std::endl;
    }
    zzt_qrcode_error_t ret_release_detector = zzt_qrcode_release_detector(detector);
    if (ret_release_detector != ZZT_QRCODE_OK) {
        std::cerr << "release detector failed with error: " << ret_release_detector << std::endl;
    }

    if (g_log_callback_invalid_event.load(std::memory_order_relaxed) != 0) {
        std::cerr << "log callback validation failed: received invalid event" << std::endl;
        zzt_qrcode_remove_runtime_log_sink(log_sink);
        return EXIT_FAILURE;
    }
    std::cout << "log callback count: " << g_log_callback_count.load(std::memory_order_relaxed) << std::endl;

    ret_log = zzt_qrcode_remove_runtime_log_sink(log_sink);
    if (ret_log != ZZT_QRCODE_OK) {
        std::cerr << "remove runtime log sink failed with error: " << ret_log << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}
