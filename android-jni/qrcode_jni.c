#include <jni.h>
#include <malloc.h>
#include <string.h>
#include <threads.h>

#include "zzt_qrcode/qrcode.h"

static thread_local zzt_qrcode_error_t last_error = ZZT_QRCODE_OK;

/**
 * Convert standard UTF-8 to UTF-16 (BMP + Surrogate Pairs)
 * Insert 0xFFFD character when handling invalid sequences, and resync as much as possible.
 * @param src UTF-8 input byte stream
 * @param src_len Input length (bytes)
 * @param dst UTF-16 output buffer
 * @param dst_max Maximum code units in output buffer
 * @return Number of uint16_t written to dst
 */
static int utf8_to_utf16(const char8_t *src, int src_len, char16_t *dst, int dst_max) {
    if (!src || src_len <= 0 || !dst || dst_max <= 0) return 0;

    int i = 0;
    int out = 0;
    while (i < src_len && out < dst_max) {
        char32_t cp = 0;
        int len = 0;
        unsigned char b = src[i];

        if (b == 0) {
            break;
        } else if (b < 0x80) {
            cp = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            len = 4;
        } else {
            // Invalid start byte
            dst[out++] = 0xFFFD;
            i++;
            continue;
        }

        if (i + len > src_len) {
            // Incomplete sequence
            dst[out++] = 0xFFFD;
            break;
        }

        // Concatenate continuation bytes
        int valid = 1;
        for (int j = 1; j < len; j++) {
            if ((src[i + j] & 0xC0) != 0x80) {
                valid = 0;
                break;
            }
            cp = (cp << 6) | (src[i + j] & 0x3F);
        }

        if (!valid) {
            dst[out++] = 0xFFFD;
            i++; // Skip only the invalid start byte, try to resync at next byte
            continue;
        }

        // Check for overlong encoding and invalid code points
        if ((len == 2 && cp < 0x80) ||
            (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) ||
            (cp >= 0xD800 && cp <= 0xDFFF) ||
            (cp > 0x10FFFF)) {
            dst[out++] = 0xFFFD;
            i += len;
            continue;
        }

        // Write result
        if (cp <= 0xFFFF) {
            dst[out++] = (uint16_t) cp;
        } else {
            // Surrogate pair needed
            if (out + 1 < dst_max) {
                cp -= 0x10000;
                dst[out++] = (uint16_t) (0xD800 | (cp >> 10));
                dst[out++] = (uint16_t) (0xDC00 | (cp & 0x3FF));
            } else {
                // Insufficient space for surrogate pair, stop conversion
                break;
            }
        }
        i += len;
    }
    return out;
}

jint zzt_qrcode_get_last_error_jni(JNIEnv *env, jclass clazz) {
    return (jint) last_error;
}

jlong zzt_qrcode_create_detector_jni(JNIEnv *env, jclass clazz) {
    return (jlong) zzt_qrcode_create_detector();
}

void zzt_qrcode_release_detector_jni(JNIEnv *env, jclass clazz, jlong native_detector) {
    last_error = zzt_qrcode_release_detector((zzt_qrcode_detector_h) native_detector);
}

jlong zzt_qrcode_detect_and_decode_path_jni(JNIEnv *env, jclass clazz, jlong native_detector, jstring path) {
    if (path == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    const jchar *path_chars = (*env)->GetStringChars(env, path, NULL);
    if (path_chars == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    zzt_qrcode_result_h result = NULL;
    last_error = zzt_qrcode_detect_and_decode_path_u16(
            (zzt_qrcode_detector_h) native_detector,
            (const char16_t *) path_chars,
            &result);
    (*env)->ReleaseStringChars(env, path, path_chars);
    return (jlong) result;
}

jlong zzt_qrcode_detect_and_decode_data_jni(JNIEnv *env, jclass clazz, jlong native_detector, jbyteArray data) {
    if (data == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
    jsize bytes_len = (*env)->GetArrayLength(env, data);
    zzt_qrcode_result_h result = NULL;
    last_error = zzt_qrcode_detect_and_decode_data(
            (zzt_qrcode_detector_h) native_detector,
            (const unsigned char *) bytes,
            (int) bytes_len, &result);
    (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    return (jlong) result;
}

static zzt_qrcode_result_h
zzt_qrcode_detect_and_decode_pixels_jni(zzt_qrcode_detector_h detector, unsigned char *pixels, int pixel_len,
                                        zzt_qrcode_pixel_format_t format, int width, int height, int stride) {
    if (width <= 0 || height <= 0) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    int bpp = 1;
    switch (format) {
        case ZZT_QRCODE_PIXEL_GRAY:
            bpp = 1;
            break;
        case ZZT_QRCODE_PIXEL_RGB:
        case ZZT_QRCODE_PIXEL_BGR:
            bpp = 3;
            break;
        case ZZT_QRCODE_PIXEL_RGBA:
        case ZZT_QRCODE_PIXEL_BGRA:
        case ZZT_QRCODE_PIXEL_ARGB:
        case ZZT_QRCODE_PIXEL_ABGR:
            bpp = 4;
            break;
    }

    int row_bytes = stride <= 0 ? (width * bpp) : stride;

    if (row_bytes * height > pixel_len) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return NULL;
    }

    zzt_qrcode_result_h result = NULL;
    last_error = zzt_qrcode_detect_and_decode_pixels(detector, pixels, format, width, height, stride, &result);

    return result;
}

jlong zzt_qrcode_detect_and_decode_pixels_byte_jni(JNIEnv *env, jclass clazz, jlong native_detector, jbyteArray data,
                                                   jint format, jint width, jint height, jint stride) {
    if (data == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    jsize data_len = (*env)->GetArrayLength(env, data);
    jbyte *pixels = (*env)->GetByteArrayElements(env, data, NULL);

    if (pixels == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    zzt_qrcode_result_h result = zzt_qrcode_detect_and_decode_pixels_jni((zzt_qrcode_detector_h) native_detector,
                                                                         (unsigned char *) pixels, (int) data_len,
                                                                         format, width, height, stride);

    (*env)->ReleaseByteArrayElements(env, data, pixels, JNI_ABORT);

    return (jlong) result;
}

jlong zzt_qrcode_detect_and_decode_pixels_int_jni(JNIEnv *env, jclass clazz, jlong native_detector, jintArray data,
                                                  jint format, jint width, jint height, jint stride) {
    if (data == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    jsize data_len = (*env)->GetArrayLength(env, data);
    jint *pixels = (*env)->GetIntArrayElements(env, data, NULL);

    if (pixels == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    zzt_qrcode_result_h result = zzt_qrcode_detect_and_decode_pixels_jni((zzt_qrcode_detector_h) native_detector,
                                                                         (unsigned char *) pixels, (int) data_len * 4,
                                                                         format, width, height, stride);

    (*env)->ReleaseIntArrayElements(env, data, pixels, JNI_ABORT);

    return (jlong) result;
}

void zzt_qrcode_release_result_jni(JNIEnv *env, jclass clazz, jlong native_result) {
    last_error = zzt_qrcode_release_result((zzt_qrcode_result_h) native_result);
}

int zzt_qrcode_get_result_size_jni(JNIEnv *env, jclass clazz, jlong native_result) {
    int size = 0;
    last_error = zzt_qrcode_get_result_size((zzt_qrcode_result_h) native_result, &size);
    return size;
}

jstring zzt_qrcode_get_result_text_jni(JNIEnv *env, jclass clazz, jlong native_result, jint index) {
    jstring result = NULL;

    int len = 0;
    last_error = zzt_qrcode_get_result_text((zzt_qrcode_result_h) native_result, index, NULL, &len);
    if (last_error == ZZT_QRCODE_OK && len > 0) {
        char *text = malloc(sizeof(char) * len);
        if (text == NULL) {
            last_error = ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
        memset(text, 0, sizeof(char) * len);
        last_error = zzt_qrcode_get_result_text((zzt_qrcode_result_h) native_result, index, text, &len);
        if (last_error == ZZT_QRCODE_OK && len > 0) {
            const char8_t *text_u8 = (const char8_t *) text;
            char16_t *text_u16 = malloc(sizeof(char16_t) * len);
            if (text_u16 == NULL) {
                last_error = ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
            } else {
                int text_u16_len = utf8_to_utf16(text_u8, len, text_u16, len);
                result = (*env)->NewString(env, text_u16, text_u16_len);
            }
            free(text_u16);
        }
        free(text);
    }

    return result;
}

jobjectArray
zzt_qrcode_get_result_points_jni(JNIEnv *env, jclass clazz, jlong native_result, jint index) {
    jobjectArray points_array = NULL;

    int len = 0;
    last_error = zzt_qrcode_get_result_points((zzt_qrcode_result_h) native_result, index, NULL, &len);
    if (last_error == ZZT_QRCODE_OK && len > 0) {
        float *points = malloc(sizeof(float) * len);
        if (points == NULL) {
            last_error = ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
        memset(points, 0, sizeof(float) * len);
        last_error = zzt_qrcode_get_result_points((zzt_qrcode_result_h) native_result, index, points, &len);
        if (last_error == ZZT_QRCODE_OK && len > 0) {
            jclass cls = (*env)->FindClass(env, "[F");
            points_array = (*env)->NewObjectArray(env, (jsize) len / 2, cls, NULL);
            (*env)->DeleteLocalRef(env, cls);
            for (int i = 0; i < len / 2; ++i) {
                jfloatArray point_jarr = (*env)->NewFloatArray(env, 2);
                (*env)->SetFloatArrayRegion(env, point_jarr, 0, 2, &points[i * 2]);
                (*env)->SetObjectArrayElement(env, points_array, i, point_jarr);
                (*env)->DeleteLocalRef(env, point_jarr);
            }
        }
        free(points);
    }

    return points_array;
}

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = NULL;
    jint ret = (*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6);
    if (ret != JNI_OK) {
        return JNI_ERR;
    }
    jclass cls = (*env)->FindClass(env, "xyz/zhuzeitou/qrcode/NativeLib");
    if (cls == NULL) {
        return JNI_ERR;
    }
    JNINativeMethod methods[] = {
            {"createDetector",        "()J",                    (void *) zzt_qrcode_create_detector_jni},
            {"releaseDetector",       "(J)V",                   (void *) zzt_qrcode_release_detector_jni},
            {"detectAndDecodePath",   "(JLjava/lang/String;)J", (void *) zzt_qrcode_detect_and_decode_path_jni},
            {"detectAndDecodeData",   "(J[B)J",                 (void *) zzt_qrcode_detect_and_decode_data_jni},
            {"detectAndDecodePixels", "(J[BIIII)J",             (void *) zzt_qrcode_detect_and_decode_pixels_byte_jni},
            {"detectAndDecodePixels", "(J[IIIII)J",             (void *) zzt_qrcode_detect_and_decode_pixels_int_jni},
            {"releaseResult",         "(J)V",                   (void *) zzt_qrcode_release_result_jni},
            {"getResultSize",         "(J)I",                   (void *) zzt_qrcode_get_result_size_jni},
            {"getResultText",         "(JI)Ljava/lang/String;", (void *) zzt_qrcode_get_result_text_jni},
            {"getResultPoints",       "(JI)[[F",                (void *) zzt_qrcode_get_result_points_jni},
            {"getLastError",          "()I",                    (void *) zzt_qrcode_get_last_error_jni},
    };
    ret = (*env)->RegisterNatives(env, cls, methods, sizeof(methods) / sizeof(methods[0]));
    (*env)->DeleteLocalRef(env, cls);
    if (ret != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM *vm, void *reserved) {

}
