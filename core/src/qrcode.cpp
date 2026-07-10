#include "zzt_qrcode/qrcode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include <exception>
#include <new>

#include "handle.h"
#include "logger.h"
#include "mat.h"
#include "opencv2/wechat_qrcode.hpp"
#include "qrcode_result.h"
#include "simpleocv.h"

namespace {

thread_local zzt::qrcode::LogContext g_log_context{};
thread_local const char* g_log_operation = "";
struct LogContextScope {
    zzt::qrcode::LogContext saved;
    const char* saved_operation;
    explicit LogContextScope(zzt::qrcode::LogContext value, const char* operation = "")
        : saved(std::move(g_log_context)), saved_operation(g_log_operation) {
        g_log_context = std::move(value);
        g_log_operation = operation;
    }
    ~LogContextScope() { g_log_context = std::move(saved); g_log_operation = saved_operation; }
};

bool can_log(zzt_qrcode_log_level_t level) {
    return zzt::qrcode::can_log(g_log_context, level);
}

void dispatch_log(zzt_qrcode_log_level_t level, const std::string& message) {
    zzt::qrcode::dispatch_log(g_log_context, g_log_operation, level, ZZT_QRCODE_OK, message);
}

struct TimingGate {
    bool enabled;
    std::chrono::steady_clock::time_point start;
    TimingGate(zzt_qrcode_log_level_t level) : enabled(can_log(level)) {
        if (enabled) start = std::chrono::steady_clock::now();
    }
    double elapsed_ms() const {
        return enabled ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() : 0.0;
    }
};

struct TotalDurationLogger {
    const char* fn;
    bool enabled;
    std::chrono::steady_clock::time_point start;
    TotalDurationLogger(const char* func, zzt_qrcode_log_level_t level) : fn(func), enabled(can_log(level)) {
        if (enabled) start = std::chrono::steady_clock::now();
    }
    ~TotalDurationLogger() {
        if (enabled) try {
            zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_VERBOSE, ZZT_QRCODE_OK,
                std::string(fn) + ": total duration: " + std::to_string(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()) + " ms");
        } catch (...) {}
    }
};

static bool is_path_decode_function(const char* fn) {
    return std::strcmp(fn, "zzt_qrcode_detect_and_decode_path_u8") == 0 ||
           std::strcmp(fn, "zzt_qrcode_detect_and_decode_path_u16") == 0;
}
static void log_exception(const char* fn, const std::exception& e) {
    try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_ERROR,
          dynamic_cast<const std::bad_alloc*>(&e) ? ZZT_QRCODE_ERROR_OUT_OF_MEMORY : ZZT_QRCODE_ERROR_INTERNAL,
          is_path_decode_function(fn) ? std::string("Exception in ") + fn : std::string("Exception in ") + fn + ": " + e.what()); } catch (...) {}
}
static void log_unknown(const char* fn) { try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_ERROR, ZZT_QRCODE_ERROR_INTERNAL, std::string("Unknown exception in ") + fn); } catch (...) {} }
static void log_warn_int(const char* fn, const char* reason, const char* name, int value) { try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, std::string("Warning in ") + fn + ": " + reason + " (" + name + "=" + std::to_string(value) + ")"); } catch (...) {} }
static void log_warn_pixels(const char* fn, const char* reason, zzt_qrcode_pixel_format_t format, int width, int height, int stride) {
    try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_ARGUMENT,
        std::string("Warning in ") + fn + ": " + reason + " (format=" + std::to_string(static_cast<int>(format)) +
        ", width=" + std::to_string(width) + ", height=" + std::to_string(height) + ", stride=" + std::to_string(stride) + ")"); } catch (...) {}
}
static void log_warn(const char* fn, const char* reason) {
    zzt_qrcode_error_t error = std::strstr(reason, "handle") ? ZZT_QRCODE_ERROR_INVALID_HANDLE :
        (std::strstr(reason, "decode failed") ? ZZT_QRCODE_ERROR_DECODE_FAILED : ZZT_QRCODE_ERROR_INVALID_ARGUMENT);
    try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_WARN, error, std::string("Warning in ") + fn + ": " + reason); } catch (...) {}
}
static void log_warn_index(const char* fn, int index, size_t count) { try { zzt::qrcode::dispatch_log(g_log_context, fn, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_INDEX, std::string("Warning in ") + fn + ": invalid result index (index=" + std::to_string(index) + ", count=" + std::to_string(count) + ")"); } catch (...) {} }

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

struct WeChatQRCode : cv::wechat_qrcode::WeChatQRCode, zzt::qrcode::Handle<WeChatQRCode, zzt_qrcode_detector_h> {
    zzt::qrcode::LogRoute route;
    uint64_t detector_id;
    explicit WeChatQRCode(zzt::qrcode::LogRoute value = {}) : route(std::move(value)), detector_id(zzt::qrcode::generate_object_id()) {}
};
struct QrcodeResultList : std::vector<std::shared_ptr<zzt::qrcode::QrcodeResult>>,
                          zzt::qrcode::Handle<QrcodeResultList, zzt_qrcode_result_h> {
    zzt::qrcode::LogRoute route;
    uint64_t detector_id = 0;
    uint64_t result_id;
    QrcodeResultList() : result_id(zzt::qrcode::generate_object_id()) {}
    QrcodeResultList(const std::vector<std::shared_ptr<zzt::qrcode::QrcodeResult>>& values,
                     zzt::qrcode::LogRoute value, uint64_t detector)
        : std::vector<std::shared_ptr<zzt::qrcode::QrcodeResult>>(values), route(std::move(value)),
          detector_id(detector), result_id(zzt::qrcode::generate_object_id()) {}
};
zzt::qrcode::LogContext detector_context(zzt_qrcode_detector_h detector) {
    auto value = WeChatQRCode::get(detector);
    return value ? zzt::qrcode::LogContext{value->route, value->detector_id, 0} : zzt::qrcode::LogContext{};
}
zzt::qrcode::LogContext result_context(zzt_qrcode_result_h result) {
    auto value = QrcodeResultList::get(result);
    return value ? zzt::qrcode::LogContext{value->route, value->detector_id, value->result_id} : zzt::qrcode::LogContext{};
}

zzt_qrcode_error_t zzt_qrcode_create_detector_with_options(
        const zzt_qrcode_detector_options_t* options, zzt_qrcode_detector_h* out_detector) {
    constexpr const char* op = "zzt_qrcode_create_detector_with_options";
    if (!out_detector) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
    *out_detector = nullptr;
    zzt::qrcode::LogRoute route;
    if (options) {
        if (options->struct_size < sizeof(*options)) {
            zzt::qrcode::dispatch_log({}, op, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, std::string("Warning in ") + op + ": invalid options struct size");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        route.custom_logger = zzt::qrcode::lookup_logger(options->logger);
        if (options->logger && !route.custom_logger) {
            zzt::qrcode::dispatch_log({}, op, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_HANDLE, std::string("Warning in ") + op + ": invalid logger handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        if ((options->log_flags & ~ZZT_QRCODE_DETECTOR_LOG_PROPAGATE_TO_RUNTIME) != 0) {
            zzt::qrcode::dispatch_log({route, 0, 0}, op, ZZT_QRCODE_LOG_LEVEL_WARN, ZZT_QRCODE_ERROR_INVALID_ARGUMENT, std::string("Warning in ") + op + ": invalid log flags");
            return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        }
        route.propagate_to_runtime = (options->log_flags & ZZT_QRCODE_DETECTOR_LOG_PROPAGATE_TO_RUNTIME) != 0;
    }
    try {
        *out_detector = WeChatQRCode::create_handle(route);
        return ZZT_QRCODE_OK;
    } catch (const std::bad_alloc&) {
        zzt::qrcode::dispatch_log({route, 0, 0}, op, ZZT_QRCODE_LOG_LEVEL_ERROR, ZZT_QRCODE_ERROR_OUT_OF_MEMORY, std::string("Exception in ") + op + ": out of memory");
        return ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
    } catch (const std::exception& e) {
        zzt::qrcode::dispatch_log({route, 0, 0}, op, ZZT_QRCODE_LOG_LEVEL_ERROR, ZZT_QRCODE_ERROR_INTERNAL, std::string("Exception in ") + op + ": " + e.what());
        return ZZT_QRCODE_ERROR_INTERNAL;
    } catch (...) {
        zzt::qrcode::dispatch_log({route, 0, 0}, op, ZZT_QRCODE_LOG_LEVEL_ERROR, ZZT_QRCODE_ERROR_INTERNAL, std::string("Unknown exception in ") + op);
        return ZZT_QRCODE_ERROR_INTERNAL;
    }
}

zzt_qrcode_detector_h zzt_qrcode_create_detector() {
    LogContextScope scope({}, "zzt_qrcode_create_detector");
    zzt_qrcode_detector_h detector = nullptr;
    return zzt_qrcode_create_detector_with_options(nullptr, &detector) == ZZT_QRCODE_OK ? detector : nullptr;
}

zzt_qrcode_error_t zzt_qrcode_release_detector(zzt_qrcode_detector_h detector) {
    LogContextScope log_scope(detector_context(detector), "zzt_qrcode_release_detector");
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
    std::vector<std::string> results;
    {
        TimingGate detect_timing(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
        results = detector_ptr->detectAndDecode(img, points);
        if (detect_timing.enabled) {
            dispatch_log(ZZT_QRCODE_LOG_LEVEL_VERBOSE,
                         std::string(fn) + ": internal detectAndDecode duration: " +
                         std::to_string(detect_timing.elapsed_ms()) + " ms, results count: " +
                         std::to_string(results.size()));
        }
    }

    size_t result_len = results.size();
    QrcodeResultList result_vector;
    if (result_len > 0) {
        result_vector.reserve(result_len);
        for (int i = 0; i < result_len; ++i) {
            result_vector.emplace_back(std::make_shared<zzt::qrcode::QrcodeResult>(results[i], points[i]));
        }
    }
    *out_result = QrcodeResultList::create_handle(result_vector, detector_ptr->route, detector_ptr->detector_id);
    return ZZT_QRCODE_OK;
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_data(zzt_qrcode_detector_h detector, const unsigned char *data, int data_len,
                                  zzt_qrcode_result_h *out_result) {
    LogContextScope log_scope(detector_context(detector), "zzt_qrcode_detect_and_decode_data");
    TotalDurationLogger total_logger("zzt_qrcode_detect_and_decode_data", ZZT_QRCODE_LOG_LEVEL_VERBOSE);
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

        cv::Mat img;
        {
            TimingGate decode_timing(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
            std::vector bytes_vector(data, data + data_len);
            img = cv::imdecode(bytes_vector, cv::IMREAD_GRAYSCALE);
            if (decode_timing.enabled) {
                dispatch_log(ZZT_QRCODE_LOG_LEVEL_VERBOSE,
                             "zzt_qrcode_detect_and_decode_data: image decode/preparation duration: " +
                             std::to_string(decode_timing.elapsed_ms()) + " ms");
            }
        }

        if (img.empty()) {
            log_warn("zzt_qrcode_detect_and_decode_data", "encoded image decode failed");
            return ZZT_QRCODE_ERROR_DECODE_FAILED;
        }

        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_data", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u8(zzt_qrcode_detector_h detector, const char8_t *path,
                                     zzt_qrcode_result_h *out_result) {
    LogContextScope log_scope(detector_context(detector), "zzt_qrcode_detect_and_decode_path_u8");
    TotalDurationLogger total_logger("zzt_qrcode_detect_and_decode_path_u8", ZZT_QRCODE_LOG_LEVEL_VERBOSE);
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
        cv::Mat img;
        {
            TimingGate load_timing(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
            img = cv::imread(fs_path.string(), cv::IMREAD_GRAYSCALE);
            if (load_timing.enabled) {
                dispatch_log(ZZT_QRCODE_LOG_LEVEL_VERBOSE,
                             "zzt_qrcode_detect_and_decode_path_u8: image load/decode duration: " +
                             std::to_string(load_timing.elapsed_ms()) + " ms");
            }
        }

        if (img.empty()) {
            log_warn("zzt_qrcode_detect_and_decode_path_u8", "path image load/decode failed");
            return ZZT_QRCODE_ERROR_DECODE_FAILED;
        }

        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_path_u8", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_path_u16(zzt_qrcode_detector_h detector, const char16_t *path,
                                      zzt_qrcode_result_h *out_result) {
    LogContextScope log_scope(detector_context(detector), "zzt_qrcode_detect_and_decode_path_u16");
    TotalDurationLogger total_logger("zzt_qrcode_detect_and_decode_path_u16", ZZT_QRCODE_LOG_LEVEL_VERBOSE);
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
        cv::Mat img;
        {
            TimingGate load_timing(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
            img = cv::imread(fs_path.string(), cv::IMREAD_GRAYSCALE);
            if (load_timing.enabled) {
                dispatch_log(ZZT_QRCODE_LOG_LEVEL_VERBOSE,
                             "zzt_qrcode_detect_and_decode_path_u16: image load/decode duration: " +
                             std::to_string(load_timing.elapsed_ms()) + " ms");
            }
        }

        if (img.empty()) {
            log_warn("zzt_qrcode_detect_and_decode_path_u16", "path image load/decode failed");
            return ZZT_QRCODE_ERROR_DECODE_FAILED;
        }

        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_path_u16", detector, img, out_result);
    });
}

zzt_qrcode_error_t
zzt_qrcode_detect_and_decode_pixels(zzt_qrcode_detector_h detector, const unsigned char *pixels,
                                    zzt_qrcode_pixel_format_t format, int width, int height, int stride,
                                    zzt_qrcode_result_h *out_result) {
    LogContextScope log_scope(detector_context(detector), "zzt_qrcode_detect_and_decode_pixels");
    TotalDurationLogger total_logger("zzt_qrcode_detect_and_decode_pixels", ZZT_QRCODE_LOG_LEVEL_VERBOSE);
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

        cv::Mat img;
        {
            TimingGate prep_timing(ZZT_QRCODE_LOG_LEVEL_VERBOSE);
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
                    // This is unreachable since pixel_stride checks format range, but keep for safety.
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

            img.create(height, width, CV_8UC1);
            ncnn_img.to_pixels(img.data, ncnn::Mat::PIXEL_GRAY);

            if (prep_timing.enabled) {
                dispatch_log(ZZT_QRCODE_LOG_LEVEL_VERBOSE,
                             "zzt_qrcode_detect_and_decode_pixels: pixel conversion/prep duration: " +
                             std::to_string(prep_timing.elapsed_ms()) + " ms (format=" +
                             std::to_string(static_cast<int>(format)) + ", width=" + std::to_string(width) +
                             ", height=" + std::to_string(height) + ", stride=" + std::to_string(stride) + ")");
            }
        }

        if (img.empty()) {
            log_warn_pixels("zzt_qrcode_detect_and_decode_pixels", "raw pixel conversion/input failure", format, width, height, stride);
            return ZZT_QRCODE_ERROR_DECODE_FAILED;
        }

        return qrcode_detect_and_decode_internal("zzt_qrcode_detect_and_decode_pixels", detector, img, out_result);
    });
}

zzt_qrcode_error_t zzt_qrcode_release_result(zzt_qrcode_result_h result) {
    LogContextScope log_scope(result_context(result), "zzt_qrcode_release_result");
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
    LogContextScope log_scope(result_context(result), "zzt_qrcode_get_result_size");
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
    LogContextScope log_scope(result_context(result), "zzt_qrcode_get_result_text");
    return catch_exceptions("zzt_qrcode_get_result_text", [&] {
        auto result_ptr = QrcodeResultList::get(result);
        if (result_ptr == nullptr) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn("zzt_qrcode_get_result_text", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        size_t result_count = result_ptr->size();
        if (index < 0 || static_cast<size_t>(index) >= result_count) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn_index("zzt_qrcode_get_result_text", index, result_count);
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
    LogContextScope log_scope(result_context(result), "zzt_qrcode_get_result_points");
    return catch_exceptions("zzt_qrcode_get_result_points", [&] {
        auto result_ptr = QrcodeResultList::get(result);
        if (result_ptr == nullptr) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn("zzt_qrcode_get_result_points", "invalid result handle");
            return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        }
        size_t result_count = result_ptr->size();
        if (index < 0 || static_cast<size_t>(index) >= result_count) {
            if (buffer_size) {
                *buffer_size = 0;
            }
            log_warn_index("zzt_qrcode_get_result_points", index, result_count);
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
