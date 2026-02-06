// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.
//
// Tencent is pleased to support the open source community by making WeChat QRCode available.
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
#include "../precomp.hpp"
#include "super_scale.hpp"
#ifdef SR_USE_OPT_MODEL
#include "sr_opt.id.h"
#include "sr_opt.mem.h"
#else
#include "sr.id.h"
#include "sr.mem.h"
#endif

namespace cv {
namespace wechat_qrcode {
int SuperScale::init() {
    srnet_.opt.num_threads = 1;
    srnet_.load_param(sr_param_bin);
    srnet_.load_model(sr_bin);
    net_loaded_ = true;
    return 0;
}

Mat SuperScale::processImageScale(const Mat &src, float scale, const bool &use_sr,
                                  int sr_max_size) {
    Mat dst = src;
    if (scale == 1.0) {  // src
        return dst;
    }

    int width = src.cols;
    int height = src.rows;
    int target_width = width * scale;
    int target_height = height * scale;
    if (scale == 2.0) {  // upsample
        int SR_TH = sr_max_size;
        if (use_sr && (int)sqrt(width * height * 1.0) < SR_TH && net_loaded_) {
            int ret = superResoutionScale(src, dst);
            if (ret == 0) return dst;
        }

        {
            dst.create(target_height, target_width, CV_8UC1);
            ncnn::Mat ncnn_src = ncnn::Mat::from_pixels(src.data, ncnn::Mat::PIXEL_GRAY, src.cols, src.rows);
            ncnn::Mat ncnn_dst;
            ncnn::resize_bicubic(ncnn_src, ncnn_dst, target_width, target_height);
            ncnn_dst.to_pixels(dst.data, ncnn::Mat::PIXEL_GRAY);
        }
    } else if (scale < 1.0) {  // downsample
        dst.create(target_height, target_width, CV_8UC1);
        ncnn::resize_bilinear_c1(src.data, width, height, dst.data, target_width, target_height);
    }

    return dst;
}

int SuperScale::superResoutionScale(const Mat &src, Mat &dst) {
    ncnn::Mat blob = ncnn::Mat::from_pixels(src.data, ncnn::Mat::PIXEL_GRAY, src.cols, src.rows);
    const float norm_vals[] = { 1.f / 255.f };
    blob.substract_mean_normalize(nullptr, norm_vals);

    ncnn::Extractor ex = srnet_.create_extractor();
    ex.input(sr_param_id::BLOB_data, blob);

    ncnn::Mat prob;
    ex.extract(sr_param_id::BLOB_fc, prob);

    dst = Mat(prob.h, prob.w, CV_8UC1);

    int cnt = 0;
    for (int row = 0; row < prob.h; row++) {
        for (int col = 0; col < prob.w; col++) {
            float pixel = prob[cnt++] * 255.f;
            dst.ptr<uint8_t>(row)[col] = static_cast<uint8_t>(std::clamp(pixel, 0.f, 255.f));
        }
    }

    return 0;
}
}  // namespace wechat_qrcode
}  // namespace cv