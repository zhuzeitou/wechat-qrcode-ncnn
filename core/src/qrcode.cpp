#include "zzt_qrcode/qrcode.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "handle.h"
#include "mat.h"
#include "opencv2/wechat_qrcode.hpp"
#include "qrcode_result.h"
#include "simpleocv.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

struct WeChatQRCode : cv::wechat_qrcode::WeChatQRCode, zzt::qrcode::Handle<WeChatQRCode, zzt_qrcode_detector_h> {};
struct QrcodeResultList : std::vector<std::shared_ptr<zzt::qrcode::QrcodeResult>>,
                          zzt::qrcode::Handle<QrcodeResultList, zzt_qrcode_result_h> {};

zzt_qrcode_detector_h zzt_qrcode_create_detector() { return WeChatQRCode::create_handle(); }

zzt_qrcode_error_t zzt_qrcode_release_detector(zzt_qrcode_detector_h detector) {
    if (detector == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }
    return WeChatQRCode::release_handle(detector) ? ZZT_QRCODE_OK : ZZT_QRCODE_ERROR_INVALID_HANDLE;
}

static zzt_qrcode_error_t qrcode_detect_and_decode_internal(zzt_qrcode_detector_h detector, cv::Mat &img,
                                                          zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    auto detector_ptr = WeChatQRCode::get(detector);
    if (detector_ptr == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }

    if (img.empty()) {
        return ZZT_QRCODE_ERROR_DECODE_FAILED;
    }

    std::vector<cv::Mat> points;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    auto results = detector_ptr->detectAndDecode(img, points);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_WARN, "zzt_jni", "detectAndDecode %f seconds", elapsed_seconds.count());
#else
    std::cout << "detectAndDecode " << elapsed_seconds.count() << " seconds" << std::endl;
#endif
    size_t result_len = results.size();
    QrcodeResultList result_vector;
    if (result_len > 0) {
        result_vector.reserve(result_len);
        for (int i = 0; i < result_len; ++i) {
            result_vector.emplace_back(std::make_shared<zzt::qrcode::QrcodeResult>(results[i], points[i]));
        }
    }
    *out_result = QrcodeResultList::create_handle(result_vector);
    return ZZT_QRCODE_OK;
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_data(zzt_qrcode_detector_h detector, const unsigned char *data, int data_len,
                                  zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    if (data == nullptr || data_len <= 0) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    std::vector bytes_vector(data, data + data_len);
    cv::Mat img = cv::imdecode(bytes_vector, cv::IMREAD_GRAYSCALE);
    return qrcode_detect_and_decode_internal(detector, img, out_result);
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u8(zzt_qrcode_detector_h detector, const char8_t *path,
                                     zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    if (path == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }

#ifdef __cpp_lib_char8_t
    std::filesystem::path fs_path(path);
#else
    std::filesystem::path fs_path = std::filesystem::u8path(reinterpret_cast<const char*>(path));
#endif
    std::ifstream is(fs_path, std::ios::binary);
    if (!is) {
        return ZZT_QRCODE_ERROR_DECODE_FAILED;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator(is)), std::istreambuf_iterator<char>());
    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_GRAYSCALE);
    return qrcode_detect_and_decode_internal(detector, img, out_result);
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u16(zzt_qrcode_detector_h detector, const char16_t *path,
                                      zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    if (path == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }

    std::filesystem::path fs_path(path);
    std::ifstream is(fs_path, std::ios::binary);
    if (!is) {
        return ZZT_QRCODE_ERROR_DECODE_FAILED;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator(is)), std::istreambuf_iterator<char>());
    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_GRAYSCALE);
    return qrcode_detect_and_decode_internal(detector, img, out_result);
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_pixels(zzt_qrcode_detector_h detector, const unsigned char *pixels,
                                    zzt_qrcode_pixel_format_t format, int width, int height, int stride,
                                    zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    if (pixels == nullptr || width <= 0 || height <= 0) {
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }

    ncnn::Mat::PixelType pixel_type;
    const unsigned char *actual_pixels = pixels;
    switch (format) {
        case ZZT_QRCODE_PIXEL_GRAY:
            pixel_type = ncnn::Mat::PIXEL_GRAY;
            break;
        case ZZT_QRCODE_PIXEL_RGB:
            pixel_type = ncnn::Mat::PIXEL_RGB2GRAY;
            break;
        case ZZT_QRCODE_PIXEL_BGR:
            pixel_type = ncnn::Mat::PIXEL_BGR2GRAY;
            break;
        case ZZT_QRCODE_PIXEL_RGBA:
            pixel_type = ncnn::Mat::PIXEL_RGBA2GRAY;
            break;
        case ZZT_QRCODE_PIXEL_BGRA:
            pixel_type = ncnn::Mat::PIXEL_BGRA2GRAY;
            break;
        case ZZT_QRCODE_PIXEL_ARGB:
            pixel_type = ncnn::Mat::PIXEL_RGBA2GRAY;
            actual_pixels = pixels + 1;
            break;
        case ZZT_QRCODE_PIXEL_ABGR:
            pixel_type = ncnn::Mat::PIXEL_BGRA2GRAY;
            actual_pixels = pixels + 1;
            break;
        default:
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }

    ncnn::Mat ncnn_img;
    if (stride > 0) {
        ncnn_img = ncnn::Mat::from_pixels(actual_pixels, pixel_type, width, height, stride);
    } else {
        ncnn_img = ncnn::Mat::from_pixels(actual_pixels, pixel_type, width, height);
    }

    cv::Mat img;
    img.create(height, width, CV_8UC1);
    ncnn_img.to_pixels(img.data, ncnn::Mat::PIXEL_GRAY);

    return qrcode_detect_and_decode_internal(detector, img, out_result);
}

zzt_qrcode_error_t zzt_qrcode_release_result(zzt_qrcode_result_h result) {
    if (result == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }
    return QrcodeResultList::release_handle(result) ? ZZT_QRCODE_OK : ZZT_QRCODE_ERROR_INVALID_HANDLE;
}

zzt_qrcode_error_t
zzt_qrcode_get_result_size(zzt_qrcode_result_h result, int *size) {
    auto result_ptr = QrcodeResultList::get(result);
    if (result_ptr == nullptr) {
        if (size) {
            *size = 0;
        }
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }
    if (size) {
        *size = static_cast<int>(result_ptr->size());
    }
    return ZZT_QRCODE_OK;
}

zzt_qrcode_error_t
zzt_qrcode_get_result_text(zzt_qrcode_result_h result, int index, char *output_text, int *buffer_size) {
    auto result_ptr = QrcodeResultList::get(result);
    if (result_ptr == nullptr) {
        if (buffer_size) {
            *buffer_size = 0;
        }
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }
    if (index < 0 || index >= static_cast<int>(result_ptr->size())) {
        if (buffer_size) {
            *buffer_size = 0;
        }
        return ZZT_QRCODE_ERROR_INVALID_INDEX;
    }
    const std::string &text = result_ptr->at(index)->get_text();
    size_t text_size = text.size();
    if (output_text != nullptr) {
        int provided_size = buffer_size ? *buffer_size : 0;
        if (provided_size <= static_cast<int>(text_size)) {
            if (buffer_size) {
                *buffer_size = static_cast<int>(text_size) + 1;
            }
            return ZZT_QRCODE_ERROR_BUFFER_TOO_SMALL;
        }
        text.copy(output_text, text_size);
        output_text[text_size] = '\0';
    }
    if (buffer_size) {
        *buffer_size = static_cast<int>(text_size) + 1;
    }
    return ZZT_QRCODE_OK;
}

zzt_qrcode_error_t
zzt_qrcode_get_result_points(zzt_qrcode_result_h result, int index, float *output_point, int *buffer_size) {
    auto result_ptr = QrcodeResultList::get(result);
    if (result_ptr == nullptr) {
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }
    if (index < 0 || index >= (int)result_ptr->size()) {
        return ZZT_QRCODE_ERROR_INVALID_INDEX;
    }
    const cv::Mat &result_points = result_ptr->at(index)->get_result_points();
    int len = result_points.rows;
    if (output_point != nullptr) {
        int provided_size = buffer_size ? *buffer_size : 0;
        if (provided_size < len * 2) {
            if (buffer_size) {
                *buffer_size = len * 2;
            }
            return ZZT_QRCODE_ERROR_BUFFER_TOO_SMALL;
        }
        for (int i = 0; i < len; ++i) {
            output_point[i * 2] = result_points.ptr<float>(i)[0];
            output_point[i * 2 + 1] = result_points.ptr<float>(i)[1];
        }
    }
    if (buffer_size) {
        *buffer_size = len * 2;
    }
    return ZZT_QRCODE_OK;
}
