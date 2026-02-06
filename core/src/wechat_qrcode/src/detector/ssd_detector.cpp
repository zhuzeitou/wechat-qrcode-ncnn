// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#include "../precomp.hpp"
#include "ssd_detector.hpp"
#ifdef DETECT_USE_OPT_MODEL
#include "detect_opt.id.h"
#include "detect_opt.mem.h"
#else
#include "detect.id.h"
#include "detect.mem.h"
#endif
namespace cv {
namespace wechat_qrcode {
int SSDDetector::init() {
    net_.opt.num_threads = 1;
    net_.load_param(detect_param_bin);
    net_.load_model(detect_bin);
    return 0;
}

vector<Mat> SSDDetector::forward(Mat img, const int target_width, const int target_height) {
    int img_w = img.cols;
    int img_h = img.rows;
    ncnn::Mat ncnn_img = ncnn::Mat::from_pixels(img.data, ncnn::Mat::PIXEL_GRAY, img_w, img_h);

    ncnn::Mat ncnn_input;
    ncnn::resize_bicubic(ncnn_img, ncnn_input, target_width, target_height);
    const float norm_vals[] = { 1.f / 255.f };
    ncnn_input.substract_mean_normalize(nullptr, norm_vals);
    ncnn::Extractor ex = net_.create_extractor();
    ex.input(detect_param_id::BLOB_data, ncnn_input);

    ncnn::Mat prob;
    ex.extract(detect_param_id::BLOB_detection_output, prob);

    vector<Mat> point_list;
    for (int row = 0; row < prob.h; row++) {
        float* prob_score = (float*)prob.channel(0) + 6 * row;
        if (prob_score[0] == 1 && prob_score[1] > 1E-5) {
            auto point = cv::Mat(4, 2, CV_32FC1);
            float x0 = std::clamp(prob_score[2] * img_w, 0.f, img_w - 1.f);
            float y0 = std::clamp(prob_score[3] * img_h, 0.f, img_h - 1.f);
            float x1 = std::clamp(prob_score[4] * img_w, 0.f, img_w - 1.f);
            float y1 = std::clamp(prob_score[5] * img_h, 0.f, img_h - 1.f);

            point.ptr<float>(0)[0] = x0;
            point.ptr<float>(0)[1] = y0;
            point.ptr<float>(1)[0] = x1;
            point.ptr<float>(1)[1] = y0;
            point.ptr<float>(2)[0] = x1;
            point.ptr<float>(2)[1] = y1;
            point.ptr<float>(3)[0] = x0;
            point.ptr<float>(3)[1] = y1;
            point_list.push_back(point);
        }
    }
    return point_list;
}
}  // namespace wechat_qrcode
}  // namespace cv