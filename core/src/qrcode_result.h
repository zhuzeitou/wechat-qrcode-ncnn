#ifndef ZZT_QRCODE_RESULT_H
#define ZZT_QRCODE_RESULT_H

#include <string>

#include "simpleocv.h"

namespace zzt::qrcode {
class QrcodeResult {
public:
    QrcodeResult(std::string text, const cv::Mat &result_points);
    ~QrcodeResult();

    QrcodeResult(const QrcodeResult &) = delete;
    QrcodeResult &operator=(const QrcodeResult &) = delete;
    QrcodeResult(QrcodeResult &&) = delete;
    QrcodeResult &operator=(QrcodeResult &&) = delete;

    [[nodiscard]] const std::string &get_text() const;
    [[nodiscard]] const cv::Mat &get_result_points() const;

private:
    std::string text;
    cv::Mat result_points;
};
}  // namespace zzt::qrcode

#endif  // ZZT_QRCODE_RESULT_H
