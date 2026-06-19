#include "zzt_qrcode/qrcode.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include <exception>
#include <new>

#include "handle.h"
#include "mat.h"
#include "opencv2/wechat_qrcode.hpp"
#include "qrcode_result.h"
#include "simpleocv.h"

namespace {

std::mutex g_log_callback_mutex;
zzt_qrcode_log_callback_t g_log_callback = nullptr;

bool has_log_callback() {
    std::lock_guard<std::mutex> lock(g_log_callback_mutex);
    return g_log_callback != nullptr;
}

void dispatch_log(zzt_qrcode_log_level_t level, const std::string& message) {
    zzt_qrcode_log_callback_t callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_log_callback_mutex);
        callback = g_log_callback;
    }

    if (callback == nullptr) {
        return;
    }

    try {
        callback(level, message.c_str());
    } catch (...) {
        // Logging must never change C API behavior.
    }
}

static void log_exception(const char* fn, const std::exception& e) {
    if (!has_log_callback()) {
        return;
    }
    if (std::string(fn) == "zzt_qrcode_detect_and_decode_path_u8" ||
        std::string(fn) == "zzt_qrcode_detect_and_decode_path_u16") {
        dispatch_log(ZZT_QRCODE_LOG_LEVEL_ERROR, std::string("Exception in ") + fn);
        return;
    }
    dispatch_log(ZZT_QRCODE_LOG_LEVEL_ERROR, std::string("Exception in ") + fn + ": " + e.what());
}

static void log_unknown(const char* fn) {
    if (!has_log_callback()) {
        return;
    }
    dispatch_log(ZZT_QRCODE_LOG_LEVEL_ERROR, std::string("Unknown exception in ") + fn);
}

static void log_warn(const char* fn, const char* reason) {
    if (!has_log_callback()) {
        return;
    }
    dispatch_log(ZZT_QRCODE_LOG_LEVEL_WARN, std::string("Warning in ") + fn + ": " + reason);
}

static void log_warn_int(const char* fn, const char* reason, const char* name, int value) {
    if (!has_log_callback()) {
        return;
    }
    dispatch_log(ZZT_QRCODE_LOG_LEVEL_WARN,
                 std::string("Warning in ") + fn + ": " + reason + " (" + name + "=" + std::to_string(value) + ")");
}

static void log_warn_pixels(const char* fn, const char* reason, zzt_qrcode_pixel_format_t format, int width, int height,
                            int stride) {
    if (!has_log_callback()) {
        return;
    }
    dispatch_log(ZZT_QRCODE_LOG_LEVEL_WARN,
                 std::string("Warning in ") + fn + ": " + reason + " (format=" +
                         std::to_string(static_cast<int>(format)) + ", width=" + std::to_string(width) +
                         ", height=" + std::to_string(height) + ", stride=" + std::to_string(stride) + ")");
}

static int bytes_per_pixel(zzt_qrcode_pixel_format_t format) {
    switch (format) {
        case ZZT_QRCODE_PIXEL_GRAY:
            return 1;
        case ZZT_QRCODE_PIXEL_RGB:
        case ZZT_QRCODE_PIXEL_BGR:
            return 3;
        case ZZT_QRCODE_PIXEL_RGBA:
        case ZZT_QRCODE_PIXEL_BGRA:
        case ZZT_QRCODE_PIXEL_ARGB:
        case ZZT_QRCODE_PIXEL_ABGR:
            return 4;
        default:
            return 0;
    }
}

}  // namespace

template <typename F>
static zzt_qrcode_error_t catch_exceptions(const char* fn, F&& body) {
    try {
        return body();
    } catch (const std::bad_alloc& e) {
        log_exception(fn, e);
        return ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
    } catch (const std::exception& e) {
        log_exception(fn, e);
        return ZZT_QRCODE_ERROR_INTERNAL;
    } catch (...) {
        log_unknown(fn);
        return ZZT_QRCODE_ERROR_INTERNAL;
    }
}

struct WeChatQRCode : cv::wechat_qrcode::WeChatQRCode, zzt::qrcode::Handle<WeChatQRCode, zzt_qrcode_detector_h> {};
struct QrcodeResultList : std::vector<std::shared_ptr<zzt::qrcode::QrcodeResult>>,
                          zzt::qrcode::Handle<QrcodeResultList, zzt_qrcode_result_h> {};

zzt_qrcode_error_t zzt_qrcode_set_log_callback(zzt_qrcode_log_callback_t callback) {
    try {
        std::lock_guard<std::mutex> lock(g_log_callback_mutex);
        g_log_callback = callback;
        return ZZT_QRCODE_OK;
    } catch (const std::exception& e) {
        log_exception("zzt_qrcode_set_log_callback", e);
        return ZZT_QRCODE_ERROR_INTERNAL;
    } catch (...) {
        log_unknown("zzt_qrcode_set_log_callback");
        return ZZT_QRCODE_ERROR_INTERNAL;
    }
}

zzt_qrcode_detector_h zzt_qrcode_create_detector() {
    try {
        return WeChatQRCode::create_handle();
    } catch (const std::bad_alloc &e) {
        log_exception("zzt_qrcode_create_detector", e);
        return nullptr;
    } catch (const std::exception &e) {
        log_exception("zzt_qrcode_create_detector", e);
        return nullptr;
    } catch (...) {
        log_unknown("zzt_qrcode_create_detector");
        return nullptr;
    }
}

zzt_qrcode_error_t zzt_qrcode_release_detector(zzt_qrcode_detector_h detector) {
    return catch_exceptions("zzt_qrcode_release_detector", [&] {
        if (detector == nullptr) {
            log_warn("zzt_qrcode_release_detector", "invalid detector handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        if (!WeChatQRCode::release_handle(detector)) {
            log_warn("zzt_qrcode_release_detector", "invalid detector handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        return ZZT_QRCODE_OK;
    });
}

static zzt_qrcode_error_t qrcode_detect_and_decode_internal(const char* fn, zzt_qrcode_detector_h detector, cv::Mat &img,
                                                           zzt_qrcode_result_h *out_result) {
    if (out_result == nullptr) {
        log_warn(fn, "null out_result pointer");
        return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    }
    *out_result = nullptr;

    auto detector_ptr = WeChatQRCode::get(detector);
    if (detector_ptr == nullptr) {
        log_warn(fn, "invalid detector handle");
        return ZZT_QRCODE_ERROR_INVALID_HANDLE;
    }

    if (img.empty()) {
        log_warn(fn, "image decode failed");
        return ZZT_QRCODE_ERROR_DECODE_FAILED;
    }

    std::vector<cv::Mat> points;
    auto results = detector_ptr->detectAndDecode(img, points);
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
    return catch_exceptions("zzt_qrcode_detect_and_decode_data", [&] {
        if (out_result == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_data", "null out_result pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        *out_result = nullptr;

        if (data == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_data", "null data pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        if (data_len <= 0) {
            log_warn_int("zzt_qrcode_detect_and_decode_data", "invalid data length", "data_len", data_len);
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        std::vector bytes_vector(data, data + data_len);
        cv::Mat img = cv::imdecode(bytes_vector, cv::IMREAD_GRAYSCALE);
        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_data", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u8(zzt_qrcode_detector_h detector, const char8_t *path,
                                     zzt_qrcode_result_h *out_result) {
    return catch_exceptions("zzt_qrcode_detect_and_decode_path_u8", [&] {
        if (out_result == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_path_u8", "null out_result pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        *out_result = nullptr;

        if (path == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_path_u8", "null path pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }

#ifdef __cpp_lib_char8_t
        std::filesystem::path fs_path(path);
#else
        std::filesystem::path fs_path = std::filesystem::u8path(reinterpret_cast<const char*>(path));
#endif
        cv::Mat img = cv::imread(fs_path.string(), cv::IMREAD_GRAYSCALE);
        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_path_u8", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u16(zzt_qrcode_detector_h detector, const char16_t *path,
                                      zzt_qrcode_result_h *out_result) {
    return catch_exceptions("zzt_qrcode_detect_and_decode_path_u16", [&] {
        if (out_result == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_path_u16", "null out_result pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        *out_result = nullptr;

        if (path == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_path_u16", "null path pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }

        std::filesystem::path fs_path(path);
        cv::Mat img = cv::imread(fs_path.string(), cv::IMREAD_GRAYSCALE);
        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_path_u16", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_pixels(zzt_qrcode_detector_h detector, const unsigned char *pixels,
                                    zzt_qrcode_pixel_format_t format, int width, int height, int stride,
                                    zzt_qrcode_result_h *out_result) {
    return catch_exceptions("zzt_qrcode_detect_and_decode_pixels", [&] {
        if (out_result == nullptr) {
            log_warn("zzt_qrcode_detect_and_decode_pixels", "null out_result pointer");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        *out_result = nullptr;

        if (pixels == nullptr) {
            log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "null pixels pointer", format, width, height, stride);
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        if (width <= 0 || height <= 0) {
            log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "invalid dimensions", format, width, height, stride);
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }

        int pixel_stride = bytes_per_pixel(format);
        if (pixel_stride == 0) {
            log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "unsupported pixel format", format, width, height, stride);
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        if (stride > 0 && static_cast<long long>(stride) < static_cast<long long>(width) * pixel_stride) {
            log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "invalid stride", format, width, height, stride);
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
                log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "unsupported pixel format", format, width, height,
                                stride);
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

        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_pixels", detector, img, out_result);
    });
}

zzt_qrcode_error_t zzt_qrcode_release_result(zzt_qrcode_result_h result) {
    return catch_exceptions("zzt_qrcode_release_result", [&] {
        if (result == nullptr) {
            log_warn("zzt_qrcode_release_result", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        if (!QrcodeResultList::release_handle(result)) {
            log_warn("zzt_qrcode_release_result", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        return ZZT_QRCODE_OK;
    });
}

zzt_qrcode_error_t
zzt_qrcode_get_result_size(zzt_qrcode_result_h result, int *size) {
    return catch_exceptions("zzt_qrcode_get_result_size", [&] {
        auto result_ptr = QrcodeResultList::get(result);
        if (result_ptr == nullptr) {
            if (size) {
                *size = 0;
            }
            log_warn("zzt_qrcode_get_result_size", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        if (size) {
            *size = static_cast<int>(result_ptr->size());
        }
        return ZZT_QRCODE_OK;
    });
}

zzt_qrcode_error_t
zzt_qrcode_get_result_text(zzt_qrcode_result_h result, int index, char *output_text, int *buffer_size) {
    return catch_exceptions("zzt_qrcode_get_result_text", [&] {
        auto result_ptr = QrcodeResultList::get(result);
        if (result_ptr == nullptr) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn("zzt_qrcode_get_result_text", "invalid result handle");
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
    });
}

zzt_qrcode_error_t
zzt_qrcode_get_result_points(zzt_qrcode_result_h result, int index, float *output_point, int *buffer_size) {
    return catch_exceptions("zzt_qrcode_get_result_points", [&] {
        auto result_ptr = QrcodeResultList::get(result);
        if (result_ptr == nullptr) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn("zzt_qrcode_get_result_points", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        if (index < 0 || index >= (int)result_ptr->size()) {
            if (buffer_size) {
                *buffer_size = 0;
            }
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
    });
}
