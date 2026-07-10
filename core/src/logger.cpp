#include "logger.h"
#include "handle.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <cstring>
#include <utility>
#include <vector>

namespace zzt::qrcode {
namespace {
constexpr int32_t kNoLogLevel = ZZT_QRCODE_LOG_LEVEL_ERROR + 1;
thread_local unsigned inside_log_user_code = 0;
struct UserCodeScope { UserCodeScope() { ++inside_log_user_code; } ~UserCodeScope() { --inside_log_user_code; } };
enum class RegistrationState { Active, Retiring, Destroying, Destroyed };
struct SinkRegistration {
    zzt_qrcode_log_callback_t callback;
    void* user_data;
    zzt_qrcode_log_user_data_destroy_t destroy;
    std::mutex mutex;
    std::condition_variable cv;
    RegistrationState state = RegistrationState::Active;
    uint32_t in_flight = 0;
    SinkRegistration(zzt_qrcode_log_callback_t c, void* d, zzt_qrcode_log_user_data_destroy_t x) : callback(c), user_data(d), destroy(x) {}
};
struct CallbackFrame {
    const SinkRegistration* registration;
    CallbackFrame* previous;
};
thread_local CallbackFrame* active_registration_frame = nullptr;
struct CallbackScope {
    CallbackFrame frame;
    explicit CallbackScope(const SinkRegistration* registration) : frame{registration, active_registration_frame} {
        active_registration_frame = &frame; ++inside_log_user_code;
    }
    ~CallbackScope() { --inside_log_user_code; active_registration_frame = frame.previous; }
};
struct SinkEntry { uint64_t id; int32_t min_level; std::shared_ptr<SinkRegistration> registration; };
struct SinkSnapshot { std::vector<SinkEntry> entries; int32_t min_level = kNoLogLevel; };
std::atomic<uint64_t> g_sink_ids{0};
std::atomic<uint64_t> g_object_ids{0};
uint64_t next_id(std::atomic<uint64_t>& counter) {
    uint64_t value = counter.load(std::memory_order_relaxed);
    for (;;) { if (value == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("ID exhausted"); if (counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1; }
}
bool valid_level(int32_t level) { return level >= ZZT_QRCODE_LOG_LEVEL_VERBOSE && level <= ZZT_QRCODE_LOG_LEVEL_ERROR; }
void complete_destroy(const std::shared_ptr<SinkRegistration>& r) noexcept {
    void* user_data = nullptr; zzt_qrcode_log_user_data_destroy_t destroy = nullptr;
    { std::lock_guard<std::mutex> lock(r->mutex); user_data = r->user_data; destroy = r->destroy; r->user_data = nullptr; r->destroy = nullptr; }
    try { if (destroy) { UserCodeScope scope; destroy(user_data); } } catch (...) {}
    { std::lock_guard<std::mutex> lock(r->mutex); r->state = RegistrationState::Destroyed; r->cv.notify_all(); }
}
bool claim_destroy_locked(const std::shared_ptr<SinkRegistration>& r) {
    if (r->state == RegistrationState::Retiring && r->in_flight == 0) { r->state = RegistrationState::Destroying; return true; } return false;
}
}

class Logger : public std::enable_shared_from_this<Logger> {
public:
    Logger() = default;
    ~Logger() { retire_all(); }
    zzt_qrcode_error_t add(const zzt_qrcode_log_sink_options_t* options, uint64_t* out) {
        if (!out) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT; *out = 0;
        if (!options || options->struct_size < sizeof(*options) || !options->callback || !valid_level(options->min_level)) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
        try {
            auto registration = std::make_shared<SinkRegistration>(options->callback, options->user_data, options->destroy_user_data);
            const uint64_t id = next_id(g_sink_ids);
            std::lock_guard<std::mutex> lock(writer_);
            auto old = std::atomic_load(&snapshot_); auto next = std::make_shared<SinkSnapshot>(); next->entries.reserve((old ? old->entries.size() : 0) + 1);
            if (old) for (const auto& e : old->entries) { std::lock_guard<std::mutex> gate(e.registration->mutex); if (e.registration->state == RegistrationState::Active) next->entries.push_back(e); }
            next->entries.push_back({id, options->min_level, registration}); recompute(*next); std::atomic_store(&snapshot_, std::const_pointer_cast<const SinkSnapshot>(next)); *out = id; return ZZT_QRCODE_OK;
        } catch (const std::bad_alloc&) { return ZZT_QRCODE_ERROR_OUT_OF_MEMORY; } catch (...) { return ZZT_QRCODE_ERROR_INTERNAL; }
    }
    zzt_qrcode_error_t remove(uint64_t id) {
        if (!id) return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        std::shared_ptr<SinkRegistration> target;
        { std::lock_guard<std::mutex> lock(writer_); auto current = std::atomic_load(&snapshot_); if (!current) return ZZT_QRCODE_ERROR_INVALID_HANDLE; for (const auto& e : current->entries) if (e.id == id) { target=e.registration; break; } if (!target) return ZZT_QRCODE_ERROR_INVALID_HANDLE;
          std::lock_guard<std::mutex> gate(target->mutex); if (target->state != RegistrationState::Active) return ZZT_QRCODE_ERROR_INVALID_HANDLE; target->state=RegistrationState::Retiring; }
        bool destroy=false; { std::lock_guard<std::mutex> gate(target->mutex); destroy=claim_destroy_locked(target); }
        if (destroy) complete_destroy(target);
        if (inside_log_user_code) return ZZT_QRCODE_OK;
        std::unique_lock<std::mutex> gate(target->mutex); target->cv.wait(gate, [&]{ return target->state == RegistrationState::Destroyed; }); return ZZT_QRCODE_OK;
    }
    zzt_qrcode_error_t set_level(uint64_t id, zzt_qrcode_log_level_t level) {
        if (!id) return ZZT_QRCODE_ERROR_INVALID_HANDLE;
        try { std::lock_guard<std::mutex> lock(writer_); auto old=std::atomic_load(&snapshot_); if (!old) return ZZT_QRCODE_ERROR_INVALID_HANDLE; bool found=false;
          for (const auto& e: old->entries) if (e.id == id) { std::lock_guard<std::mutex> gate(e.registration->mutex); found=e.registration->state == RegistrationState::Active; break; }
          if (!found) return ZZT_QRCODE_ERROR_INVALID_HANDLE;
          if (!valid_level(level)) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT;
          auto next=std::make_shared<SinkSnapshot>(); next->entries.reserve(old->entries.size());
          for (auto e: old->entries) { std::lock_guard<std::mutex> gate(e.registration->mutex); if (e.registration->state != RegistrationState::Active) continue; if (e.id == id) e.min_level=level; next->entries.push_back(std::move(e)); }
          recompute(*next); std::atomic_store(&snapshot_, std::const_pointer_cast<const SinkSnapshot>(next)); return ZZT_QRCODE_OK;
        } catch (const std::bad_alloc&) { return ZZT_QRCODE_ERROR_OUT_OF_MEMORY; } catch (...) { return ZZT_QRCODE_ERROR_INTERNAL; }
    }
    bool can_log(zzt_qrcode_log_level_t level) const { auto value=std::atomic_load(&snapshot_); return value && static_cast<int32_t>(level) >= value->min_level; }
    std::shared_ptr<const SinkSnapshot> snapshot() const { return std::atomic_load(&snapshot_); }
    void retire_all() noexcept { auto current=std::atomic_load(&snapshot_); if (!current) return; for (const auto& e: current->entries) { bool destroy=false; { std::lock_guard<std::mutex> gate(e.registration->mutex); if (e.registration->state == RegistrationState::Active) e.registration->state=RegistrationState::Retiring; destroy=claim_destroy_locked(e.registration); } if (destroy) complete_destroy(e.registration); } }
private:
    static void recompute(SinkSnapshot& snapshot) { snapshot.min_level=kNoLogLevel; for (const auto& e:snapshot.entries) snapshot.min_level=std::min(snapshot.min_level,e.min_level); }
    mutable std::mutex writer_; std::shared_ptr<const SinkSnapshot> snapshot_;
};

namespace {
Logger& runtime_logger() noexcept { static Logger value; return value; }
void dispatch_to(const std::shared_ptr<const SinkSnapshot>& snapshot, const LogContext& ctx, const char* operation, zzt_qrcode_log_level_t level, zzt_qrcode_error_t error, const std::string& message) noexcept {
    if (!snapshot || static_cast<int32_t>(level) < snapshot->min_level || message.size() > static_cast<size_t>(INT32_MAX)) return;
    for (const auto& entry : snapshot->entries) { if (static_cast<int32_t>(level) < entry.min_level) continue; auto r=entry.registration;
      bool already_active=false; for (auto* frame=active_registration_frame; frame; frame=frame->previous) if (frame->registration == r.get()) { already_active=true; break; }
      if (already_active) continue;
      bool invoke=false; { std::lock_guard<std::mutex> gate(r->mutex); if (r->state == RegistrationState::Active) { ++r->in_flight; invoke=true; } } if (!invoke) continue;
      try { zzt_qrcode_log_event_t event{sizeof(event), static_cast<int32_t>(level), static_cast<int32_t>(error), ctx.detector_id, ctx.result_id, operation, static_cast<uint32_t>(std::strlen(operation)), message.c_str(), static_cast<uint32_t>(message.size())}; CallbackScope scope(r.get()); r->callback(&event, r->user_data); } catch (...) {}
      bool destroy=false; { std::lock_guard<std::mutex> gate(r->mutex); --r->in_flight; destroy=claim_destroy_locked(r); } if (destroy) complete_destroy(r);
    }
}
}
uint64_t generate_object_id() { return next_id(g_object_ids); }
std::shared_ptr<Logger> lookup_logger(zzt_qrcode_logger_h logger) { return Handle<Logger, zzt_qrcode_logger_h>::get(logger); }
bool can_log(const LogContext& c, zzt_qrcode_log_level_t level) { auto& runtime=runtime_logger(); if (!c.route.custom_logger) return runtime.can_log(level); return c.route.custom_logger->can_log(level) || (c.route.propagate_to_runtime && runtime.can_log(level)); }
LogRoute runtime_log_route() { return {}; }
void dispatch_log(const LogContext& c, const char* op, zzt_qrcode_log_level_t level, zzt_qrcode_error_t error, const std::string& message) noexcept { auto& runtime=runtime_logger(); auto custom=c.route.custom_logger; auto custom_snapshot=custom ? custom->snapshot() : nullptr; auto runtime_snapshot=(!custom || c.route.propagate_to_runtime) ? runtime.snapshot() : nullptr; if (custom) dispatch_to(custom_snapshot,c,op,level,error,message); if (runtime_snapshot) dispatch_to(runtime_snapshot,c,op,level,error,message); }
}

using LoggerHandle = zzt::qrcode::Handle<zzt::qrcode::Logger, zzt_qrcode_logger_h>;
extern "C" {
zzt_qrcode_error_t zzt_qrcode_add_runtime_log_sink(const zzt_qrcode_log_sink_options_t* o, zzt_qrcode_log_sink_id_t* id) { return zzt::qrcode::runtime_logger().add(o,id); }
zzt_qrcode_error_t zzt_qrcode_remove_runtime_log_sink(zzt_qrcode_log_sink_id_t id) { return zzt::qrcode::runtime_logger().remove(id); }
zzt_qrcode_error_t zzt_qrcode_set_runtime_log_sink_level(zzt_qrcode_log_sink_id_t id,zzt_qrcode_log_level_t l) { return zzt::qrcode::runtime_logger().set_level(id,l); }
zzt_qrcode_error_t zzt_qrcode_create_logger(zzt_qrcode_logger_h* out) { if(!out) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT; *out=nullptr; try { *out=LoggerHandle::create_handle(); return ZZT_QRCODE_OK; } catch(const std::bad_alloc&) { return ZZT_QRCODE_ERROR_OUT_OF_MEMORY; } catch(...) { return ZZT_QRCODE_ERROR_INTERNAL; } }
zzt_qrcode_error_t zzt_qrcode_release_logger(zzt_qrcode_logger_h l) { if(!l || !LoggerHandle::release_handle(l)) return ZZT_QRCODE_ERROR_INVALID_HANDLE; return ZZT_QRCODE_OK; }
zzt_qrcode_error_t zzt_qrcode_logger_add_sink(zzt_qrcode_logger_h l,const zzt_qrcode_log_sink_options_t* o,zzt_qrcode_log_sink_id_t* id) { if(!id) return ZZT_QRCODE_ERROR_INVALID_ARGUMENT; *id=0; auto p=LoggerHandle::get(l); if(!p) return ZZT_QRCODE_ERROR_INVALID_HANDLE; return p->add(o,id); }
zzt_qrcode_error_t zzt_qrcode_logger_remove_sink(zzt_qrcode_logger_h l,zzt_qrcode_log_sink_id_t id) { auto p=LoggerHandle::get(l); if(!p) return ZZT_QRCODE_ERROR_INVALID_HANDLE; return p->remove(id); }
zzt_qrcode_error_t zzt_qrcode_logger_set_sink_level(zzt_qrcode_logger_h l,zzt_qrcode_log_sink_id_t id,zzt_qrcode_log_level_t level) { auto p=LoggerHandle::get(l); if(!p) return ZZT_QRCODE_ERROR_INVALID_HANDLE; return p->set_level(id,level); }
}
