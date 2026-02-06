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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path> [image_path...]" << std::endl;
        return EXIT_FAILURE;
    }

    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
#ifdef _WIN32
        int u16_size = MultiByteToWideChar(CP_ACP, 0, LPSTR(path), -1, nullptr, 0);
        std::vector<char16_t> path_u16(u16_size + 1, 0);
        MultiByteToWideChar(CP_ACP, 0, LPSTR(path), -1, LPWSTR(&path_u16[0]), u16_size);
        int u8_size = WideCharToMultiByte(CP_UTF8, 0, LPWSTR(&path_u16[0]), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char8_t> path_u8(u8_size + 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, LPWSTR(&path_u16[0]), -1, LPSTR(&path_u8[0]), u8_size, nullptr, nullptr);
#else
        int u8_size = strlen(path);
        std::vector<char8_t> path_u8(u8_size + 1, 0);
        std::copy(path, path + u8_size, &path_u8[0]);
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
            {
                std::cout << "result text: ";
                int text_buf_size = 0;
                zzt_qrcode_error_t ret = zzt_qrcode_get_result_text(result, 0, nullptr, &text_buf_size);
                if (ret == ZZT_QRCODE_OK && text_buf_size > 0) {
                    char *result_text = new char[text_buf_size]{0};
                    ret = zzt_qrcode_get_result_text(result, 0, result_text, &text_buf_size);
                    if (ret == ZZT_QRCODE_OK && text_buf_size > 0) {
                        std::cout << result_text;
                    }
                    delete[] result_text;
                }
                std::cout << std::endl;
            }

            {
                std::cout << "result points: [";
                int point_len = 0;
                zzt_qrcode_error_t ret = zzt_qrcode_get_result_points(result, 0, nullptr, &point_len);
                if (ret == ZZT_QRCODE_OK && point_len > 0) {
                    float *result_point = new float[point_len]{0};
                    ret = zzt_qrcode_get_result_points(result, 0, result_point, &point_len);
                    if (ret == ZZT_QRCODE_OK && point_len > 0) {
                        for (int j = 0; j < point_len / 2; j++) {
                            if (j != 0) std::cout << ", ";
                            std::cout << "(" << result_point[j * 2] << ", " << result_point[j * 2 + 1] << ")";
                        }
                    }
                    delete[] result_point;
                }
                std::cout << "]" << std::endl;
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
    return 0;
}
