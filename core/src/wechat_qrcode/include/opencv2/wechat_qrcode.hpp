// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.

#ifndef __OPENCV_WECHAT_QRCODE_HPP__
#define __OPENCV_WECHAT_QRCODE_HPP__
#include "simpleocv.h"

/** @defgroup wechat_qrcode WeChat QR code detector for detecting and parsing QR code.
 */
namespace cv {
namespace wechat_qrcode {
//! @addtogroup wechat_qrcode
//! @{
/**
 * @brief  WeChat QRCode includes two CNN-based models:
 * A object detection model and a super resolution model.
 * Object detection model is applied to detect QRCode with the bounding box.
 * super resolution model is applied to zoom in QRCode when it is small.
 *
 */
class WeChatQRCode {
public:
    /**
     * @brief Initialize the WeChatQRCode.
     */
    WeChatQRCode();
    ~WeChatQRCode(){};

    /**
     * @brief  Both detects and decodes QR code.
     * To simplify the usage, there is a only API: detectAndDecode
     *
     * @param img supports grayscale or color (BGR) image.
     * @param points optional output array of vertices of the found QR code quadrangle. Will be
     * empty if not found.
     * @return list of decoded string.
     */
    std::vector<std::string> detectAndDecode(cv::Mat &img, std::vector<cv::Mat> &points);

    /**
    * @brief set scale factor
    * QR code detector use neural network to detect QR.
    * Before running the neural network, the input image is pre-processed by scaling.
    * By default, the input image is scaled to an image with an area of 160000 pixels.
    * The scale factor allows to use custom scale the input image:
    * width = scaleFactor*width
    * height = scaleFactor*width
    *
    * scaleFactor valuse must be > 0 and <= 1, otherwise the scaleFactor value is set to -1
    * and use default scaled to an image with an area of 160000 pixels.
    */
    void setScaleFactor(float _scalingFactor);

    float getScaleFactor();

protected:
    class Impl;
    std::shared_ptr<Impl> p;
};

//! @}
}  // namespace wechat_qrcode
}  // namespace cv
#endif  // __OPENCV_WECHAT_QRCODE_HPP__
