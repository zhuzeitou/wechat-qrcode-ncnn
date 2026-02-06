#include "qrcode_result.h"

#include <iostream>
#include <utility>

namespace zzt::qrcode {
QrcodeResult::QrcodeResult(std::string text, const cv::Mat &result_points)
    : text(std::move(text)), result_points(result_points) {}

QrcodeResult::~QrcodeResult() = default;

const std::string &QrcodeResult::get_text() const { return text; }

const cv::Mat &QrcodeResult::get_result_points() const { return result_points; }
}  // namespace zzt::qrcode
