#include <jni.h>
#include <malloc.h>
#include <string.h>
#include <threads.h>

#include "zzt_qrcode/qrcode.h"

static thread_local zzt_qrcode_error_t last_error = ZZT_QRCODE_OK;

/* Log dispatch bridge state */
static mtx_t g_log_mutex;
static int g_log_mutex_initialized = 0;
static JavaVM *g_jvm = NULL;
static jclass g_log_class = NULL;
static jmethodID g_log_method = NULL;
static const jchar empty_jchar[] = {0};

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

jint zzt_qrcode_set_log_level_jni(JNIEnv *env, jclass clazz, jint level) {
    last_error = zzt_qrcode_set_log_level((zzt_qrcode_log_level_t) level);
    return (jint) last_error;
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

    jsize len = (*env)->GetStringLength(env, path);
    if (len == 0) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    jchar *buf = (jchar *) malloc(sizeof(jchar) * (len + 1));
    if (buf == NULL) {
        last_error = ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
        return 0;
    }

    (*env)->GetStringRegion(env, path, 0, len, buf);
    buf[len] = '\0';

    zzt_qrcode_result_h result = NULL;
    last_error = zzt_qrcode_detect_and_decode_path_u16(
            (zzt_qrcode_detector_h) native_detector,
            (const char16_t *) buf,
            &result);
    free(buf);
    return (jlong) result;
}

jlong zzt_qrcode_detect_and_decode_data_jni(JNIEnv *env, jclass clazz, jlong native_detector, jbyteArray data) {
    if (data == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

    jsize bytes_len = (*env)->GetArrayLength(env, data);
    jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);

    if (bytes == NULL) {
        last_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        return 0;
    }

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
                if (result == NULL) {
                    last_error = ZZT_QRCODE_ERROR_INTERNAL;
                }
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
            if (cls == NULL) {
                last_error = ZZT_QRCODE_ERROR_INTERNAL;
            } else {
                points_array = (*env)->NewObjectArray(env, (jsize) len / 2, cls, NULL);
                if (points_array == NULL) {
                    last_error = ZZT_QRCODE_ERROR_INTERNAL;
                }
                (*env)->DeleteLocalRef(env, cls);
                if (points_array != NULL) {
                    for (int i = 0; i < len / 2; ++i) {
                        jfloatArray point_jarr = (*env)->NewFloatArray(env, 2);
                        if (point_jarr == NULL) {
                            last_error = ZZT_QRCODE_ERROR_INTERNAL;
                            (*env)->DeleteLocalRef(env, points_array);
                            points_array = NULL;
                            break;
                        }
                        (*env)->SetFloatArrayRegion(env, point_jarr, 0, 2, &points[i * 2]);
                        (*env)->SetObjectArrayElement(env, points_array, i, point_jarr);
                        (*env)->DeleteLocalRef(env, point_jarr);
                    }
                }
            }
        }
        free(points);
    }

    return points_array;
}

/**
 * Native callback registered with zzt_qrcode_set_log_callback.
 * Dispatches to the fixed static method NativeLib.dispatchLog(int, String).
 * Handles thread attachment for arbitrary native threads and clears Java exceptions.
 */
static void native_log_callback(zzt_qrcode_log_level_t level, const char *message) {
    if (!g_log_mutex_initialized) {
        return;
    }

    mtx_lock(&g_log_mutex);
    JavaVM *jvm = g_jvm;
    mtx_unlock(&g_log_mutex);

    if (jvm == NULL) {
        return;
    }

    JNIEnv *env = NULL;
    jint get_env_err = (*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
    jboolean did_attach = JNI_FALSE;

    if (get_env_err == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK || env == NULL) {
            return;
        }
        did_attach = JNI_TRUE;
    } else if (get_env_err != JNI_OK || env == NULL) {
        return;
    }

    mtx_lock(&g_log_mutex);
    jclass log_class = g_log_class == NULL ? NULL : (jclass) (*env)->NewLocalRef(env, g_log_class);
    jmethodID log_method = g_log_method;
    mtx_unlock(&g_log_mutex);

    if (log_class == NULL || log_method == NULL) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        if (did_attach) {
            (*jvm)->DetachCurrentThread(jvm);
        }
        return;
    }

    jstring msg_jstr = NULL;
    int skip_dispatch = 0;

    /* Convert message: NULL or empty -> empty string */
    if (message != NULL && message[0] != '\0') {
        int msg_len = (int) strlen(message);
        int msg_buf_len = msg_len + 1;
        char16_t *msg_u16 = (char16_t *) malloc(sizeof(char16_t) * msg_buf_len);
        if (msg_u16 != NULL) {
            int msg_u16_len = utf8_to_utf16((const char8_t *) message, msg_len, msg_u16, msg_buf_len);
            msg_jstr = (msg_u16_len > 0)
                       ? (*env)->NewString(env, (const jchar *) msg_u16, msg_u16_len)
                       : (*env)->NewString(env, empty_jchar, 0);
            free(msg_u16);
        } else {
            skip_dispatch = 1;
        }
    } else {
        msg_jstr = (*env)->NewString(env, empty_jchar, 0);
    }

    if (!skip_dispatch && msg_jstr != NULL) {
        (*env)->CallStaticVoidMethod(env, log_class, log_method, (jint) level, msg_jstr);
    }

    /* Clear any Java exception thrown by string creation or dispatch; logging must never affect QR behavior. */
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    if (msg_jstr != NULL) {
        (*env)->DeleteLocalRef(env, msg_jstr);
    }
    (*env)->DeleteLocalRef(env, log_class);

    if (did_attach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
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

    if (!g_log_mutex_initialized && mtx_init(&g_log_mutex, mtx_plain) != thrd_success) {
        (*env)->DeleteLocalRef(env, cls);
        return JNI_ERR;
    }
    g_log_mutex_initialized = 1;

    /* Cache JVM, global class reference, and method ID for log dispatch */
    mtx_lock(&g_log_mutex);
    g_jvm = vm;
    g_log_class = (jclass) (*env)->NewGlobalRef(env, cls);
    if (g_log_class != NULL) {
        g_log_method = (*env)->GetStaticMethodID(env, g_log_class,
                                                  "dispatchLog",
                                                  "(ILjava/lang/String;)V");
    }
    mtx_unlock(&g_log_mutex);

    if (g_log_class == NULL || g_log_method == NULL) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        if (g_log_class != NULL) {
            (*env)->DeleteGlobalRef(env, g_log_class);
        }
        g_jvm = NULL;
        g_log_class = NULL;
        g_log_method = NULL;
        (*env)->DeleteLocalRef(env, cls);
        mtx_destroy(&g_log_mutex);
        g_log_mutex_initialized = 0;
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
            {"setLogLevel",           "(I)I",                   (void *) zzt_qrcode_set_log_level_jni},
    };
    ret = (*env)->RegisterNatives(env, cls, methods, sizeof(methods) / sizeof(methods[0]));
    (*env)->DeleteLocalRef(env, cls);
    if (ret != JNI_OK) {
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
        if (g_log_class != NULL) {
            (*env)->DeleteGlobalRef(env, g_log_class);
        }
        g_jvm = NULL;
        g_log_class = NULL;
        g_log_method = NULL;
        mtx_destroy(&g_log_mutex);
        g_log_mutex_initialized = 0;
        return JNI_ERR;
    }

    zzt_qrcode_error_t log_callback_error = zzt_qrcode_set_log_callback(native_log_callback);
    if (log_callback_error != ZZT_QRCODE_OK) {
        if (g_log_class != NULL) {
            (*env)->DeleteGlobalRef(env, g_log_class);
        }
        g_jvm = NULL;
        g_log_class = NULL;
        g_log_method = NULL;
        mtx_destroy(&g_log_mutex);
        g_log_mutex_initialized = 0;
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM *vm, void *reserved) {
    /* Clear the C log callback so no further dispatches arrive during teardown */
    zzt_qrcode_set_log_callback(NULL);

    if (!g_log_mutex_initialized) {
        return;
    }

    JNIEnv *env = NULL;
    jclass log_class = NULL;
    mtx_lock(&g_log_mutex);
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) == JNI_OK && env != NULL) {
        log_class = g_log_class;
    }
    g_jvm = NULL;
    g_log_class = NULL;
    g_log_method = NULL;
    mtx_unlock(&g_log_mutex);

    /* Release the global class reference after it is no longer reachable from callbacks. */
    if (env != NULL && log_class != NULL) {
        (*env)->DeleteGlobalRef(env, log_class);
    }

    /* Do not destroy g_log_mutex here: an in-flight callback may still be unwinding after
     * zzt_qrcode_set_log_callback(NULL). Keeping the mutex initialized avoids a teardown race. */
}
