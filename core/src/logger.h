#ifndef ZZT_QRCODE_LOGGER_H
#define ZZT_QRCODE_LOGGER_H

#include "zzt_qrcode/qrcode.h"

#include <cstdint>
#include <memory>
#include <string>

namespace zzt::qrcode {
class Logger;
struct LogRoute {
    std::shared_ptr<Logger> custom_logger;
    bool propagate_to_runtime = false;
};
struct LogContext {
    LogRoute route;
    uint64_t detector_id = 0;
    uint64_t result_id = 0;
};

bool can_log(const LogContext& context, zzt_qrcode_log_level_t level);
void dispatch_log(const LogContext& context, const char* operation, zzt_qrcode_log_level_t level,
                  zzt_qrcode_error_t error_code, const std::string& message) noexcept;
uint64_t generate_object_id();
std::shared_ptr<Logger> lookup_logger(zzt_qrcode_logger_h logger);
LogRoute runtime_log_route();
}
#endif
