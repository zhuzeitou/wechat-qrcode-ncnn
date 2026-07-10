#include <jni.h>
#include <malloc.h>
#include <string.h>
#include <threads.h>
#include <limits.h>

#include "zzt_qrcode/qrcode.h"

static thread_local zzt_qrcode_error_t last_error = ZZT_QRCODE_OK;

/* Runtime log sink bridge state.  The native sink exists only while either
 * managed listener list is non-empty. */
typedef struct {
    uint64_t generation;
    int destroyed;
} NativeLogSinkLifetime;

static mtx_t g_log_mutex;
static cnd_t g_log_cv;
static int g_log_mutex_initialized = 0;
static JavaVM *g_jvm = NULL;
static jclass g_log_class = NULL;
static jmethodID g_log_method = NULL;
static zzt_qrcode_log_sink_id_t g_log_sink_id = 0;
static NativeLogSinkLifetime *g_log_lifetime = NULL;
static uint64_t g_desired_generation = 0;
static uint64_t g_applied_generation = 0;
static uint64_t g_transition_token = 0;
static uint64_t g_lifetime_generation = 0;
static int g_desired_enabled = 0;
static int g_actual_enabled = 0;
static int32_t g_desired_level = ZZT_QRCODE_LOG_LEVEL_WARN;
static int32_t g_actual_level = ZZT_QRCODE_LOG_LEVEL_WARN;
static int g_transitioning = 0;
static int g_transition_destroy_owns_completion = 0;
static int g_unloading = 0;
static zzt_qrcode_error_t g_log_error = ZZT_QRCODE_OK;
static thread_local int g_inside_log_callback = 0;
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

/* The core owns a lifetime only after add succeeds.  Destruction is its
 * quiescence acknowledgement; it is deliberately separate from registration. */
static void ZZT_QRCODE_CALLBACK native_log_sink_destroy(void *user_data) {
    NativeLogSinkLifetime *lifetime = (NativeLogSinkLifetime *) user_data;
    int destroy_owned = 0;
    if (lifetime == NULL || !g_log_mutex_initialized) return;
    mtx_lock(&g_log_mutex);
    lifetime->destroyed = 1;
    if (g_transitioning && g_log_lifetime == lifetime &&
        g_transition_destroy_owns_completion) {
        g_actual_enabled = 0;
        g_log_sink_id = 0;
        g_log_lifetime = NULL;
        g_applied_generation = g_desired_generation;
        g_transitioning = 0;
        g_transition_destroy_owns_completion = 0;
        destroy_owned = 1;
        cnd_broadcast(&g_log_cv);
    }
    cnd_broadcast(&g_log_cv);
    mtx_unlock(&g_log_mutex);
    if (destroy_owned) free(lifetime);
}

static void ZZT_QRCODE_CALLBACK native_log_callback(
        const zzt_qrcode_log_event_t *event, void *user_data) {
    (void) user_data;
    if (!g_log_mutex_initialized || event == NULL || event->message == NULL ||
        event->message_len > INT32_MAX) return;
    mtx_lock(&g_log_mutex);
    JavaVM *jvm = g_jvm;
    mtx_unlock(&g_log_mutex);
    if (jvm == NULL) return;

    JNIEnv *env = NULL;
    jint get_env_err = (*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
    jboolean did_attach = JNI_FALSE;
    if (get_env_err == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK || env == NULL) return;
        did_attach = JNI_TRUE;
    } else if (get_env_err != JNI_OK || env == NULL) return;

    mtx_lock(&g_log_mutex);
    jclass log_class = g_log_class == NULL ? NULL : (jclass) (*env)->NewLocalRef(env, g_log_class);
    jmethodID log_method = g_log_method;
    mtx_unlock(&g_log_mutex);
    if (log_class == NULL || log_method == NULL) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        if (did_attach) (*jvm)->DetachCurrentThread(jvm);
        return;
    }

    const int message_len = (int) event->message_len;
    char16_t *message_u16 = NULL;
    jstring message = NULL;
    if (message_len == 0) {
        message = (*env)->NewString(env, empty_jchar, 0);
    } else if ((message_u16 = malloc(sizeof(char16_t) * (size_t) (message_len + 1))) != NULL) {
        int u16_len = utf8_to_utf16((const char8_t *) event->message, message_len,
                                    message_u16, message_len + 1);
        message = (*env)->NewString(env, (const jchar *) message_u16, u16_len);
    }
    if (message != NULL) {
        ++g_inside_log_callback;
        (*env)->CallStaticVoidMethod(env, log_class, log_method, (jint) event->level, message);
        --g_inside_log_callback;
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    free(message_u16);
    if (message != NULL) (*env)->DeleteLocalRef(env, message);
    (*env)->DeleteLocalRef(env, log_class);
    if (did_attach) (*jvm)->DetachCurrentThread(jvm);
}

/* Exactly one caller owns a transition.  Native calls run with the mutex
 * released; stale calls can therefore never commit a newer desired generation. */
static zzt_qrcode_error_t reconcile_log_sink(int enabled, int32_t level) {
    if (!g_log_mutex_initialized) return ZZT_QRCODE_ERROR_INTERNAL;
    mtx_lock(&g_log_mutex);
    if (enabled != g_desired_enabled || level != g_desired_level) {
        g_desired_enabled = enabled;
        g_desired_level = level;
        ++g_desired_generation;
    }
    if (level < ZZT_QRCODE_LOG_LEVEL_VERBOSE || level > ZZT_QRCODE_LOG_LEVEL_ERROR) {
        g_log_error = ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        mtx_unlock(&g_log_mutex);
        return g_log_error;
    }
    if (g_inside_log_callback) {
        mtx_unlock(&g_log_mutex);
        return ZZT_QRCODE_OK;
    }
    while (g_transitioning) cnd_wait(&g_log_cv, &g_log_mutex);
    if (g_actual_enabled == g_desired_enabled &&
        (!g_actual_enabled || g_actual_level == g_desired_level)) {
        zzt_qrcode_error_t result = g_log_error;
        mtx_unlock(&g_log_mutex);
        return result;
    }

    const uint64_t token = ++g_transition_token;
    const uint64_t generation = g_desired_generation;
    const int target_enabled = g_desired_enabled;
    const int32_t target_level = g_desired_level;
    const zzt_qrcode_log_sink_id_t old_sink_id = g_log_sink_id;
    NativeLogSinkLifetime *lifetime = NULL;
    int remove = 0, update = 0;
    g_transitioning = 1;
    g_transition_destroy_owns_completion = 0;
    if (target_enabled && !g_actual_enabled) {
        lifetime = calloc(1, sizeof(*lifetime));
        if (lifetime == NULL) {
            g_transitioning = 0;
            g_log_error = ZZT_QRCODE_ERROR_OUT_OF_MEMORY;
            cnd_broadcast(&g_log_cv);
            mtx_unlock(&g_log_mutex);
            return g_log_error;
        }
        lifetime->generation = ++g_lifetime_generation;
    } else if (!target_enabled && g_actual_enabled) {
        remove = 1;
        lifetime = g_log_lifetime;
        g_transition_destroy_owns_completion = 0;
    } else {
        update = 1;
    }
    mtx_unlock(&g_log_mutex);

    zzt_qrcode_error_t error;
    zzt_qrcode_log_sink_id_t new_sink_id = 0;
    if (lifetime != NULL && !remove) {
        zzt_qrcode_log_sink_options_t options = ZZT_QRCODE_LOG_SINK_OPTIONS_INIT;
        options.callback = native_log_callback;
        options.user_data = lifetime;
        options.destroy_user_data = native_log_sink_destroy;
        options.min_level = target_level;
        error = zzt_qrcode_add_runtime_log_sink(&options, &new_sink_id);
    } else if (remove) {
        error = zzt_qrcode_remove_runtime_log_sink(old_sink_id);
    } else if (update) {
        error = zzt_qrcode_set_runtime_log_sink_level(old_sink_id, target_level);
    } else {
        error = ZZT_QRCODE_ERROR_INTERNAL;
    }

    mtx_lock(&g_log_mutex);
    if (!g_transitioning || token != g_transition_token || generation != g_desired_generation) {
        const int remove_new_sink = lifetime != NULL && !remove &&
                                    error == ZZT_QRCODE_OK && new_sink_id != 0;
        mtx_unlock(&g_log_mutex);
        if (remove_new_sink) (void) zzt_qrcode_remove_runtime_log_sink(new_sink_id);
        mtx_lock(&g_log_mutex);
        if (remove && error == ZZT_QRCODE_OK && lifetime != NULL && lifetime->destroyed) {
            free(lifetime);
            g_log_lifetime = NULL;
            g_log_sink_id = 0;
            g_actual_enabled = 0;
        } else if (lifetime != NULL && !remove) {
            free(lifetime);
        }
        if (g_transitioning && token == g_transition_token) {
            g_transitioning = 0;
            g_transition_destroy_owns_completion = 0;
            cnd_broadcast(&g_log_cv);
        }
        mtx_unlock(&g_log_mutex);
        return error;
    }
    if (error == ZZT_QRCODE_OK) {
        if (remove) {
            if (lifetime == NULL || !lifetime->destroyed) {
                error = ZZT_QRCODE_ERROR_INTERNAL;
            } else {
                free(lifetime);
                g_log_lifetime = NULL;
                g_log_sink_id = 0;
                g_actual_enabled = 0;
            }
        } else if (update) {
            g_actual_level = target_level;
        } else {
            g_log_lifetime = lifetime;
            g_log_sink_id = new_sink_id;
            g_actual_enabled = 1;
            g_actual_level = target_level;
        }
    }
    if (error != ZZT_QRCODE_OK && lifetime != NULL && !remove) free(lifetime);
    g_log_error = error;
    g_applied_generation = generation;
    g_transitioning = 0;
    g_transition_destroy_owns_completion = 0;
    cnd_broadcast(&g_log_cv);
    mtx_unlock(&g_log_mutex);
    return error;
}

jint zzt_qrcode_configure_log_sink_jni(JNIEnv *env, jclass clazz, jboolean enabled, jint level) {
    (void) env; (void) clazz;
    last_error = reconcile_log_sink(enabled == JNI_TRUE, (int32_t) level);
    return (jint) last_error;
}

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void) reserved;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = (*env)->FindClass(env, "xyz/zhuzeitou/qrcode/NativeLib");
    if (cls == NULL) return JNI_ERR;
    if (!g_log_mutex_initialized &&
        (mtx_init(&g_log_mutex, mtx_plain) != thrd_success ||
         cnd_init(&g_log_cv) != thrd_success)) {
        (*env)->DeleteLocalRef(env, cls);
        return JNI_ERR;
    }
    g_log_mutex_initialized = 1;
    mtx_lock(&g_log_mutex);
    g_jvm = vm;
    g_log_class = (jclass) (*env)->NewGlobalRef(env, cls);
    if (g_log_class != NULL)
        g_log_method = (*env)->GetStaticMethodID(env, g_log_class, "dispatchLog", "(ILjava/lang/String;)V");
    mtx_unlock(&g_log_mutex);
    if (g_log_class == NULL || g_log_method == NULL) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        return JNI_ERR;
    }
    JNINativeMethod methods[] = {
            {"createDetector", "()J", (void *) zzt_qrcode_create_detector_jni},
            {"releaseDetector", "(J)V", (void *) zzt_qrcode_release_detector_jni},
            {"detectAndDecodePath", "(JLjava/lang/String;)J", (void *) zzt_qrcode_detect_and_decode_path_jni},
            {"detectAndDecodeData", "(J[B)J", (void *) zzt_qrcode_detect_and_decode_data_jni},
            {"detectAndDecodePixels", "(J[BIIII)J", (void *) zzt_qrcode_detect_and_decode_pixels_byte_jni},
            {"detectAndDecodePixels", "(J[IIIII)J", (void *) zzt_qrcode_detect_and_decode_pixels_int_jni},
            {"releaseResult", "(J)V", (void *) zzt_qrcode_release_result_jni},
            {"getResultSize", "(J)I", (void *) zzt_qrcode_get_result_size_jni},
            {"getResultText", "(JI)Ljava/lang/String;", (void *) zzt_qrcode_get_result_text_jni},
            {"getResultPoints", "(JI)[[F", (void *) zzt_qrcode_get_result_points_jni},
            {"getLastError", "()I", (void *) zzt_qrcode_get_last_error_jni},
            {"configureLogSink", "(ZI)I", (void *) zzt_qrcode_configure_log_sink_jni},
    };
    jint result = (*env)->RegisterNatives(env, cls, methods, sizeof(methods) / sizeof(methods[0]));
    (*env)->DeleteLocalRef(env, cls);
    return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}

void JNI_OnUnload(JavaVM *vm, void *reserved) {
    (void) reserved;
    if (!g_log_mutex_initialized) return;
    mtx_lock(&g_log_mutex);
    g_unloading = 1;
    mtx_unlock(&g_log_mutex);
    (void) reconcile_log_sink(0, ZZT_QRCODE_LOG_LEVEL_WARN);
    JNIEnv *env = NULL;
    mtx_lock(&g_log_mutex);
    jclass log_class = g_log_class;
    g_jvm = NULL;
    g_log_class = NULL;
    g_log_method = NULL;
    mtx_unlock(&g_log_mutex);
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) == JNI_OK && env != NULL && log_class != NULL)
        (*env)->DeleteGlobalRef(env, log_class);
}
