#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "zzt_qrcode/qrcode.h"

#define EXPECT_TRUE(condition, message) do { if (!(condition)) { std::cerr << "FAIL: " << message << " (line " << __LINE__ << ")\n"; return 1; } } while (false)
#define EXPECT_EQ(actual, expected, message) do { const auto actual_value = (actual); const auto expected_value = (expected); if (actual_value != expected_value) { std::cerr << "FAIL: " << message << " (expected " << expected_value << ", got " << actual_value << ")\n"; return 1; } } while (false)

namespace {
struct Event {
    int32_t level;
    int32_t error;
    uint64_t detector;
    uint64_t result;
    std::string operation;
    std::string message;
};
struct Capture {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<Event> events;
    int destroys = 0;
    bool entered = false;
    bool allow_return = true;
    bool destroy_entered = false;
    bool allow_destroy = true;
    zzt_qrcode_log_sink_id_t self = 0;
    zzt_qrcode_log_sink_id_t other = 0;
    bool remove_self = false;
    bool remove_other = false;
    bool nested = false;
    zzt_qrcode_detector_h detector = nullptr;
    Capture *add_capture = nullptr;
    bool add_runtime_once = false;
    zzt_qrcode_log_sink_id_t added_id = 0;
};

bool contains(const std::string &text, const char *needle) { return text.find(needle) != std::string::npos; }
void clear(Capture &capture) { std::lock_guard<std::mutex> lock(capture.mutex); capture.events.clear(); }
std::vector<Event> snapshot(Capture &capture) { std::lock_guard<std::mutex> lock(capture.mutex); return capture.events; }
bool saw(Capture &capture, const char *message) { for (const Event &event : snapshot(capture)) if (contains(event.message, message)) return true; return false; }

void ZZT_QRCODE_CALLBACK capture_log(const zzt_qrcode_log_event_t *event, void *user_data) {
    Capture *capture = static_cast<Capture *>(user_data);
    if (event == nullptr || event->struct_size < sizeof(zzt_qrcode_log_event_t) || event->operation == nullptr || event->message == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(capture->mutex);
        capture->events.push_back({event->level, event->error_code, event->detector_id, event->result_id,
                                   std::string(event->operation, event->operation_len), std::string(event->message, event->message_len)});
        capture->entered = true;
    }
    capture->cv.notify_all();
    if (capture->nested && capture->detector != nullptr) {
        capture->nested = false;
        zzt_qrcode_result_h ignored = nullptr;
        zzt_qrcode_detect_and_decode_pixels(capture->detector, nullptr, ZZT_QRCODE_PIXEL_GRAY, 1, 1, 1, &ignored);
    }
    if (capture->remove_other) zzt_qrcode_remove_runtime_log_sink(capture->other);
    if (capture->remove_self) zzt_qrcode_remove_runtime_log_sink(capture->self);
    if (capture->add_runtime_once && capture->add_capture != nullptr) {
        capture->add_runtime_once = false;
        zzt_qrcode_log_sink_options_t added = ZZT_QRCODE_LOG_SINK_OPTIONS_INIT;
        added.callback = capture_log;
        added.user_data = capture->add_capture;
        added.min_level = ZZT_QRCODE_LOG_LEVEL_WARN;
        zzt_qrcode_add_runtime_log_sink(&added, &capture->added_id);
    }
    std::unique_lock<std::mutex> lock(capture->mutex);
    capture->cv.wait(lock, [&] { return capture->allow_return; });
}
void ZZT_QRCODE_CALLBACK destroy_capture(void *user_data) {
    Capture *capture = static_cast<Capture *>(user_data);
    // Management must be reentrant and runs without the logger's internal lock.
    if (capture->self != 0) zzt_qrcode_set_runtime_log_sink_level(capture->self, ZZT_QRCODE_LOG_LEVEL_WARN);
    std::unique_lock<std::mutex> lock(capture->mutex);
    ++capture->destroys;
    capture->destroy_entered = true;
    capture->cv.notify_all();
    capture->cv.wait(lock, [&] { return capture->allow_destroy; });
}
zzt_qrcode_log_sink_options_t options(Capture *capture, int32_t level = ZZT_QRCODE_LOG_LEVEL_WARN, bool destroy = false) {
    zzt_qrcode_log_sink_options_t value = ZZT_QRCODE_LOG_SINK_OPTIONS_INIT;
    value.callback = capture_log;
    value.user_data = capture;
    value.destroy_user_data = destroy ? destroy_capture : nullptr;
    value.min_level = level;
    return value;
}
zzt_qrcode_error_t invalid_pixels(zzt_qrcode_detector_h detector) {
    zzt_qrcode_result_h result = reinterpret_cast<zzt_qrcode_result_h>(1);
    const zzt_qrcode_error_t error = zzt_qrcode_detect_and_decode_pixels(detector, nullptr, ZZT_QRCODE_PIXEL_GRAY, 1, 1, 1, &result);
    return error;
}
void wait_entered(Capture &capture, bool destroy = false) {
    std::unique_lock<std::mutex> lock(capture.mutex);
    capture.cv.wait(lock, [&] { return destroy ? capture.destroy_entered : capture.entered; });
}

int test_runtime_sinks() {
    Capture warn, error;
    zzt_qrcode_log_sink_id_t warn_id = 0, error_id = 0;
    auto warn_options = options(&warn, ZZT_QRCODE_LOG_LEVEL_WARN);
    auto error_options = options(&error, ZZT_QRCODE_LOG_LEVEL_ERROR);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&warn_options, &warn_id), ZZT_QRCODE_OK, "add WARN runtime sink");
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&error_options, &error_id), ZZT_QRCODE_OK, "add ERROR runtime sink");
    zzt_qrcode_detector_h detector = nullptr;
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(nullptr, &detector), ZZT_QRCODE_OK, "null detector options select runtime");
    EXPECT_TRUE(detector != nullptr, "create detector");
    EXPECT_EQ(invalid_pixels(detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "invalid pixels warning");
    EXPECT_TRUE(!snapshot(warn).empty(), "WARN sink receives warning");
    EXPECT_TRUE(snapshot(error).empty(), "ERROR sink filters warning independently");
    EXPECT_EQ(zzt_qrcode_set_runtime_log_sink_level(error_id, ZZT_QRCODE_LOG_LEVEL_WARN), ZZT_QRCODE_OK, "update one sink level");
    EXPECT_EQ(invalid_pixels(detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "second warning");
    zzt_qrcode_detector_options_t null_logger_options = ZZT_QRCODE_DETECTOR_OPTIONS_INIT;
    null_logger_options.log_flags = ZZT_QRCODE_DETECTOR_LOG_PROPAGATE_TO_RUNTIME;
    zzt_qrcode_detector_h null_logger_detector = nullptr;
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(&null_logger_options, &null_logger_detector), ZZT_QRCODE_OK, "null logger propagation is runtime-only");
    const size_t warn_before_null_logger = snapshot(warn).size();
    EXPECT_EQ(invalid_pixels(null_logger_detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "null logger propagated warning");
    EXPECT_TRUE(snapshot(warn).size() > warn_before_null_logger, "null logger propagation delivers once to runtime");
    EXPECT_EQ(zzt_qrcode_release_detector(null_logger_detector), ZZT_QRCODE_OK, "release null logger detector");
    EXPECT_TRUE(!snapshot(error).empty(), "updated sink receives warning");
    EXPECT_EQ(zzt_qrcode_release_detector(detector), ZZT_QRCODE_OK, "release detector");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(warn_id), ZZT_QRCODE_OK, "remove WARN sink");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(error_id), ZZT_QRCODE_OK, "remove ERROR sink");
    return 0;
}

int test_quiescent_removal_and_reentry() {
    Capture blocking;
    blocking.allow_return = false;
    blocking.allow_destroy = false;
    zzt_qrcode_log_sink_id_t id = 0;
    auto sink_options = options(&blocking, ZZT_QRCODE_LOG_LEVEL_WARN, true);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&sink_options, &id), ZZT_QRCODE_OK, "add blocking sink");
    blocking.self = id;
    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    std::thread emitter([&] {
        const zzt_qrcode_error_t error = invalid_pixels(detector);
        if (error != ZZT_QRCODE_ERROR_INVALID_ARGUMENT) std::cerr << "FAIL: blocked event\n";
    });
    wait_entered(blocking);
    std::atomic<bool> removed{false};
    std::thread remover([&] {
        const zzt_qrcode_error_t error = zzt_qrcode_remove_runtime_log_sink(id);
        if (error != ZZT_QRCODE_OK) std::cerr << "FAIL: external remove\n";
        else removed.store(true);
    });
    { std::lock_guard<std::mutex> lock(blocking.mutex); blocking.allow_return = true; }
    blocking.cv.notify_all();
    wait_entered(blocking, true);
    EXPECT_TRUE(!removed.load(), "remove waits for blocked destroy callback");
    { std::lock_guard<std::mutex> lock(blocking.mutex); blocking.allow_destroy = true; }
    blocking.cv.notify_all();
    emitter.join(); remover.join();
    EXPECT_TRUE(removed.load() && blocking.destroys == 1, "external removal completes terminal destroy exactly once");

    Capture self;
    zzt_qrcode_log_sink_id_t self_id = 0;
    auto self_options = options(&self, ZZT_QRCODE_LOG_LEVEL_WARN, true);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&self_options, &self_id), ZZT_QRCODE_OK, "add self removing sink");
    self.self = self_id; self.remove_self = true;
    EXPECT_EQ(invalid_pixels(detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "self removal event");
    EXPECT_TRUE(self.destroys == 1, "self removal destroys exactly once");
    EXPECT_EQ(zzt_qrcode_release_detector(detector), ZZT_QRCODE_OK, "release detector after reentry");
    return 0;
}

int test_nested_and_routes() {
    Capture primary, secondary;
    zzt_qrcode_log_sink_id_t primary_id = 0, secondary_id = 0;
    auto primary_options = options(&primary), secondary_options = options(&secondary);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&primary_options, &primary_id), ZZT_QRCODE_OK, "add nested primary");
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&secondary_options, &secondary_id), ZZT_QRCODE_OK, "add nested secondary");
    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    primary.detector = detector; primary.nested = true;
    EXPECT_EQ(invalid_pixels(detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "nested callback event");
    const size_t primary_events = snapshot(primary).size();
    const size_t secondary_events = snapshot(secondary).size();
    EXPECT_TRUE(primary_events == 1, "active callback skipped for nested event");
    EXPECT_TRUE(secondary_events >= 2, "other sink receives nested event");
    EXPECT_EQ(zzt_qrcode_release_detector(detector), ZZT_QRCODE_OK, "release nested detector");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(primary_id), ZZT_QRCODE_OK, "remove nested primary");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(secondary_id), ZZT_QRCODE_OK, "remove nested secondary");

    Capture runtime, custom;
    zzt_qrcode_log_sink_id_t runtime_id = 0, custom_id = 0;
    auto runtime_options = options(&runtime), custom_options = options(&custom);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&runtime_options, &runtime_id), ZZT_QRCODE_OK, "add route runtime");
    zzt_qrcode_logger_h logger = nullptr;
    EXPECT_EQ(zzt_qrcode_create_logger(&logger), ZZT_QRCODE_OK, "create custom logger");
    EXPECT_EQ(zzt_qrcode_logger_add_sink(logger, &custom_options, &custom_id), ZZT_QRCODE_OK, "add custom sink");
    zzt_qrcode_detector_options_t route_options = ZZT_QRCODE_DETECTOR_OPTIONS_INIT;
    route_options.logger = logger;
    zzt_qrcode_detector_h custom_detector = nullptr;
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(&route_options, &custom_detector), ZZT_QRCODE_OK, "create custom detector");
    clear(runtime); clear(custom);
    EXPECT_EQ(invalid_pixels(custom_detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "custom route error");
    EXPECT_TRUE(!snapshot(custom).empty() && snapshot(runtime).empty(), "custom detector overrides runtime");
    EXPECT_EQ(zzt_qrcode_release_detector(custom_detector), ZZT_QRCODE_OK, "release custom detector");
    route_options.log_flags = ZZT_QRCODE_DETECTOR_LOG_PROPAGATE_TO_RUNTIME;
    zzt_qrcode_detector_h propagated = nullptr;
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(&route_options, &propagated), ZZT_QRCODE_OK, "create propagated detector");
    clear(runtime); clear(custom);
    EXPECT_EQ(invalid_pixels(propagated), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "propagated route error");
    EXPECT_TRUE(!snapshot(custom).empty() && !snapshot(runtime).empty(), "propagation dual writes");
    Capture victim, added;
    zzt_qrcode_log_sink_id_t victim_id = 0;
    auto victim_options = options(&victim, ZZT_QRCODE_LOG_LEVEL_WARN, true);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&victim_options, &victim_id), ZZT_QRCODE_OK, "add removable runtime sink");
    custom.other = victim_id;
    custom.remove_other = true;
    custom.add_capture = &added;
    custom.add_runtime_once = true;
    clear(runtime); clear(custom); clear(victim); clear(added);
    EXPECT_EQ(invalid_pixels(propagated), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "custom management callback event");
    EXPECT_TRUE(snapshot(victim).empty() && victim.destroys == 1, "custom callback removes idle runtime sink before runtime phase");
    EXPECT_TRUE(snapshot(added).empty() && custom.added_id != 0, "runtime sink added in callback begins next event");
    custom.remove_other = false;
    EXPECT_EQ(invalid_pixels(propagated), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "next event reaches added sink");
    EXPECT_TRUE(!snapshot(added).empty(), "new runtime sink receives next event");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(custom.added_id), ZZT_QRCODE_OK, "remove callback-added sink");
    EXPECT_EQ(zzt_qrcode_release_detector(propagated), ZZT_QRCODE_OK, "release propagated detector");
    EXPECT_EQ(zzt_qrcode_logger_remove_sink(logger, custom_id), ZZT_QRCODE_OK, "remove custom sink");
    EXPECT_EQ(zzt_qrcode_release_logger(logger), ZZT_QRCODE_OK, "release logger");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(runtime_id), ZZT_QRCODE_OK, "remove route runtime");
    return 0;
}

int test_options_handles_and_result_lifetime() {
    Capture runtime, custom;
    zzt_qrcode_log_sink_id_t runtime_id = 123;
    auto runtime_options = options(&runtime);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(nullptr, &runtime_id), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "null options rejected");
    EXPECT_TRUE(runtime_id == 0, "failed add clears output ID");
    auto short_options = runtime_options; short_options.struct_size = sizeof(short_options) - 1;
    runtime_id = 123;
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&short_options, &runtime_id), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "undersized options rejected");
    EXPECT_TRUE(runtime_id == 0, "undersized add clears output ID");
    struct LargeSink { zzt_qrcode_log_sink_options_t options; uint64_t tail; } large_sink{runtime_options, 0xdeadbeef};
    large_sink.options.struct_size = sizeof(large_sink);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&large_sink.options, &runtime_id), ZZT_QRCODE_OK, "oversized options accepted");
    EXPECT_EQ(zzt_qrcode_set_runtime_log_sink_level(runtime_id, static_cast<zzt_qrcode_log_level_t>(5)), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "invalid runtime level");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(runtime_id), ZZT_QRCODE_OK, "remove oversized sink");

    zzt_qrcode_logger_h logger = nullptr;
    EXPECT_EQ(zzt_qrcode_create_logger(nullptr), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "null logger output");
    EXPECT_EQ(zzt_qrcode_create_logger(&logger), ZZT_QRCODE_OK, "create logger for result lifetime");
    zzt_qrcode_log_sink_id_t custom_id = 0;
    auto custom_options = options(&custom, ZZT_QRCODE_LOG_LEVEL_WARN, true);
    EXPECT_EQ(zzt_qrcode_logger_add_sink(logger, &custom_options, &custom_id), ZZT_QRCODE_OK, "add lifetime sink");
    zzt_qrcode_log_sink_id_t validation_runtime_id = 0;
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&runtime_options, &validation_runtime_id), ZZT_QRCODE_OK, "add validation runtime sink");
    zzt_qrcode_detector_options_t invalid_detector_options = ZZT_QRCODE_DETECTOR_OPTIONS_INIT;
    invalid_detector_options.logger = logger;
    invalid_detector_options.log_flags = 2;
    zzt_qrcode_detector_h invalid_detector = reinterpret_cast<zzt_qrcode_detector_h>(1);
    clear(custom); clear(runtime);
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(&invalid_detector_options, &invalid_detector), ZZT_QRCODE_ERROR_INVALID_ARGUMENT, "unknown detector flags rejected");
    EXPECT_TRUE(invalid_detector == nullptr, "failed detector creation clears output");
    EXPECT_TRUE(!snapshot(custom).empty() && snapshot(runtime).empty(), "custom logger receives invalid create event without propagation");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(validation_runtime_id), ZZT_QRCODE_OK, "remove validation runtime sink");
    zzt_qrcode_detector_options_t detector_options = ZZT_QRCODE_DETECTOR_OPTIONS_INIT;
    detector_options.logger = logger;
    struct LargeDetector { zzt_qrcode_detector_options_t options; uint64_t tail; } large_detector{detector_options, 9};
    large_detector.options.struct_size = sizeof(large_detector);
    zzt_qrcode_detector_h detector = nullptr;
    EXPECT_EQ(zzt_qrcode_create_detector_with_options(&large_detector.options, &detector), ZZT_QRCODE_OK, "oversized detector options accepted");
    const unsigned char pixels[1] = {0};
    zzt_qrcode_result_h result = nullptr;
    EXPECT_EQ(zzt_qrcode_detect_and_decode_pixels(detector, pixels, ZZT_QRCODE_PIXEL_GRAY, 1, 1, 1, &result), ZZT_QRCODE_OK, "create blank result");
    EXPECT_TRUE(result != nullptr, "blank result handle");
    EXPECT_EQ(zzt_qrcode_release_logger(logger), ZZT_QRCODE_OK, "release public logger handle");
    EXPECT_EQ(zzt_qrcode_release_detector(detector), ZZT_QRCODE_OK, "release detector before result");
    clear(custom);
    int size = 0;
    EXPECT_EQ(zzt_qrcode_get_result_text(result, 10, nullptr, &size), ZZT_QRCODE_ERROR_INVALID_INDEX, "invalid result index");
    const auto events = snapshot(custom);
    EXPECT_TRUE(!events.empty() && events.back().error == ZZT_QRCODE_ERROR_INVALID_INDEX, "result log reaches retained custom route");
    EXPECT_TRUE(events.back().detector != 0 && events.back().result != 0, "result keeps stable IDs");
    EXPECT_EQ(zzt_qrcode_release_result(result), ZZT_QRCODE_OK, "release retained result");
    EXPECT_TRUE(custom.destroys == 1, "retained custom sink destroyed once");
    return 0;
}

int test_timing_and_privacy() {
    Capture capture;
    zzt_qrcode_log_sink_id_t id = 0;
    auto sink_options = options(&capture, ZZT_QRCODE_LOG_LEVEL_VERBOSE);
    EXPECT_EQ(zzt_qrcode_add_runtime_log_sink(&sink_options, &id), ZZT_QRCODE_OK, "add verbose timing sink");
    zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();
    const unsigned char pixels[100] = {};
    zzt_qrcode_result_h result = nullptr;
    EXPECT_EQ(zzt_qrcode_detect_and_decode_pixels(detector, pixels, ZZT_QRCODE_PIXEL_GRAY, 10, 10, 10, &result), ZZT_QRCODE_OK, "timed pixels");
    if (result != nullptr) zzt_qrcode_release_result(result);
    const char *path = "SENTINEL_PATH_xyz_123_SECRET";
    EXPECT_EQ(zzt_qrcode_detect_and_decode_path_u8(detector, reinterpret_cast<const char8_t *>(path), &result), ZZT_QRCODE_ERROR_DECODE_FAILED, "timed invalid path");
    const auto events = snapshot(capture);
    bool timing = false;
    for (const Event &event : events) {
        if (event.level == ZZT_QRCODE_LOG_LEVEL_VERBOSE) { timing = true; EXPECT_TRUE(event.error == ZZT_QRCODE_OK, "timing event has OK error"); }
        EXPECT_TRUE(!contains(event.message, path), "path sentinel does not leak");
        EXPECT_TRUE(!event.operation.empty(), "operation is populated");
    }
    EXPECT_TRUE(timing && saw(capture, "total duration"), "verbose timing events are captured");
    EXPECT_EQ(zzt_qrcode_release_detector(detector), ZZT_QRCODE_OK, "release timing detector");
    EXPECT_EQ(zzt_qrcode_remove_runtime_log_sink(id), ZZT_QRCODE_OK, "remove timing sink");
    return 0;
}
}  // namespace

int main() {
    std::cout << "Starting structured logger validation tests...\n";
    if (int result = test_runtime_sinks()) return result;
    if (int result = test_quiescent_removal_and_reentry()) return result;
    if (int result = test_nested_and_routes()) return result;
    if (int result = test_options_handles_and_result_lifetime()) return result;
    if (int result = test_timing_and_privacy()) return result;
    std::cout << "All structured logger validation tests passed successfully!\n";
    return 0;
}
