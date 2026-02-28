#ifndef ZZT_QRCODE_H
#define ZZT_QRCODE_H

#ifdef __cplusplus
#include <version>
#ifndef __cpp_lib_char8_t
using char8_t = unsigned char;
#endif
#else
typedef uint8_t char8_t;
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle definition
 */
struct zzt_qrcode_detector_t;
struct zzt_qrcode_result_t;
typedef struct zzt_qrcode_detector_t *zzt_qrcode_detector_h;
typedef struct zzt_qrcode_result_t *zzt_qrcode_result_h;

#ifdef _WIN32
#ifdef ZZT_QRCODE_EXPORT
#define ZZT_QRCODE_API __declspec(dllexport)
#else
#define ZZT_QRCODE_API __declspec(dllimport)
#endif
#else
#ifdef ZZT_QRCODE_EXPORT
#define ZZT_QRCODE_API __attribute__((visibility("default")))
#else
#define ZZT_QRCODE_API
#endif
#endif

/**
 * Pixel format enum
 */
typedef enum {
    ZZT_QRCODE_PIXEL_GRAY = 0,  // Single channel Gray
    ZZT_QRCODE_PIXEL_RGB = 1,   // 3 channels RGB
    ZZT_QRCODE_PIXEL_BGR = 2,   // 3 channels BGR
    ZZT_QRCODE_PIXEL_RGBA = 3,  // 4 channels RGBA
    ZZT_QRCODE_PIXEL_BGRA = 4,  // 4 channels BGRA
    ZZT_QRCODE_PIXEL_ARGB = 5,  // 4 channels ARGB
    ZZT_QRCODE_PIXEL_ABGR = 6   // 4 channels ABGR
} zzt_qrcode_pixel_format_t;

/**
 * Error code enum
 */
typedef enum {
    ZZT_QRCODE_OK = 0,                       // Success
    ZZT_QRCODE_ERROR_INVALID_HANDLE = -1,    // Invalid handle
    ZZT_QRCODE_ERROR_INVALID_INDEX = -2,     // Invalid index
    ZZT_QRCODE_ERROR_BUFFER_TOO_SMALL = -3,  // Buffer too small
    ZZT_QRCODE_ERROR_DECODE_FAILED = -4,     // Image decode failed
    ZZT_QRCODE_ERROR_INVALID_ARGUMENT = -5,  // Invalid argument (e.g. null pointer or invalid size)
    ZZT_QRCODE_ERROR_OUT_OF_MEMORY = -6,     // Out of memory
} zzt_qrcode_error_t;

/**
 * Create a QR code detector instance.
 * @return Returns the detector handle, or NULL if failed.
 */
ZZT_QRCODE_API zzt_qrcode_detector_h zzt_qrcode_create_detector();

/**
 * Release the QR code detector instance.
 * @param detector Detector handle.
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_release_detector(zzt_qrcode_detector_h detector);

/**
 * Detect and decode from image file data in memory (supports JPEG, PNG, etc.).
 * @param detector Detector handle.
 * @param data Image file data pointer (encoded image, such as JPEG/PNG format).
 * @param data_len Data length (bytes).
 * @param out_result Pointer to the output result list handle. Must be released with zzt_qrcode_release_result after use.
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid detector handle
 *         ZZT_QRCODE_ERROR_DECODE_FAILED Image decode failed
 *         ZZT_QRCODE_ERROR_INVALID_ARGUMENT Invalid argument
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_detect_and_decode_data(zzt_qrcode_detector_h detector,
                                                                  const unsigned char *data, int data_len,
                                                                  zzt_qrcode_result_h *out_result);

/**
 * Detect and decode from image file path.
 * @param detector Detector handle.
 * @param path Image file path (supports JPEG, PNG, etc.).
 * @param out_result Pointer to the output result list handle. Must be released with zzt_qrcode_release_result after use.
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid detector handle
 *         ZZT_QRCODE_ERROR_DECODE_FAILED Image file read or decode failed
 *         ZZT_QRCODE_ERROR_INVALID_ARGUMENT Invalid argument
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_detect_and_decode_path_u8(zzt_qrcode_detector_h detector,
                                                                     const char8_t *path,
                                                                     zzt_qrcode_result_h *out_result);
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_detect_and_decode_path_u16(zzt_qrcode_detector_h detector,
                                                                      const char16_t *path,
                                                                      zzt_qrcode_result_h *out_result);

/**
 * Process raw pixel data directly.
 * @param detector Detector handle.
 * @param pixels Pixel data pointer. The data will be automatically converted to grayscale for processing.
 * @param format Pixel format, supports GRAY/RGB/BGR/RGBA/BGRA.
 * @param width Image width (pixels).
 * @param height Image height (pixels).
 * @param stride Image row stride (bytes). If 0, it is automatically calculated based on width and format.
 * @param out_result Pointer to the output result list handle. Must be released with zzt_qrcode_release_result after use.
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid detector handle
 *         ZZT_QRCODE_ERROR_INVALID_ARGUMENT Invalid argument (e.g. null pointer or invalid size)
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_detect_and_decode_pixels(zzt_qrcode_detector_h detector,
                                                                    const unsigned char *pixels,
                                                                    zzt_qrcode_pixel_format_t format, int width,
                                                                    int height, int stride,
                                                                    zzt_qrcode_result_h *out_result);

/**
 * Release the result list instance.
 * @param result Result list handle.
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_release_result(zzt_qrcode_result_h result);

/**
 * Get the number of QR codes in the result list.
 * @param result Result list handle.
 * @param size Pointer to output the number of QR codes.
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid result handle
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_get_result_size(zzt_qrcode_result_h result, int *size);

/**
 * Get the QR code text content at the specified index.
 * @param result Result list handle.
 * @param index Result index (starts from 0).
 * @param output_text Output text buffer pointer. If NULL, only returns the required buffer size.
 * @param buffer_size Input/Output parameter. Input indicates the buffer size, output returns the actual required size (including the null terminator \0).
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid result handle
 *         ZZT_QRCODE_ERROR_INVALID_INDEX Index out of bounds
 *         ZZT_QRCODE_ERROR_BUFFER_TOO_SMALL Buffer too small, required size will be written to buffer_size
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_get_result_text(zzt_qrcode_result_h result, int index, char *output_text,
                                                           int *buffer_size);

/**
 * Get the QR code vertex coordinates at the specified index.
 * @param result Result list handle.
 * @param index Result index (starts from 0).
 * @param output_point Output vertex coordinate array (usually 4 points, 8 float values in total, in order x0, y0, x1, y1, ...). If NULL, only returns the required array size.
 * @param buffer_size Input/Output parameter. Input indicates array capacity (number of float elements), output returns the actual required array size (usually 8).
 * @return ZZT_QRCODE_OK Success
 *         ZZT_QRCODE_ERROR_INVALID_HANDLE Invalid result handle
 *         ZZT_QRCODE_ERROR_INVALID_INDEX Index out of bounds
 *         ZZT_QRCODE_ERROR_BUFFER_TOO_SMALL Array too small, required size will be written to buffer_size
 */
ZZT_QRCODE_API zzt_qrcode_error_t zzt_qrcode_get_result_points(zzt_qrcode_result_h result, int index, float *output_point,
                                                             int *buffer_size);

#ifdef __cplusplus
}
#endif

#endif  // ZZT_QRCODE_H
